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

#include "../../../../ext/minhook/MinHook.h"

#include "../../../sdk/source2/SceneObject.h"
#include "../../../sdk/source2/MaterialSystem.h"
#include "../../../sdk/memory/PatternScan.h"
#include "../../../sdk/utils/Globals.h"

namespace
{
    // ---------------------------------------------------------------------
    // Hook target: CAnimatableSceneObjectDesc::RenderObjects in
    // scenesystem.dll. The function prologue has been stable for many
    // CS2 builds. Pattern lifted from PureLiquid-CS2-External
    // (CS2/Include/CS2/Patterns.h) - the leading `48 8B C4` (mov rax,
    // rsp) + `53 57 41 54` (push rbx,rdi,r12) + `48 81 EC ?? ?? ?? ??`
    // (sub rsp, imm32) + `49 63 F9 49` (movsxd r9,? + REX.W rex)
    // collectively pin this exact function.
    // ---------------------------------------------------------------------
    constexpr const char* kRenderObjectsPattern =
        "48 8B C4 53 57 41 54 48 81 EC ?? ?? ?? ?? 49 63 F9 49";

    TRenderObjects g_pOriginalRenderObjects = nullptr;
    void*          g_pHookTarget = nullptr;

    // Lazy-built materials. nullptr until first use; if creation fails we
    // leave them nullptr permanently and skip the per-category override.
    void* g_pPlayerMaterial = nullptr; // visible color (no z-test pass)
    void* g_pWeaponMaterial = nullptr;
    void* g_pHandsMaterial  = nullptr;

    bool  g_bMaterialsRequested = false; // once-flag for OnNewFrame builder
    std::atomic<bool> g_bHookEnabled{ false };

    // -----------------------------------------------------------------
    // KV3 vmat blobs.
    //
    // The shader/flag set is identical to public CS2 chams material
    // dumps (csgo_character.vfx + DISABLE_Z_* + BLEND_MODE=1). The
    // engine reads g_vColorTint as a vec4, but at draw time the
    // per-object tint we set via CBaseSceneData::m_r/g/b/a multiplies
    // it - so we deliberately leave g_vColorTint at white to give the
    // user-chosen RGBA full authority.
    //
    // csgo_character.vfx is the 'right' shader for player models and
    // happens to also render fine on weapon and arm meshes (the shader
    // gracefully degrades when the per-vertex bone weights are absent
    // or the mesh has no skinning data). This is the same trick the
    // public references use - they only ever build one chams material
    // and apply it to every category. We split into three in case we
    // want to change shaders later, but the textual KV3 is identical
    // today.
    // -----------------------------------------------------------------
    constexpr const char* kChamsKV3 = R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "csgo_character.vfx"
    F_DISABLE_Z_BUFFERING = 1
    F_DISABLE_Z_PREPASS = 1
    F_DISABLE_Z_WRITE = 1
    F_BLEND_MODE = 1
    g_vColorTint = [1.0, 1.0, 1.0, 1.0]
    g_bFogEnabled = 0
    g_flMetalness = 0.000
    g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
    g_tAmbientOcclusion = resource:"materials/default/default_ao_tga_79a2e0d0.vtex"
    g_tNormal = resource:"materials/default/default_normal_tga_1b833b2a.vtex"
    g_tMetalness = resource:"materials/default/default_metal_tga_8fbc2820.vtex"
})";

    // -----------------------------------------------------------------
    // Model-path classification.
    //
    // CS2 model conventions (current build):
    //   - Player bodies:        "characters/models/.../<character>.vmdl"
    //                       or  "models/player/..."
    //   - Viewmodel weapons:    "weapons/models/<weapon>/v_<weapon>.vmdl"
    //                       or  start with "weapons/" and contain "/v_"
    //   - Viewmodel arms+gloves:contain "arms" (e.g. "characters/models/ctm_st6/v_arms_st6.vmdl")
    //
    // Tested precedence:
    //   hands  > weapon > player
    // because "weapons/.../arms_..." would otherwise be misclassified.
    // -----------------------------------------------------------------
    enum class ChamsKind : uint8_t
    {
        None    = 0,
        Player  = 1,
        Weapon  = 2,
        Hands   = 3,
    };

    inline bool ContainsCI(const char* haystack, const char* needle)
    {
        if (!haystack || !needle) return false;
        // Model paths are lowercase by Source2 convention. Plain strstr is fine.
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

    // CBaseSceneData starts with a chain of model-impl pointers. The string
    // we want lives at:
    //
    //   ((char**)((**(uintptr_t**)data) + 0x8))[0x8 / 8]
    //
    // Concretely: deref(data) -> object header; +0x8 -> model impl ptr;
    //             deref that  -> impl object;  +0x8 -> char* path.
    //
    // This chain is sensitive to Source2 layout; if a future build moves
    // it the classifier just returns None and chams silently no-op (no
    // crash, because we null-check every step).
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

    // ---------------------------------------------------------------------
    // The detour. Hot path. Keep it branch-light.
    // ---------------------------------------------------------------------
    void* __fastcall hkRenderObjects(
        void* thisPtr,
        void* a2,
        CBaseSceneData* pData,
        int32_t count,
        void* a5, void* a6, void* a7)
    {
        if (!g_bHookEnabled.load(std::memory_order_relaxed) || !pData || count <= 0)
            return g_pOriginalRenderObjects(thisPtr, a2, pData, count, a5, a6, a7);

        const bool wantPlayer = Globals::chams_player_enabled && g_pPlayerMaterial;
        const bool wantWeapon = Globals::chams_weapon_enabled && g_pWeaponMaterial;
        const bool wantHands  = Globals::chams_hands_enabled  && g_pHandsMaterial;

        if (wantPlayer || wantWeapon || wantHands)
        {
            for (int32_t i = 0; i < count; ++i)
            {
                CBaseSceneData* d = &pData[i];
                const char* model = GetModelPath(d);
                if (!model) continue;

                switch (ClassifyByModelPath(model))
                {
                case ChamsKind::Player:
                    if (wantPlayer)
                        ApplyOverride(d, g_pPlayerMaterial, Globals::chams_player_visible_color);
                    break;
                case ChamsKind::Weapon:
                    if (wantWeapon)
                        ApplyOverride(d, g_pWeaponMaterial, Globals::chams_weapon_color);
                    break;
                case ChamsKind::Hands:
                    if (wantHands)
                        ApplyOverride(d, g_pHandsMaterial, Globals::chams_hands_color);
                    break;
                default:
                    break;
                }
            }
        }

        return g_pOriginalRenderObjects(thisPtr, a2, pData, count, a5, a6, a7);
    }
}

