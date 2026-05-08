#pragma once

// Chams - material-based replacement.
//
// Strategy:
//   1. Find CAnimatableSceneObjectDesc::RenderObjects in scenesystem.dll
//      via pattern scan.
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

    // Called from Hooks::hkPresent each frame after EntityManager::Update.
    // Used for lazy material creation - we cannot safely call
    // CreateMaterial during DllMain or before the engine has finished
    // bootstrapping its tier0/material singletons.
    void OnNewFrame();
}
