#pragma once
#include <cstdint>

// Chams - material-based replacement.
//
// Strategy:
//   1. Find CAnimatableSceneObjectDesc::RenderObjects in scenesystem.dll
//      (or one of its known fallback modules) via pattern scan.
//   2. Detour it with MinHook.
//   3. For every CBaseSceneData passed in, classify by model-path string
//      (player / weapon / hands), and if the matching toggle is on,
//      replace the material pointer + RGBA tint inline before calling
//      the original. The engine renders the scene with the swapped
//      material, then restores nothing (we own the per-frame array).
//
// Init() pattern-scans + installs the detour. OnNewFrame() is a cheap
// per-frame tick where we lazily build chams materials the first time
// the user enables a category. Shutdown() un-hooks.
namespace Chams
{
    bool Init();
    void Shutdown();
    void OnNewFrame();

    // Diagnostics surface so the menu can show *exactly* which step of
    // the bringup pipeline failed without forcing the user to hook up
    // a debugger / DbgView. Every field is set at the moment its step
    // executes; consumers should read the snapshot via GetDiag().
    struct Diag
    {
        // Module presence (GetModuleHandleA succeeded)
        bool mod_materialsystem2 = false;
        bool mod_tier0           = false;
        bool mod_scenesystem     = false;
        bool mod_client          = false;
        bool mod_rendersystemdx11 = false;

        // Resolution steps
        bool createinterface_export   = false;
        bool matsys_singleton         = false;
        bool tier0_loadkv3            = false;
        bool creatematerial_pattern   = false;
        bool renderobjects_pattern    = false;

        // Which module the RenderObjects pattern eventually hit
        // (string copy, lower-case, max 31 chars + NUL).
        char renderobjects_module[32] = { 0 };
        // Index of the pattern variant that matched (-1 = none).
        int  renderobjects_variant = -1;

        // Hook
        bool hook_created   = false;
        bool hook_enabled   = false;

        // Materials
        bool material_player = false;
        bool material_weapon = false;
        bool material_hands  = false;

        // Live counters (atomically updated from the hot path).
        uint64_t calls_total = 0;
        uint64_t calls_overridden = 0;
        uint32_t last_classified_player = 0;
        uint32_t last_classified_weapon = 0;
        uint32_t last_classified_hands  = 0;

        // Last successful target address (for log/debug) and last frame
        // where any of the three categories matched a model path.
        uintptr_t hook_target_addr = 0;
    };

    const Diag& GetDiag();
}
