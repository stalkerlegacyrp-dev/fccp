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

#include "../../../../ext/minhook/MinHook.h"

#include "../../../sdk/source2/SceneObject.h"
#include "../../../sdk/source2/MaterialSystem.h"
#include "../../../sdk/memory/PatternScan.h"
#include "../../../sdk/utils/Globals.h"

namespace
{
    // Hook target lives in scenesystem.dll on older builds, but on current
    // builds the SceneSystem_002 interface is registered by materialsystem2.dll
    // so the implementation may live there instead. Try the scenesystem
    // module first then fall back to the other engine modules that have
    // historically held this code.
    constexpr const char* kHookCandidateModules[] = {
        "scenesystem.dll",
        "materialsystem2.dll",
        "rendersystemdx11.dll",
        "client.dll",
    };

    // Several prologue patterns to attempt for CAnimatableSceneObjectDesc::RenderObjects.
    constexpr const char* kRenderObjectsPatterns[] = {
        "48 8B C4 53 57 41 54 48 81 EC ?? ?? ?? ?? 49 63 F9 49",
        "48 8B C4 53 57 41 54 48 81 EC ?? ?? ?? ??",
        "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 41 56 41 57 48 81 EC ?? ?? ?? ??",
        "40 53 56 57 41 54 41 55 41 56 41 57 48 81 EC ?? ?? ?? ?? 48 8B",
    };

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

    const char* GetModelPath(const CBaseSceneData* data) noexcept
    {
        if (!data) return nullptr;
        auto p1 = *reinterpret_cast<const uintptr_t*>(data);
        if (!p1) return nullptr;
        auto p2 = *reinterpret_cast<const uintptr_t*>(p1 + 0x8);
        if (!p2) return nullptr;
        auto p3 = *reinterpret_cast<const uintptr_t*>(p2);
        if (!p3) return nullptr;
        return *reinterpret_cast<const char* const*>(p3 + 0x8);
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

    // The detour. Hot path. Increments live counters in g_Diag for the
    // diagnostic UI even when nothing matches the classifier - the user
    // can read off the panel whether the hook is firing at all.
    void* __fastcall hkRenderObjects(
        void* thisPtr,
        void* a2,
        CBaseSceneData* pData,
        int32_t count,
        void* a5, void* a6, void* a7)
    {
        g_Diag.calls_total++;

        if (!g_bHookEnabled.load(std::memory_order_relaxed) || !pData || count <= 0)
            return g_pOriginalRenderObjects(thisPtr, a2, pData, count, a5, a6, a7);

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

        return g_pOriginalRenderObjects(thisPtr, a2, pData, count, a5, a6, a7);
    }

    // Try every (module, pattern) combination. Returns 0 if nothing matched.
    uintptr_t ResolveRenderObjectsTarget(int& outVariantIdx, char outModule[32])
    {
        outVariantIdx = -1;
        outModule[0] = 0;

        for (const char* mod : kHookCandidateModules)
        {
            if (!GetModuleHandleA(mod))
                continue;
            const int npat = (int)(sizeof(kRenderObjectsPatterns) / sizeof(kRenderObjectsPatterns[0]));
            for (int v = 0; v < npat; ++v)
            {
                uintptr_t hit = Memory::PatternScan(mod, kRenderObjectsPatterns[v]);
                if (hit)
                {
                    outVariantIdx = v;
                    std::strncpy(outModule, mod, 31);
                    outModule[31] = 0;
                    DbgLog("[fccp][chams] RenderObjects hit in %s, variant %d, addr=0x%016llX\n",
                        mod, v, (unsigned long long)hit);
                    return hit;
                }
            }
        }
        return 0;
    }
}

namespace Chams
{
    bool Init()
    {
        // Module presence is recorded every call so the menu can reflect
        // the engine bringup state in real time.
        g_Diag.mod_materialsystem2  = GetModuleHandleA("materialsystem2.dll")  != nullptr;
        g_Diag.mod_tier0            = GetModuleHandleA("tier0.dll")            != nullptr;
        g_Diag.mod_scenesystem      = GetModuleHandleA("scenesystem.dll")      != nullptr;
        g_Diag.mod_client           = GetModuleHandleA("client.dll")           != nullptr;
        g_Diag.mod_rendersystemdx11 = GetModuleHandleA("rendersystemdx11.dll") != nullptr;

        // SDK side - also pull MaterialSystem flags into g_Diag whether
        // it succeeded or not so the menu can tell us where bringup died.
        const bool matSysOK = MaterialSystem::Init();
        const auto& s = MaterialSystem::GetStatus();
        g_Diag.createinterface_export = s.createinterface_ok;
        g_Diag.matsys_singleton       = s.singleton_ok;
        g_Diag.tier0_loadkv3          = s.loadkv3_ok;
        g_Diag.creatematerial_pattern = s.createmat_ok;
        if (!matSysOK)
            return false;

        // Pattern scan + hook install
        int variant = -1;
        char modName[32] = { 0 };
        uintptr_t target = ResolveRenderObjectsTarget(variant, modName);
        if (!target)
        {
            g_Diag.renderobjects_pattern = false;
            return false;
        }

        g_Diag.renderobjects_pattern = true;
        g_Diag.renderobjects_variant = variant;
        std::memcpy(g_Diag.renderobjects_module, modName, sizeof(g_Diag.renderobjects_module));
        g_Diag.hook_target_addr = target;

        g_pHookTarget = reinterpret_cast<void*>(target);

        if (MH_CreateHook(g_pHookTarget,
                          reinterpret_cast<void*>(&hkRenderObjects),
                          reinterpret_cast<void**>(&g_pOriginalRenderObjects)) != MH_OK)
        {
            DbgLog("[fccp][chams] MH_CreateHook failed\n");
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

    void Shutdown()
    {
        g_bHookEnabled.store(false, std::memory_order_release);

        if (g_pHookTarget)
        {
            MH_DisableHook(g_pHookTarget);
            MH_RemoveHook(g_pHookTarget);
            g_pHookTarget = nullptr;
        }

        g_pPlayerMaterial = nullptr;
        g_pWeaponMaterial = nullptr;
        g_pHandsMaterial  = nullptr;
        g_pOriginalRenderObjects = nullptr;
        g_bMaterialsRequested = false;
    }

    void OnNewFrame()
    {
        if (!g_bHookEnabled.load(std::memory_order_acquire))
        {
            // Retry every frame until we either succeed or run out of
            // patterns to try. Init is cheap and idempotent.
            (void)Init();
            return;
        }

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
