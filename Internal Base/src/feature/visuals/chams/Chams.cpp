#include "Chams.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <excpt.h>

#include "../../../../ext/minhook/MinHook.h"

#include "../../../sdk/source2/SceneObject.h"
#include "../../../sdk/source2/MaterialSystem.h"
#include "../../../sdk/memory/PatternScan.h"
#include "../../../sdk/utils/Globals.h"

namespace
{
    // Hook target lives in scenesystem.dll on older builds. On builds
    // where SceneSystem_002 is registered by materialsystem2.dll the
    // implementation typically lives there. We deliberately keep this
    // list short and ordered by likelihood - matching a wrong function
    // crashes the game, so we'd rather miss than guess.
    constexpr const char* kHookCandidateModules[] = {
        "scenesystem.dll",
        "materialsystem2.dll",
    };

    // Strict prologue for CAnimatableSceneObjectDesc::RenderObjects on
    // recent builds. Loose alternates were removed in this patch
    // because they generate false positives in arbitrary engine code,
    // and a wrong target = guaranteed crash on first dispatch.
    constexpr const char* kRenderObjectsPattern =
        "48 8B C4 53 57 41 54 48 81 EC ?? ?? ?? ?? 49 63 F9 49";

    TRenderObjects g_pOriginalRenderObjects = nullptr;
    void*          g_pHookTarget = nullptr;

    void* g_pPlayerMaterial = nullptr;
    void* g_pWeaponMaterial = nullptr;
    void* g_pHandsMaterial  = nullptr;

    bool  g_bMaterialsRequested = false;
    std::atomic<bool> g_bHookEnabled{ false };

    Chams::Diag g_Diag;

    constexpr const char* kChamsKV3 =
        "<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->\n"
        "{\n"
        "    shader = \"csgo_character.vfx\"\n"
        "    F_DISABLE_Z_BUFFERING = 1\n"
        "    F_DISABLE_Z_PREPASS = 1\n"
        "    F_DISABLE_Z_WRITE = 1\n"
        "    F_BLEND_MODE = 1\n"
        "    g_vColorTint = [1.0, 1.0, 1.0, 1.0]\n"
        "    g_bFogEnabled = 0\n"
        "    g_flMetalness = 0.000\n"
        "    g_tColor = resource:\"materials/dev/primary_white_color_tga_21186c76.vtex\"\n"
        "    g_tAmbientOcclusion = resource:\"materials/default/default_ao_tga_79a2e0d0.vtex\"\n"
        "    g_tNormal = resource:\"materials/default/default_normal_tga_1b833b2a.vtex\"\n"
        "    g_tMetalness = resource:\"materials/default/default_metal_tga_8fbc2820.vtex\"\n"
        "}\n";

    enum class ChamsKind : uint8_t { None = 0, Player = 1, Weapon = 2, Hands = 3 };

    inline bool ContainsCI(const char* haystack, const char* needle)
    {
        if (!haystack || !needle) return false;
        return std::strstr(haystack, needle) != nullptr;
    }

    ChamsKind ClassifyByModelPath(const char* path)
    {
        if (!path) return ChamsKind::None;
        if (ContainsCI(path, "arms") || ContainsCI(path, "gloves"))
            return ChamsKind::Hands;
        if (ContainsCI(path, "weapons/") || ContainsCI(path, "/v_"))
            return ChamsKind::Weapon;
        if (ContainsCI(path, "characters/") || ContainsCI(path, "models/player"))
            return ChamsKind::Player;
        return ChamsKind::None;
    }

