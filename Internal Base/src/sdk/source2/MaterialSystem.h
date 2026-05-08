#pragma once
#include <cstdint>

// Thin wrapper around CMaterialSystem2::CreateMaterial. The function we
// actually call is found by pattern at runtime (the symbol is not exported).
// We pair it with LoadKV3 (exported by tier0.dll under a stable mangled name)
// to push a KV3 .vmat blob through the engine and get back an opaque
// material handle, suitable to drop into CBaseSceneData::m_pMaterial.
namespace MaterialSystem
{
    // Resolves CreateMaterial via pattern scan in materialsystem2.dll and
    // GetProcAddress("?LoadKV3@@...") in tier0.dll. Returns false if either
    // fails - chams will then no-op cleanly instead of crashing.
    bool Init();

    // True after Init succeeded and both targets resolved.
    bool IsReady();

    // Build a chams material from a textual KV3 vmat. The string must contain
    // the full KV3 header (encoding/format GUIDs) plus the body. The returned
    // handle is opaque - just store the pointer and assign it to
    // CBaseSceneData::m_pMaterial (and m_pMaterial2) inside the hook.
    //
    // Returns nullptr on failure. Safe to call even if Init() failed.
    void* CreateChamsMaterial(const char* name, const char* kv3Source);
}
