#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Triggerbot.h"
#include "../../../sdk/utils/Globals.h"
#include "../../../sdk/utils/Utils.h"
#include "../../../sdk/entity/EntityManager.h"
#include "../../../../ext/imgui/imgui.h"

#include <Windows.h>
#include <chrono>
#include <cmath>
#include <algorithm>

namespace
{
    using clock_t = std::chrono::steady_clock;

    bool                       g_pendingFire = false;
    clock_t::time_point        g_fireAt = {};
    clock_t::time_point        g_lastShot = {};

    inline float Vfov_Deg(const float* m)
    {
        float yrow = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
        if (yrow < 1e-4f) return 73.74f;
        return 2.f * RAD2DEG(std::atan(1.f / yrow));
    }

    inline bool BindHeld(int vk)
    {
        if (vk == 0) return true;
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }

    bool CrosshairOnEnemy(float fov_deg)
    {
        auto& em = EntityManager::Get();
        C_CSPlayerPawn* local = em.GetLocalPawn();
        if (!local || !local->IsAlive())
            return false;

        float w = (float)Globals::ScreenWidth;
        float h = (float)Globals::ScreenHeight;
        if (w < 1.f || h < 1.f) return false;

        float vpX, vpY, vpW, vpH;
        Utils::ComputeViewportRect(Globals::ViewMatrix, w, h, vpX, vpY, vpW, vpH);

        const float cx = w * 0.5f, cy = h * 0.5f;
        const float vfov = Vfov_Deg(Globals::ViewMatrix);
        const float px_per_deg = vpH / vfov;
        const float maxR = (fov_deg > 0.f ? fov_deg : 1.f) * px_per_deg;

        const bool correct = Globals::esp_aspect_correct;
        auto W2S = [&](const Vector& world, Vector& out) -> bool {
            return correct
                ? Utils::WorldToScreenViewport(world, out, Globals::ViewMatrix, vpX, vpY, vpW, vpH)
                : Utils::WorldToScreen(world, out, Globals::ViewMatrix, w, h);
        };

        for (const auto& ent : em.GetEntities())
        {
            if (!ent.pawn || !ent.pawn->IsAlive() || !ent.isEnemy)
                continue;

            Vector head = Utils::GetBonePos(ent.pawn, BoneID::Head);
            Vector pelvis = Utils::GetBonePos(ent.pawn, BoneID::Pelvis);
            if (head.IsZero() || pelvis.IsZero()) continue;

            Vector sH, sP;
            if (!W2S(head, sH)) continue;
            if (!W2S(pelvis, sP)) continue;

            float boxH = sP.y - sH.y;
            if (boxH < 5.f) continue;
            float halfW = boxH * 0.25f;
            float minX = std::min(sH.x, sP.x) - halfW;
            float maxX = std::max(sH.x, sP.x) + halfW;
            float minY = sH.y - 4.f;
            float maxY = sP.y + 2.f;

            if (cx >= minX && cx <= maxX && cy >= minY && cy <= maxY)
                return true;

            float dx = sH.x - cx, dy = sH.y - cy;
            if (std::sqrt(dx * dx + dy * dy) <= maxR) return true;
        }
        return false;
    }
}

void Triggerbot::Run()
{
    if (!Globals::trigger_enabled)
    {
        g_pendingFire = false;
        return;
    }
    if (!BindHeld(Globals::trigger_bind))
    {
        g_pendingFire = false;
        return;
    }

    const auto now = clock_t::now();

    if (g_pendingFire)
    {
        if (now < g_fireAt) return;

        INPUT down{};
        down.type = INPUT_MOUSE;
        down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &down, sizeof(down));

        Sleep(10);

        INPUT up{};
        up.type = INPUT_MOUSE;
        up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &up, sizeof(up));

        g_pendingFire = false;
        g_lastShot = clock_t::now();
        return;
    }

    if (!CrosshairOnEnemy(Globals::trigger_fov))
        return;

    g_pendingFire = true;
    int delay_ms = (int)Globals::trigger_shot_delay;
    if (delay_ms < 0) delay_ms = 0;
    g_fireAt = now + std::chrono::milliseconds(delay_ms);
}

void Triggerbot::DrawFov()
{
    if (!Globals::trigger_draw_fov || !Globals::trigger_enabled)
        return;

    float w = (float)Globals::ScreenWidth;
    float h = (float)Globals::ScreenHeight;
    if (w < 1.f || h < 1.f) return;

    float vpX, vpY, vpW, vpH;
    Utils::ComputeViewportRect(Globals::ViewMatrix, w, h, vpX, vpY, vpW, vpH);

    float vfov = Vfov_Deg(Globals::ViewMatrix);
    float r = Globals::trigger_fov * (vpH / vfov);
    if (r < 1.f) return;

    ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(
        Globals::trigger_fov_color[0], Globals::trigger_fov_color[1],
        Globals::trigger_fov_color[2], Globals::trigger_fov_color[3]));

    ImGui::GetBackgroundDrawList()->AddCircle(
        { w * 0.5f, h * 0.5f }, r, col, 64, 1.5f);
}
