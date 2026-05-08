#include "MaterialSystem.h"
#include "KeyValues3.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstring>

#include "../memory/PatternScan.h"

namespace
{
    // tier0.dll exports LoadKV3 under its mangled MSVC name. Stable across
    // CS2 builds (the function has not been renamed in over a year).
    //
    //   bool LoadKV3(CKeyValues3* out,
    //                CUtlString*  errorOut,    // we pass nullptr
    //                const char*  kv3Source,
    //                const KV3ID_t& format,
    //                const char*  context,     // we pass nullptr
    //                unsigned     flags);      // we pass 0
    using TLoadKV3 = bool(__fastcall*)(CKeyValues3*, void*, const char*, const KV3ID_t&, const char*, unsigned);

    // CMaterialSystem2::CreateMaterial.
    //
    // The function is non-virtual and not exported, so we sig-scan it.
    // Its prototype (from public CS2 SDK reverse engineering):
    //
    //   void CreateMaterial(IMaterialSystem2* this,
    //                       void**       outHandle,    // out: opaque CStrongHandle slot
    //                       const char*  name,
    //                       CKeyValues3* kv,
    //                       int          flagsA,       // typically 0
    //                       int          flagsB);      // typically 1
    //
    // The first arg is `this`; we obtain it from the SceneSystem_002 /
    // VMaterialSystem2_001 singleton via CreateInterface.
    using TCreateMaterial = void(__fastcall*)(void*, void**, const char*, CKeyValues3*, int, int);

    // Pattern of the CreateMaterial prologue. Stable across recent builds;
    // taken from PureLiquid-CS2-External (CS2/Include/CS2/Patterns.h).
    // 4D 89 4C 24 ?? push regs / 4C 89 44 24 ?? movs followed by sub rsp, ?? ?? ?? ??
    constexpr const char* kCreateMaterialPattern =
        "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 41 56 48 81 EC ?? ?? ?? ?? 48 8B 05";

    // CreateInterface wrapper for the materialsystem2 singleton. Each Source2
    // module exports CreateInterface(name, returnCode) - we ask for
    // VMaterialSystem2_001.
    using TCreateInterface = void* (__cdecl*)(const char*, int*);

    void*           g_pMaterialSystem = nullptr;
    TLoadKV3        g_pLoadKV3 = nullptr;
    TCreateMaterial g_pCreateMaterial = nullptr;
    bool            g_bReady = false;
}

bool MaterialSystem::Init()
{
    if (g_bReady)
        return true;

    HMODULE matSysMod = GetModuleHandleA("materialsystem2.dll");
    HMODULE tier0Mod  = GetModuleHandleA("tier0.dll");

    if (!matSysMod || !tier0Mod)
        return false;

    auto matCreateIface = reinterpret_cast<TCreateInterface>(
        GetProcAddress(matSysMod, "CreateInterface"));
    if (!matCreateIface)
        return false;

    int rc = 0;
    g_pMaterialSystem = matCreateIface("VMaterialSystem2_001", &rc);
    if (!g_pMaterialSystem)
        return false;

    g_pLoadKV3 = reinterpret_cast<TLoadKV3>(
        GetProcAddress(tier0Mod,
            // ?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEBDAEBUKV3ID_t@@2I@Z
            "?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEBDAEBUKV3ID_t@@2I@Z"));
    if (!g_pLoadKV3)
        return false;

    uintptr_t createMat = Memory::PatternScan("materialsystem2.dll", kCreateMaterialPattern);
    if (!createMat)
        return false;

    g_pCreateMaterial = reinterpret_cast<TCreateMaterial>(createMat);
    g_bReady = true;
    return true;
}

bool MaterialSystem::IsReady()
{
    return g_bReady;
}

void* MaterialSystem::CreateChamsMaterial(const char* name, const char* kv3Source)
{
    if (!g_bReady || !name || !kv3Source)
        return nullptr;

    // LoadKV3 fully populates the outer KV3 object. We zero it first so any
    // residual stack state doesn't trip the parser's "is initialised" checks.
    CKeyValues3 kv{};
    if (!g_pLoadKV3(&kv, nullptr, kv3Source, k_KV3Format_Generic, nullptr, 0u))
        return nullptr;

    void* outHandle = nullptr;
    g_pCreateMaterial(g_pMaterialSystem, &outHandle, name, &kv, 0, 1);
    return outHandle;
}
