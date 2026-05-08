#pragma once
#include <cstdint>

// Source2 entity handle. Encodes (entry index | serial number).
//
// Layout:
//   bits  0..14   - entry index (15 bits, max 32767)
//   bits 15..30   - serial number (16 bits)
//   bit  31       - invalid sentinel (handle == 0xFFFFFFFF)
//
// CBaseSceneObject stores the owning entity in the same packed form
// at offset 0xB8 (m_hOwner). Once we have a CBaseEntity*, we look it
// up via the entity list to filter chams targets.
class CBaseHandle
{
public:
    static constexpr uint32_t INVALID_HANDLE = 0xFFFFFFFFu;

    uint32_t m_value;

    [[nodiscard]] bool IsValid() const { return m_value != INVALID_HANDLE; }
    [[nodiscard]] int  GetEntryIndex() const { return (int)(m_value & 0x7FFFu); }
    [[nodiscard]] int  GetSerialNumber() const { return (int)((m_value >> 15) & 0xFFFFu); }
};
static_assert(sizeof(CBaseHandle) == 4, "CBaseHandle must be exactly 4 bytes");
