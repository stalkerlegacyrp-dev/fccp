#pragma once
#include <cstdint>
#include <Windows.h>

namespace Globals
{
    // Engine
    inline float ViewMatrix[16] = { 0.f };
    inline int ScreenWidth = 0;
    inline int ScreenHeight = 0;

    // Menu state
    inline int  menu_tab = 0; // 0=Legit Bot, 1=Players, 2=View, 3=Main, 4=Configs

    // ESP
    inline bool esp_enabled = true;
    inline int  esp_bind = VK_F1;

    // When the game's aspect ratio differs from the back buffer (e.g.
    // 4:3 letterboxed in a 16:9 monitor), the rendered image is
    // pillarboxed and ESP would project past the visible viewport.
    // Disable this if you play on stretched 4:3.
    inline bool esp_aspect_correct = true;

    inline bool esp_box = true;
    inline float esp_box_color[4] = { 1.f, 0.f, 0.f, 1.f };
    inline float esp_box_thickness = 1.5f;

    inline bool esp_skeleton = true;
    inline float esp_skeleton_color[4] = { 1.f, 1.f, 1.f, 0.9f };
    inline float esp_skeleton_thickness = 1.8f;

    inline bool esp_name = true;
    inline float esp_name_color[4] = { 1.f, 1.f, 1.f, 1.f };

    inline bool esp_health = true;

    // HUD
    inline bool hud_enemy_counter = true;

    // Aimbot (placeholder logic; settings persisted)
    inline bool aimbot_enabled = false;
    inline int  aimbot_bind = VK_MENU; // ALT
    inline bool aimbot_visibility = false;
    inline bool aimbot_aimlock = false;
    inline bool aimbot_draw_fov = false;
    inline float aimbot_fov_color[4] = { 1.f, 1.f, 1.f, 1.f };
    inline float aimbot_fov = 10.f;
    inline float aimbot_smoothing = 5.f;
    inline int   aimbot_hitbox = 0; // 0=Head,1=Neck,2=Chest,3=Pelvis

    // Triggerbot (placeholder)
    inline bool trigger_enabled = false;
    inline int  trigger_bind = VK_XBUTTON1; // Mouse 4
    inline float trigger_fov = 0.f;
    inline float trigger_shot_delay = 0.f;
    inline bool trigger_draw_fov = false;
    inline float trigger_fov_color[4] = { 1.f, 1.f, 0.f, 1.f };

    // Adaptive Recoil Control (placeholder)
    inline bool rcs_enabled = false;
    inline float rcs_reduction = 0.5f;
    inline float rcs_calibration = 1.f;
}