namespace Chams
{
    bool Init()
    {
        // We resolve the SDK-side bits first (CreateMaterial / LoadKV3).
        // If they are missing the hook stays uninstalled - chams will
        // simply do nothing instead of installing a hook with no
        // material to apply.
        if (!MaterialSystem::Init())
            return false;

        // scenesystem.dll is loaded by the engine very early. If it is not
        // mapped at injection time, we are too early - bail and let
        // OnNewFrame retry on a future call.
        if (!GetModuleHandleA("scenesystem.dll"))
            return false;

        uintptr_t target = Memory::PatternScan("scenesystem.dll", kRenderObjectsPattern);
        if (!target)
            return false;

        g_pHookTarget = reinterpret_cast<void*>(target);

        if (MH_CreateHook(g_pHookTarget,
                          reinterpret_cast<void*>(&hkRenderObjects),
                          reinterpret_cast<void**>(&g_pOriginalRenderObjects)) != MH_OK)
            return false;

        if (MH_EnableHook(g_pHookTarget) != MH_OK)
            return false;

        g_bHookEnabled.store(true, std::memory_order_release);
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

        // Materials are owned by the engine - we just drop our handles.
        g_pPlayerMaterial = nullptr;
        g_pWeaponMaterial = nullptr;
        g_pHandsMaterial  = nullptr;
        g_pOriginalRenderObjects = nullptr;
        g_bMaterialsRequested = false;
    }

    void OnNewFrame()
    {
        // Retry hook install once per frame until scenesystem.dll is loaded
        // and the pattern resolves. This handles the case where the cheat is
        // injected into the launcher / loading screen before scenesystem
        // is mapped.
        if (!g_bHookEnabled.load(std::memory_order_acquire))
        {
            (void)Init();
            // Don't fall through to material build - if Init just succeeded
            // we still have a fresh frame to settle. Build on next tick.
            return;
        }

        if (!g_bMaterialsRequested)
        {
            g_bMaterialsRequested = true;

            // Build all three lazily. If any fails (returns nullptr), the
            // detour just won't override that category. No retry - the
            // failure is almost always due to a bad KV3 string, not a
            // transient engine state.
            g_pPlayerMaterial = MaterialSystem::CreateChamsMaterial("fccp/chams_player", kChamsKV3);
            g_pWeaponMaterial = MaterialSystem::CreateChamsMaterial("fccp/chams_weapon", kChamsKV3);
            g_pHandsMaterial  = MaterialSystem::CreateChamsMaterial("fccp/chams_hands",  kChamsKV3);
        }
    }
}