    // Cheap check: is `p` a committed, readable user-mode page?
    // Uses VirtualQuery so a bad pointer in the detour body's switch
    // branch can't crash the process; we just skip the entry instead.
    inline bool IsReadablePtr(const void* p, size_t bytes = 1)
    {
        if (!p) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT) return false;
        const DWORD prot = mbi.Protect & 0xFF;
        if (prot == PAGE_NOACCESS || (mbi.Protect & PAGE_GUARD)) return false;
        // ensure the requested range stays inside the committed region
        const auto base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const auto end  = base + mbi.RegionSize;
        const auto pa   = reinterpret_cast<uintptr_t>(p);
        if (pa + bytes > end) return false;
        return true;
    }

    const char* GetModelPath(const CBaseSceneData* data) noexcept
    {
        if (!IsReadablePtr(data, 0x10)) return nullptr;
        auto p1 = *reinterpret_cast<const uintptr_t*>(data);
        if (!IsReadablePtr(reinterpret_cast<const void*>(p1), 0x10)) return nullptr;
        auto p2 = *reinterpret_cast<const uintptr_t*>(p1 + 0x8);
        if (!IsReadablePtr(reinterpret_cast<const void*>(p2), 0x8)) return nullptr;
        auto p3 = *reinterpret_cast<const uintptr_t*>(p2);
        if (!IsReadablePtr(reinterpret_cast<const void*>(p3 + 0x8), 0x8)) return nullptr;
        const char* str = *reinterpret_cast<const char* const*>(p3 + 0x8);
        if (!IsReadablePtr(str, 1)) return nullptr;
        return str;
    }

    inline uint8_t F2B(float f)
    {
        if (f < 0.f) f = 0.f;
        if (f > 1.f) f = 1.f;
        return static_cast<uint8_t>(f * 255.f + 0.5f);
    }

    void ApplyOverride(CBaseSceneData* d, void* mat, const float color[4])
    {
        if (!d || !mat) return;
        d->m_pMaterial  = mat;
        d->m_pMaterial2 = mat;
        d->m_r = F2B(color[0]);
        d->m_g = F2B(color[1]);
        d->m_b = F2B(color[2]);
        d->m_a = F2B(color[3]);
    }

    void DbgLog(const char* fmt, ...)
    {
        char buf[512];
        va_list va;
        va_start(va, fmt);
        _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, va);
        va_end(va);
        OutputDebugStringA(buf);
    }

    // SEH filter for the detour. Catches AVs and similar from wrong-
    // target hooks, disables the hook so we don't keep firing into bad
    // memory, and bumps a diagnostic counter.
    LONG DetourSehFilter(EXCEPTION_POINTERS* ep)
    {
        (void)ep;
        g_Diag.detour_seh_catches++;
        g_bHookEnabled.store(false, std::memory_order_release);
        DbgLog("[fccp][chams] detour SEH catch - hook auto-disabled\n");
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // Body of the detour, separated so the outer __try/__except wrapper
    // doesn't have to deal with C++ object lifetime restrictions.
    // Returns true if it ran to completion without faulting.
    bool RunDetourBody(void* /*thisPtr*/,
                       void* /*a2*/,
                       CBaseSceneData* pData,
                       int32_t count)
    {
        const bool wantPlayer = Globals::chams_player_enabled && g_pPlayerMaterial;
        const bool wantWeapon = Globals::chams_weapon_enabled && g_pWeaponMaterial;
        const bool wantHands  = Globals::chams_hands_enabled  && g_pHandsMaterial;

        uint32_t cP = 0, cW = 0, cH = 0;
        bool overrode_any = false;

        for (int32_t i = 0; i < count; ++i)
        {
            CBaseSceneData* d = &pData[i];
            const char* model = GetModelPath(d);
            if (!model) continue;

            switch (ClassifyByModelPath(model))
            {
            case ChamsKind::Player:
                cP++;
                if (wantPlayer) { ApplyOverride(d, g_pPlayerMaterial, Globals::chams_player_visible_color); overrode_any = true; }
                break;
            case ChamsKind::Weapon:
                cW++;
                if (wantWeapon) { ApplyOverride(d, g_pWeaponMaterial, Globals::chams_weapon_color); overrode_any = true; }
                break;
            case ChamsKind::Hands:
                cH++;
                if (wantHands)  { ApplyOverride(d, g_pHandsMaterial,  Globals::chams_hands_color);  overrode_any = true; }
                break;
            default: break;
            }
        }

        g_Diag.last_classified_player = cP;
        g_Diag.last_classified_weapon = cW;
        g_Diag.last_classified_hands  = cH;
        if (overrode_any) g_Diag.calls_overridden++;
        return true;
    }

    // The detour entrypoint. Hot path. We ALWAYS call the original
    // afterwards so the engine still renders even if our body skipped
    // (because the hook is disabled / args look invalid / SEH caught).
    void* __fastcall hkRenderObjects(
        void* thisPtr,
        void* a2,
        CBaseSceneData* pData,
        int32_t count,
        void* a5, void* a6, void* a7)
    {
        g_Diag.calls_total++;

        if (g_bHookEnabled.load(std::memory_order_relaxed)
            && pData && count > 0
            && IsReadablePtr(pData, sizeof(CBaseSceneData)))
        {
            __try
            {
                RunDetourBody(thisPtr, a2, pData, count);
            }
            __except (DetourSehFilter(GetExceptionInformation()))
            {
                // Filter already disabled the hook + bumped counter.
            }
        }

        return g_pOriginalRenderObjects(thisPtr, a2, pData, count, a5, a6, a7);
    }

    // Pattern-scan the candidate modules in order. Returns 0 if nothing
    // matched. Updates `outModule` with which module hit.
    uintptr_t ScanRenderObjectsTarget(char outModule[32])
    {
        outModule[0] = 0;
        for (const char* mod : kHookCandidateModules)
        {
            if (!GetModuleHandleA(mod))
                continue;
            uintptr_t hit = Memory::PatternScan(mod, kRenderObjectsPattern);
            if (hit)
            {
                std::strncpy(outModule, mod, 31);
                outModule[31] = 0;
                DbgLog("[fccp][chams] RenderObjects scan hit in %s, addr=0x%016llX\n",
                    mod, (unsigned long long)hit);
                return hit;
            }
        }
        return 0;
    }
}

