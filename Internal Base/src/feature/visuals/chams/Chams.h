#pragma once
#include <cstdint>

// Chams - material-based replacement.
//
// Strategy:
//   1. Probe(): pattern-scan CAnimatableSceneObjectDesc::RenderObjects
//      and verify materialsystem2 SDK bringup, but DO NOT install any
//      hook. Probe() is safe to call every frame; it only fills the
//      diagnostic struct and never alters game memory.
//   2. TryInstall(): install the MinHook detour on the address we found
//      in Probe(). This is the dangerous step (we may have matched the
//      wrong function on a new build) so it is gated behind an explicit
//      "Install hook" button in the diagnostic card.
//   3. The detour body classifies CBaseSceneData entries by model-path
//      string and rewrites material pointer + RGBA tint inline. The
//      whole body is wrapped in __try/__except so that a single bad
//      call cleanly disables the hook instead of crashing the game.
//   4. Uninstall(): drop the detour again.
namespace Chams
{
    // Cheap, idempotent. Updates Diag.* fields with what we can see in
    // memory right now. Never installs any hook. Safe from any thread.
    void Probe();

    // Install the MinHook detour. Returns true on success, false on any
    // failure (no hook will be active in that case). Calls Probe() first.
    bool TryInstall();

    // Drop the detour. Idempotent.
    void Uninstall();

    // True iff the detour is currently installed and enabled.
    bool IsInstalled();

    // Per-frame hook for lazy material creation. Does NOT auto-install
    // the detour (the user must press the button) but will lazily build
    // the chams materials the first frame after the hook becomes active.
    void OnNewFrame();

    // Compatibility wrappers for existing call sites in Hooks.cpp and
    // similar; Init() is equivalent to Probe(), Shutdown() to
    // Uninstall(). Neither installs the detour automatically.
    bool Init();
    void Shutdown();

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

        // Hook lifecycle
        bool hook_install_attempted = false;
        bool hook_created           = false;
        bool hook_enabled           = false;

        // Materials
        bool material_player = false;
        bool material_weapon = false;
        bool material_hands  = false;

        // Per-category creation attempts. Set to true the first time we
        // tried to build that material, even if it crashed or returned
        // null. Used so we don't keep re-firing LoadKV3/CreateMaterial
        // every frame on a permanently-broken pattern.
        bool material_player_attempted = false;
        bool material_weapon_attempted = false;
        bool material_hands_attempted  = false;

        // Live counters (atomically updated from the hot path).
        uint64_t calls_total          = 0;
        uint64_t calls_overridden     = 0;
        uint64_t detour_seh_catches   = 0;
        uint64_t install_seh_catches  = 0;
        uint64_t material_seh_catches = 0;
        uint32_t last_classified_player = 0;
        uint32_t last_classified_weapon = 0;
        uint32_t last_classified_hands  = 0;

        // Last successful target address (for log/debug).
        uintptr_t hook_target_addr = 0;
    };

    const Diag& GetDiag();
}
