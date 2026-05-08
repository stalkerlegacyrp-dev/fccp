#pragma once
#include <cstdint>

// Forward-declared opaque type. The engine implementation of CKeyValues3
// is much larger (~ 0x40 bytes plus heap-allocated child storage), but
// we never construct or read it from C++ - we only allocate the outer
// shell on the stack, hand it to LoadKV3 (exported by tier0.dll) which
// fully populates it, then pass that pointer to CMaterialSystem2::CreateMaterial.
//
// The actual size of the outer object varies between branches. 0x40 is
// the historically-used safe upper bound from public CS2 SDKs.
struct CKeyValues3
{
    uint8_t  m_pad[0x40];
};

// KV3 binary format identifier (encoding/format GUID pair).
//
// LoadKV3 takes a KV3ID_t to know which schema to interpret.
// "generic" (no specific schema) is what materials use.
//
// Values are taken from the public KV3 spec (see ValveSoftware's open
// docs and public reverse-engineering of source2 tier0.dll). They have
// been stable across CS2 releases since the engine moved to KV3.
struct KV3ID_t
{
    const char* m_name;
    uint64_t    m_id1;
    uint64_t    m_id2;
};

// Generic / generic - used by .vmat KV3 materials.
inline constexpr KV3ID_t k_KV3Format_Generic = {
    "generic",
    0x469806E97412167CULL,
    0xE73790B53EE6F2AFULL,
};