namespace Chams
{
    void Probe()
    {
        // Module presence
        g_Diag.mod_materialsystem2  = GetModuleHandleA("materialsystem2.dll")  != nullptr;
        g_Diag.mod_tier0            = GetModuleHandleA("tier0.dll")            != nullptr;
        g_Diag.mod_scenesystem      = GetModuleHandleA("scenesystem.dll")      != nullptr;
        g_Diag.mod_client           = GetModuleHandleA("client.dll")           != nullptr;
        g_Diag.mod_rendersystemdx11 = GetModuleHandleA("rendersystemdx11.dll") != nullptr;

        // SDK side - pull MaterialSystem flags into g_Diag whether it
        // succeeded or not so the menu can tell us where bringup died.
        (void)MaterialSystem::Init();
        const auto& s = MaterialSystem::GetStatus();
        g_Diag.createinterface_export = s.createinterface_ok;
        g_Diag.matsys_singleton       = s.singleton_ok;
        g_Diag.tier0_loadkv3          = s.loadkv3_ok;
        g_Diag.creatematerial_pattern = s.createmat_ok;

        // Pattern scan (look-only). Don't touch addresses we already
        // hooked - re-running scan after install is fine but writing
        // through a stale g_pHookTarget would clobber the live target.
        if (!IsInstalled())
        {
            char modName[32] = { 0 };
            uintptr_t target = ScanRenderObjectsTarget(modName);
            if (target)
            {
                g_Diag.renderobjects_pattern = true;
                g_Diag.renderobjects_variant = 0;
                std::memcpy(g_Diag.renderobjects_module, modName, sizeof(g_Diag.renderobjects_module));
                g_Diag.hook_target_addr = target;
            }
            else
            {
                g_Diag.renderobjects_pattern = false;
                g_Diag.hook_target_addr = 0;
            }
        }
    }

    bool TryInstall()
    {
        if (IsInstalled())
            return true;

        Probe();

        g_Diag.hook_install_attempted = true;

        if (!g_Diag.creatematerial_pattern)
        {
            DbgLog("[fccp][chams] TryInstall: MaterialSystem not ready\n");
            return false;
        }
        if (!g_Diag.renderobjects_pattern || !g_Diag.hook_target_addr)
        {
            DbgLog("[fccp][chams] TryInstall: RenderObjects pattern not found\n");
            return false;
        }

        g_pHookTarget = reinterpret_cast<void*>(g_Diag.hook_target_addr);

        MH_STATUS mhc = MH_CreateHook(g_pHookTarget,
                                      reinterpret_cast<void*>(&hkRenderObjects),
                                      reinterpret_cast<void**>(&g_pOriginalRenderObjects));
        if (mhc != MH_OK && mhc != MH_ERROR_ALREADY_CREATED)
        {
            DbgLog("[fccp][chams] MH_CreateHook failed (%d)\n", (int)mhc);
            return false;
        }
        g_Diag.hook_created = true;

        if (MH_EnableHook(g_pHookTarget) != MH_OK)
        {
            DbgLog("[fccp][chams] MH_EnableHook failed\n");
            return false;
        }
        g_Diag.hook_enabled = true;

        g_bHookEnabled.store(true, std::memory_order_release);
        DbgLog("[fccp][chams] hook installed OK\n");
        return true;
    }

    void Uninstall()
    {
        g_bHookEnabled.store(false, std::memory_order_release);

        if (g_pHookTarget)
        {
            MH_DisableHook(g_pHookTarget);
            MH_RemoveHook(g_pHookTarget);
            g_pHookTarget = nullptr;
        }

        g_Diag.hook_enabled = false;
        g_Diag.hook_created = false;

        g_pPlayerMaterial = nullptr;
        g_pWeaponMaterial = nullptr;
        g_pHandsMaterial  = nullptr;
        g_pOriginalRenderObjects = nullptr;
        g_bMaterialsRequested = false;

        g_Diag.material_player = false;
        g_Diag.material_weapon = false;
        g_Diag.material_hands  = false;
    }

    bool IsInstalled()
    {
        return g_bHookEnabled.load(std::memory_order_acquire);
    }

    bool Init()
    {
        // Compatibility shim: probe only. The detour is only ever
        // installed by the explicit "Install hook" button - never on
        // module load - because a wrong pattern match here used to
        // crash the game on first scene render.
        Probe();
        return true;
    }

    void Shutdown()
    {
        Uninstall();
    }

    void OnNewFrame()
    {
        // Cheap probe so the diag panel is live. No hook install here.
        Probe();

        if (!IsInstalled())
            return;

        if (!g_bMaterialsRequested)
        {
            g_bMaterialsRequested = true;
            g_pPlayerMaterial = MaterialSystem::CreateChamsMaterial("fccp/chams_player", kChamsKV3);
            g_pWeaponMaterial = MaterialSystem::CreateChamsMaterial("fccp/chams_weapon", kChamsKV3);
            g_pHandsMaterial  = MaterialSystem::CreateChamsMaterial("fccp/chams_hands",  kChamsKV3);
            g_Diag.material_player = g_pPlayerMaterial != nullptr;
            g_Diag.material_weapon = g_pWeaponMaterial != nullptr;
            g_Diag.material_hands  = g_pHandsMaterial  != nullptr;
            DbgLog("[fccp][chams] materials: P=%d W=%d H=%d\n",
                (int)g_Diag.material_player, (int)g_Diag.material_weapon, (int)g_Diag.material_hands);
        }
    }

    const Diag& GetDiag()
    {
        return g_Diag;
    }
}
