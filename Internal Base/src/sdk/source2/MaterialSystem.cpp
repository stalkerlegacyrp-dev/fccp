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
#include <cstdio>

#include "../memory/PatternScan.h"

namespace
{
    // tier0!LoadKV3 has been exported under several slightly different
    // manglings across CS2 builds (CKeyValues3 vs KeyValues3, mostly).
    // We resolve it by enumerating tier0's export directory and grabbing
    // the first export that matches the prefix "?LoadKV3@".
    //
    // Function shape: bool LoadKV3(CKeyValues3* out,
    //                              CUtlString*  errorOut,    // we pass nullptr
    //                              const char*  kv3Source,
    //                              const KV3ID_t& format,
    //                              const char*  context,     // we pass nullptr
    //                              unsigned     flags);      // we pass 0
    using TLoadKV3 = bool(__fastcall*)(CKeyValues3*, void*, const char*, const KV3ID_t&, const char*, unsigned);

    // CMaterialSystem2::CreateMaterial.
    using TCreateMaterial = void(__fastcall*)(void*, void**, const char*, CKeyValues3*, int, int);

    // CreateInterface wrapper.
    using TCreateInterface = void* (__cdecl*)(const char*, int*);

    // -----------------------------------------------------------------
    // Multiple patterns to try for CreateMaterial. The current build's
    // prologue is unknown without tier0.dll / fresh dumps, so we keep a
    // small ordered list of likely candidates and report which one (if
    // any) matched.
    // -----------------------------------------------------------------
    struct PatternCandidate
    {
        const char* pat;
        const char* note;
    };

    constexpr PatternCandidate kCreateMaterialPatterns[] = {
        { "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 41 56 48 81 EC ?? ?? ?? ?? 48 8B 05",
          "PureLiquid 2024" },
        { "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 41 56 41 57 48 81 EC",
          "alt regs" },
        { "40 53 56 57 41 54 41 55 41 56 41 57 48 81 EC",
          "fully-saved variant" },
    };

    void*           g_pMaterialSystem = nullptr;
    TLoadKV3        g_pLoadKV3 = nullptr;
    TCreateMaterial g_pCreateMaterial = nullptr;
    MaterialSystem::Status g_Status{};

    // Walk a module's export directory and return the first export name
    // whose ASCII prefix matches `prefix`. Returns nullptr if none.
    // The returned pointer points into the module image - safe to read
    // for the lifetime of the module.
    const char* FindExportByPrefix(HMODULE mod, const char* prefix, FARPROC* outAddr)
    {
        if (!mod || !prefix) return nullptr;
        auto base = reinterpret_cast<uint8_t*>(mod);

        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

        auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

        auto& expDirEntry = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!expDirEntry.VirtualAddress) return nullptr;

        auto exp = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + expDirEntry.VirtualAddress);
        auto names = reinterpret_cast<DWORD*>(base + exp->AddressOfNames);
        auto ords  = reinterpret_cast<WORD*>(base + exp->AddressOfNameOrdinals);
        auto funcs = reinterpret_cast<DWORD*>(base + exp->AddressOfFunctions);

        size_t prefLen = std::strlen(prefix);

        for (DWORD i = 0; i < exp->NumberOfNames; ++i)
        {
            const char* name = reinterpret_cast<const char*>(base + names[i]);
            if (std::strncmp(name, prefix, prefLen) == 0)
            {
                if (outAddr)
                    *outAddr = reinterpret_cast<FARPROC>(base + funcs[ords[i]]);
                return name;
            }
        }
        return nullptr;
    }
}

bool MaterialSystem::Init()
{
    if (g_Status.ready)
        return true;

    HMODULE matSysMod = GetModuleHandleA("materialsystem2.dll");
    HMODULE tier0Mod  = GetModuleHandleA("tier0.dll");

    g_Status.mod_materialsystem2 = matSysMod != nullptr;
    g_Status.mod_tier0           = tier0Mod  != nullptr;

    if (!matSysMod || !tier0Mod)
        return false;

    auto matCreateIface = reinterpret_cast<TCreateInterface>(
        GetProcAddress(matSysMod, "CreateInterface"));
    g_Status.createinterface_ok = matCreateIface != nullptr;
    if (!matCreateIface)
        return false;

    int rc = 0;
    g_pMaterialSystem = matCreateIface("VMaterialSystem2_001", &rc);
    g_Status.singleton_ok = g_pMaterialSystem != nullptr;
    if (!g_pMaterialSystem)
        return false;

    // Resolve LoadKV3. Try the canonical mangled name first, then fall
    // back to scanning tier0 exports for any "?LoadKV3@" prefix.
    {
        FARPROC kv3Addr = GetProcAddress(tier0Mod,
            "?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEBDAEBUKV3ID_t@@2I@Z");
        if (kv3Addr)
        {
            std::strncpy(g_Status.loadkv3_export,
                "?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEBDAEBUKV3ID_t@@2I@Z",
                sizeof(g_Status.loadkv3_export) - 1);
            g_pLoadKV3 = reinterpret_cast<TLoadKV3>(kv3Addr);
        }
        else
        {
            FARPROC found = nullptr;
            const char* name = FindExportByPrefix(tier0Mod, "?LoadKV3@", &found);
            if (name && found)
            {
                std::strncpy(g_Status.loadkv3_export, name, sizeof(g_Status.loadkv3_export) - 1);
                g_pLoadKV3 = reinterpret_cast<TLoadKV3>(found);
            }
        }
    }
    g_Status.loadkv3_ok = g_pLoadKV3 != nullptr;
    if (!g_pLoadKV3)
        return false;

    // Pattern scan CreateMaterial. Try each candidate in order.
    for (int i = 0; i < (int)(sizeof(kCreateMaterialPatterns) / sizeof(kCreateMaterialPatterns[0])); ++i)
    {
        uintptr_t hit = Memory::PatternScan("materialsystem2.dll", kCreateMaterialPatterns[i].pat);
        if (hit)
        {
            g_pCreateMaterial      = reinterpret_cast<TCreateMaterial>(hit);
            g_Status.createmat_addr    = hit;
            g_Status.createmat_variant = i;
            g_Status.createmat_ok      = true;
            break;
        }
    }
    if (!g_Status.createmat_ok)
        return false;

    g_Status.ready = true;
    return true;
}

bool MaterialSystem::IsReady()
{
    return g_Status.ready;
}

const MaterialSystem::Status& MaterialSystem::GetStatus()
{
    return g_Status;
}

void* MaterialSystem::CreateChamsMaterial(const char* name, const char* kv3Source)
{
    if (!g_Status.ready || !name || !kv3Source)
        return nullptr;

    CKeyValues3 kv{};
    if (!g_pLoadKV3(&kv, nullptr, kv3Source, k_KV3Format_Generic, nullptr, 0u))
        return nullptr;

    void* outHandle = nullptr;
    g_pCreateMaterial(g_pMaterialSystem, &outHandle, name, &kv, 0, 1);
    return outHandle;
}
