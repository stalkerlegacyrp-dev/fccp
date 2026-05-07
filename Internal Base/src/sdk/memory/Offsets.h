#pragma once
#include <cstdint>

namespace Offsets
{
    // cs2-dumper build 14159 (2026-05-07)

    // -> offsets.h on a2x dumper
    constexpr uintptr_t dwEntityList = 0x24D1DF0;
    constexpr uintptr_t dwLocalPlayerPawn = 0x2057720;
    constexpr uintptr_t dwViewMatrix = 0x2331B30;


    // -> client.dll.hpp on a2x dumper -> C_BaseEntity 
    constexpr uintptr_t m_iHealth = 0x34C;
    constexpr uintptr_t m_iTeamNum = 0x3EB;
    constexpr uintptr_t m_vOldOrigin = 0x1390;
    constexpr uintptr_t m_pGameSceneNode = 0x330;

    // -> CbaseModelEntity
    constexpr uintptr_t m_vecViewOffset = 0xE70;

    // -> C_CSPlayerPawn
    constexpr uintptr_t m_iShotsFired = 0x1C64;
    constexpr uintptr_t m_pAimPunchServices = 0x1490;

    // -> CCSPlayer_AimPunchServices
    constexpr uintptr_t m_aimPunchAngle = 0x50;

    // -> C_CSPlayerController
    constexpr uintptr_t m_iszPlayerName = 0x6F0;
    constexpr uintptr_t m_hPlayerPawn = 0x904;
    constexpr uintptr_t m_bPawnIsAlive = 0x90C;

    // -> CBodyComponentSkeletonInstance
    constexpr uintptr_t m_modelState = 0x150;

    // -> CObserverServices
    constexpr std::ptrdiff_t m_pObserverServices = 0x11F8;
    constexpr std::ptrdiff_t m_hObserverTarget = 0x4C;
    constexpr std::ptrdiff_t m_iObserverMode = 0x48;

}
