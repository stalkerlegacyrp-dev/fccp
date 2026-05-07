#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Aimbot.h"
#include "../../../sdk/utils/Globals.h"
#include "../../../sdk/utils/Utils.h"
#include "../../../sdk/entity/EntityManager.h"
#include "../../../../ext/imgui/imgui.h"

#include <Windows.h>
#include <cmath>
#include <algorithm>

namespace
{
    // Sticky lock target between frames, for the "Aimlock" toggle.
    C_CSPlayerController* g_lockedController = nullptr;

    inline float Vfov_Deg(const float* m)
    {
        // |y row| of the view*proj matrix == 1 / tan(vfov/2) for a rigid
        // (rotation-only) view, regardless of camera orientation.
        float yrow = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
        if (yrow < 1e-4f) return 73.74f; // sane fallback (vfov for hfov=90 on 16:9)
        return 2.f * RAD2DEG(std::atan(1.f / yrow));
    }

    inline BoneID HitboxBone(int hb)
    {
        switch (hb)
        {
        case 1: return BoneID::Neck;
        case 2: return BoneID::Spine;
        case 3: return BoneID::Pelvis;
        default: return BoneID::Head;
        }
    }

    inline bool BindHeld(int vk)
    {
        if (vk == 0) return true; // "Always"
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
}

void Aimbot::DrawFov()
{
    if (!Globals::aimbot_draw_fov || !Globals::aimbot_enabled)
        return;

    float w = (float)Globals::ScreenWidth;
    float h = (float)Globals::ScreenHeight;
    if (w < 1.f || h < 1.f)
        return;

    float vpX, vpY, vpW, vpH;
    Utils::ComputeViewportRect(Globals::ViewMatrix, w, h, vpX, vpY, vpW, vpH);

    float vfov = Vfov_Deg(Globals::ViewMatrix);
    float r = Globals::aimbot_fov * (vpH / vfov);
    if (r < 1.f)
        return;

    ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(
        Globals::aimbot_fov_color[0], Globals::aimbot_fov_color[1],
        Globals::aimbot_fov_color[2], Globals::aimbot_fov_color[3]));

    ImGui::GetBackgroundDrawList()->AddCircle(
        { w * 0.5f, h * 0.5f }, r, col, 64, 1.5f);
}

void Aimbot::Run()
{
    if (!Globals::aimbot_enabled)
    {
        g_lockedController = nullptr;
        return;
    }
    if (!BindHeld(Globals::aimbot_bind))
    {
        g_lockedController = nullptr;
        return;
    }

    auto& em = EntityManager::Get();
    C_CSPlayerPawn* local = em.GetLocalPawn();
    if (!local || !local->IsAlive())
    {
        g_lockedController = nullptr;
        return;
    }

    float w = (float)Globals::ScreenWidth;
    float h = (float)Globals::ScreenHeight;
    if (w < 1.f || h < 1.f) return;

    float vpX, vpY, vpW, vpH;
    Utils::ComputeViewportRect(Globals::ViewMatrix, w, h, vpX, vpY, vpW, vpH);

    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    const float vfov = Vfov_Deg(Globals::ViewMatrix);
    const float px_per_deg = vpH / vfov;
    const float fov_radius_px = Globals::aimbot_fov * px_per_deg;

    const BoneID bone = HitboxBone(Globals::aimbot_hitbox);

    auto project = [&](C_CSPlayerPawn* pawn, Vector& out_screen) -> bool
    {
        Vector hit = Utils::GetBonePos(pawn, bone);
        if (hit.IsZero()) return false;
        return Globals::esp_aspect_correct
            ? Utils::WorldToScreenViewport(hit, out_screen, Globals::ViewMatrix, vpX, vpY, vpW, vpH)
            : Utils::WorldToScreen(hit, out_screen, Globals::ViewMatrix, w, h);
    };

    Entity_t best{};
    bool have_best = false;
    float best_dist = fov_radius_px + 1.f;
    Vector best_screen{};

    // Aimlock: keep tracking the previously locked target without an FOV
    // gate so the bot doesn't drop the target if they leave the circle.
    if (Globals::aimbot_aimlock && g_lockedController)
    {
        for (const auto& ent : em.GetEntities())
        {
            if (ent.controller != g_lockedController) continue;
            if (!ent.pawn || !ent.pawn->IsAlive() || !ent.isEnemy) break;

            Vector scr;
            if (!project(ent.pawn, scr)) break;

            best = ent;
            best_screen = scr;
            have_best = true;

            float dx = scr.x - cx, dy = scr.y - cy;
            best_dist = std::sqrt(dx * dx + dy * dy);
            break;
        }
        if (!have_best) g_lockedController = nullptr;
    }

    if (!have_best)
    {
        for (const auto& ent : em.GetEntities())
        {
            if (!ent.pawn || !ent.pawn->IsAlive() || !ent.isEnemy) continue;

            Vector scr;
            if (!project(ent.pawn, scr)) continue;

            float dx = scr.x - cx, dy = scr.y - cy;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d > fov_radius_px) continue;
            if (d >= best_dist) continue;

            // Visibility check (approximate): the SDK doesn't expose an
            // engine TraceLine yet, so the toggle currently only filters
            // out pawns whose bones are not streamed (off-screen / dorm).
            // Real LOS is a follow-up task.
            if (Globals::aimbot_visibility)
            {
                Vector ignore;
                if (!project(ent.pawn, ignore)) continue;
            }

            best = ent;
            best_screen = scr;
            best_dist = d;
            have_best = true;
        }
    }

    if (!have_best)
    {
        g_lockedController = nullptr;
        return;
    }

    if (Globals::aimbot_aimlock)
        g_lockedController = best.controller;

    float dx = best_screen.x - cx;
    float dy = best_screen.y - cy;

    float smooth = Globals::aimbot_smoothing > 1.f ? Globals::aimbot_smoothing : 1.f;
    LONG mdx = (LONG)std::lround(dx / smooth);
    LONG mdy = (LONG)std::lround(dy / smooth);
    if (mdx == 0 && mdy == 0) return;

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = mdx;
    in.mi.dy = mdy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(in));
}
