#pragma once
#include <cstdint>
#include "CBaseHandle.h"

// ---------------------------------------------------------------------------
// CSceneObject (and its subclasses CAnimatableSceneObject, CLightSceneObject,
// CFireSceneObject, etc.) is the engine-side renderable. The game owns one
// per CBaseModelEntity. When the renderer wants to draw something it
// constructs a CBaseSceneData that points at the scene object plus the
// material to use, and feeds it to CAnimatableSceneObjectDesc::RenderObjects
// (in scenesystem.dll) which is where we install the chams hook.
//
// Only the fields we actually touch are typed; the rest is opaque padding
// extracted from current-build RTTI / reverse-engineered layout.
// ---------------------------------------------------------------------------
class CSceneObject
{
public:
    char         m_pad_0000[0xB8];
    CBaseHandle  m_hOwner;        // 0xB8 - entity handle that owns this scene object
    char         m_pad_00BC[0x180 - 0xBC];
};
static_assert(sizeof(CSceneObject) == 0x180, "CSceneObject layout drift");

// CBaseSceneData is an in-flight render request. The engine builds an
// array of these per frame and passes (array, count) to RenderObjects.
// We mutate m_pMaterial (and the alternate m_pMaterial2) plus the per-object
// RGBA tint to install chams.
//
// Layout (size 0x68) matches public CS2 SDKs; verified against the
// PureLiquid-CS2-External and CS2-External-Chams reference implementations.
class CBaseSceneData
{
public:
    char     m_pad_0000[0x18];   // 0x00..0x18 - opaque (model impl chain lives here)
    CSceneObject* m_pSceneObject;// 0x18
    void*    m_pMaterial;        // 0x20 - primary material (the one we override)
    void*    m_pMaterial2;       // 0x28 - alt slot used by some passes
    char     m_pad_0030[0x10];   // 0x30..0x40
    uint8_t  m_r;                // 0x40 - per-object RGBA tint
    uint8_t  m_g;                // 0x41
    uint8_t  m_b;                // 0x42
    uint8_t  m_a;                // 0x43
    char     m_pad_0044[0x68 - 0x44];
};
static_assert(sizeof(CBaseSceneData) == 0x68, "CBaseSceneData layout drift");

// Hook function signature.
//
// Original prototype (reversed from scenesystem.dll RTTI):
//   void* CAnimatableSceneObjectDesc::RenderObjects(
//       void* this,
//       void* a2,                    // render context / output buffer
//       CBaseSceneData* pData,       // pData[0..count-1]
//       int32_t count,
//       void* a5, void* a6, void* a7);
//
// __fastcall is the only sensible calling convention on x64 MSVC; the
// compiler will pass the first 4 args in RCX/RDX/R8/R9 and the rest on the
// stack. We must match this exactly, otherwise the trampoline returns into
// a corrupt frame.
using TRenderObjects = void* (__fastcall*)(
    void* /*this*/,
    void* /*a2*/,
    CBaseSceneData* /*pData*/,
    int32_t /*count*/,
    void* /*a5*/,
    void* /*a6*/,
    void* /*a7*/
    );
