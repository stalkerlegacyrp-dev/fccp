#pragma once
#include <cstdint>

// Thin wrapper around CMaterialSystem2::CreateMaterial. The function we
// actually call is found by pattern at runtime (the symbol is not exported).
// We pair it with LoadKV3 (exported by tier0.dll under a stable mangled name)
// to push a KV3 .vmat blob through the engine and get back an opaque
// material handle, suitable to drop into CBaseSceneData::m_pMaterial.
namespace MaterialSystem
{
    // Detailed bringup state for diagnostics. Populated by Init().
    struct Status
    {
        bool      mod_materialsystem2 = false;
        bool      mod_tier0           = false;
        bool      createinterface_ok  = false;
        bool      singleton_ok        = false;
        bool      loadkv3_ok          = false;
        bool      createmat_ok        = false;
        uintptr_t createmat_addr      = 0;
        int       createmat_variant   = -1;
        // Mangled export name LoadKV3 was found under (NUL-terminated copy).
        char      loadkv3_export[128] = { 0 };
        bool      ready               = false;
    };

    bool         Init();
    bool         IsReady();
    const Status& GetStatus();

    // Build a chams material from a textual KV3 vmat. Returns nullptr on failure.
    void* CreateChamsMaterial(const char* name, const char* kv3Source);
}
