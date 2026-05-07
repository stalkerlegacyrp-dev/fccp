#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "RCS.h"
#include "../../../sdk/utils/Globals.h"
#include "../../../sdk/utils/Utils.h"
#include "../../../sdk/entity/EntityManager.h"

#include <Windows.h>
#include <cmath>
#include <algorithm>

namespace
{
    Vector g_lastPunch{};
    int    g_lastShots = 0;

    inline float Vfov_Deg(const float* m)
    {
        float yrow = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
        if (yrow < 1e-4f) return 73.74f;
        return 2.f * RAD2DEG(std::atan(1.f / yrow));
    }
}

void RCS::Run()
{
    if (!Globals::rcs_enabled)
    {
        g_lastPunch = Vector{};
        g_lastShots = 0;
        return;
    }

    auto& em = EntityManager::Get();
    C_CSPlayerPawn* local = em.GetLocalPawn();
    if (!local || !local->IsAlive())
    {
        g_lastPunch = Vector{};
        g_lastShots = 0;
        return;
    }

    int    shots = local->m_iShotsFired();
    Vector punch = local->m_aimPunchAngle();

    if (shots <= g_lastShots || shots <= 1)
    {
        // Player is not actively firing — refresh the baseline so the
        // first compensation tick after firing starts is smooth.
        g_lastPunch = punch;
        g_lastShots = shots;
        return;
    }

    Vector delta = punch - g_lastPunch;
    g_lastPunch = punch;
    g_lastShots = shots;

    float reduction = std::clamp(Globals::rcs_reduction, 0.f, 1.f);
    float calib = Globals::rcs_calibration > 0.f ? Globals::rcs_calibration : 1.f;

    // CS2 displays aim_punch * 2 visually — compensate for the displayed
    // displacement, scaled by reduction * calibration.
    float h = (float)Globals::ScreenHeight;
    float vfov = Vfov_Deg(Globals::ViewMatrix);
    float px_per_deg = (h > 1.f ? h / vfov : 10.f);

    // delta.x = pitch (positive = look up), delta.y = yaw (positive = right).
    // Mouse: positive dy → camera pitches down. positive dx → camera yaws right.
    float mdy_deg = delta.x * 2.f * reduction * calib;
    float mdx_deg = -delta.y * 2.f * reduction * calib;

    LONG mdx = (LONG)std::lround(mdx_deg * px_per_deg);
    LONG mdy = (LONG)std::lround(mdy_deg * px_per_deg);
    if (mdx == 0 && mdy == 0) return;

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = mdx;
    in.mi.dy = mdy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(in));
}
