#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Menu.h"
#include "../../ext/imgui/imgui.h"
#include "../../ext/iconfont/IconsFontAwesome6.h"
#include "../sdk/utils/Globals.h"
#include "../feature/visuals/chams/Chams.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>

// Local helpers (avoid <algorithm> std::max/min/clamp because Windows.h
// pollutes the global scope with min/max macros if NOMINMAX is missed by
// any earlier translation unit).
static inline float MaxF(float a, float b) { return a < b ? b : a; }
static inline float ClampF(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
namespace Theme
{
    constexpr ImU32 Bg = IM_COL32(24, 24, 26, 255);
    constexpr ImU32 BgDim = IM_COL32(20, 20, 22, 255);
    constexpr ImU32 Card = IM_COL32(28, 28, 30, 255);
    constexpr ImU32 Border = IM_COL32(45, 45, 50, 255);
    constexpr ImU32 BorderSoft = IM_COL32(38, 38, 42, 255);
    constexpr ImU32 ItemHover = IM_COL32(36, 36, 40, 255);
    constexpr ImU32 ItemActive = IM_COL32(44, 44, 48, 255);
    constexpr ImU32 Text = IM_COL32(225, 225, 230, 255);
    constexpr ImU32 TextDim = IM_COL32(150, 150, 155, 255);
    constexpr ImU32 TextHeader = IM_COL32(125, 125, 130, 255);
    constexpr ImU32 TextDisabled = IM_COL32(95, 95, 100, 255);
    constexpr ImU32 Accent = IM_COL32(217, 168, 184, 255);   // muted pink
    constexpr ImU32 AccentHi = IM_COL32(232, 182, 198, 255);
    constexpr ImU32 SliderBg = IM_COL32(38, 38, 42, 255);
    constexpr ImU32 ChipBg = IM_COL32(38, 38, 42, 255);
}

// ---------------------------------------------------------------------------
// Bind helpers
// ---------------------------------------------------------------------------
static const char* KeyName(int vk)
{
    switch (vk)
    {
    case 0:           return "None";
    case VK_LBUTTON:  return "Mouse 1";
    case VK_RBUTTON:  return "Mouse 2";
    case VK_MBUTTON:  return "Mouse 3";
    case VK_XBUTTON1: return "Mouse 4";
    case VK_XBUTTON2: return "Mouse 5";
    case VK_SHIFT:    return "Shift";
    case VK_CONTROL:  return "Ctrl";
    case VK_MENU:     return "ALT";
    case VK_SPACE:    return "Space";
    case VK_TAB:      return "Tab";
    case VK_CAPITAL:  return "Caps";
    case VK_ESCAPE:   return "Esc";
    }

    static char buf[32];
    UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    if (GetKeyNameTextA(static_cast<LONG>(sc) << 16, buf, sizeof(buf)) > 0)
        return buf;

    snprintf(buf, sizeof(buf), "0x%02X", vk);
    return buf;
}

static bool PollKey(int& out)
{
    for (int i = 1; i < 256; i++)
    {
        if (i == VK_LBUTTON) continue; // ignore left click (used to set bind)
        if (GetAsyncKeyState(i) & 1)
        {
            out = i;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Custom widgets
// ---------------------------------------------------------------------------
namespace W
{
    static void SectionTitle(const char* label)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::Dummy({ 0, 2.f });
    }

    static void SubHeader(const char* label)
    {
        ImGui::Dummy({ 0, 4.f });
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::Dummy({ 0, 2.f });
    }

    // Custom checkbox: 14x14 rounded square, accent fill when on
    static bool Checkbox(const char* label, bool* v, bool disabled = false)
    {
        ImGui::PushID(label);

        const float sz = 14.f;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##cb", { sz, sz });
        bool changed = false;
        if (!disabled && ImGui::IsItemClicked())
        {
            *v = !*v;
            changed = true;
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(p, { p.x + sz, p.y + sz },
            *v ? Theme::Accent : Theme::Border, 3.f, 0, 1.f);
        if (*v)
        {
            dl->AddRectFilled(
                { p.x + 3, p.y + 3 }, { p.x + sz - 3, p.y + sz - 3 },
                Theme::Accent, 2.f);
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text,
            disabled ? Theme::TextDisabled : Theme::TextDim);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();

        ImGui::PopID();
        return changed;
    }

    // Small color-picker chip
    static void ColorChip(const char* id, float col[4])
    {
        ImGui::PushID(id);

        const float w = 16.f, h = 14.f;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##chip", { w, h });
        if (ImGui::IsItemClicked())
            ImGui::OpenPopup("##picker");

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 c = ImGui::ColorConvertFloat4ToU32(
            ImVec4(col[0], col[1], col[2], col[3]));
        dl->AddRectFilled(p, { p.x + w, p.y + h }, c, 2.f);
        dl->AddRect(p, { p.x + w, p.y + h }, IM_COL32(0, 0, 0, 220), 2.f);

        if (ImGui::BeginPopup("##picker"))
        {
            ImGui::ColorPicker4("##cp", col,
                ImGuiColorEditFlags_NoInputs |
                ImGuiColorEditFlags_NoSidePreview |
                ImGuiColorEditFlags_NoSmallPreview);
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    // Compact dark "key chip" button (ALT, Mouse 4, ...). Returns true on click.
    static bool KeyChip(const char* id, int vk, bool waiting)
    {
        ImGui::PushID(id);

        const char* text = waiting ? "..." : KeyName(vk);
        ImVec2 ts = ImGui::CalcTextSize(text);
        const float padX = 10.f, padY = 5.f;
        ImVec2 sz = { MaxF(ts.x + padX * 2, 60.f), ts.y + padY * 2 };

        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##k", sz);
        bool clicked = ImGui::IsItemClicked();
        bool hovered = ImGui::IsItemHovered();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 bg = hovered ? Theme::ItemHover : Theme::ChipBg;
        dl->AddRectFilled(p, { p.x + sz.x, p.y + sz.y }, bg, 4.f);
        dl->AddRect(p, { p.x + sz.x, p.y + sz.y }, Theme::BorderSoft, 4.f, 0, 1.f);

        ImVec2 tp = { p.x + (sz.x - ts.x) * 0.5f, p.y + (sz.y - ts.y) * 0.5f };
        dl->AddText(tp, Theme::Text, text);

        ImGui::PopID();
        return clicked;
    }

    // Custom horizontal slider (filled-bar style)
    static bool Slider(const char* label, float* v, float vMin, float vMax,
        const char* fmt, bool disabled = false)
    {
        ImGui::PushID(label);

        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float avail = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        char valBuf[32];
        snprintf(valBuf, sizeof(valBuf), fmt, *v);

        ImU32 textCol = disabled ? Theme::TextDisabled : Theme::TextDim;
        dl->AddText({ p0.x, p0.y }, textCol, label);

        ImVec2 vts = ImGui::CalcTextSize(valBuf);
        dl->AddText({ p0.x + avail - vts.x, p0.y }, Theme::Text, valBuf);

        ImGui::Dummy({ 0, ImGui::GetFontSize() + 4.f });

        const float barH = 6.f;
        ImVec2 bp = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##bar", { avail, barH + 6.f });
        bool active = !disabled && ImGui::IsItemActive();
        bool changed = false;

        if (active)
        {
            float mx = ImGui::GetIO().MousePos.x;
            float t = (mx - bp.x) / avail;
            t = ClampF(t, 0.f, 1.f);
            float nv = vMin + t * (vMax - vMin);
            if (nv != *v) { *v = nv; changed = true; }
        }

        float frac = (vMax > vMin) ? (*v - vMin) / (vMax - vMin) : 0.f;
        frac = ClampF(frac, 0.f, 1.f);

        ImVec2 a = { bp.x, bp.y + 3.f };
        ImVec2 b = { bp.x + avail, bp.y + 3.f + barH };
        dl->AddRectFilled(a, b, Theme::SliderBg, barH * 0.5f);
        if (frac > 0.f)
        {
            ImU32 fill = disabled ? IM_COL32(120, 100, 110, 255) : Theme::Accent;
            dl->AddRectFilled(a, { a.x + avail * frac, b.y }, fill, barH * 0.5f);
        }

        ImGui::PopID();
        return changed;
    }

    // Themed dropdown
    static bool Combo(const char* label, int* v, const char* const* items, int count)
    {
        ImGui::PushID(label);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::Dummy({ 0, 2.f });

        ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::ChipBg);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::ItemHover);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::ItemActive);
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::ChipBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::ItemHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::ItemActive);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme::Card);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::ItemHover);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Theme::ItemActive);
        ImGui::PushStyleColor(ImGuiCol_Border, Theme::BorderSoft);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        bool changed = ImGui::Combo("##c", v, items, count);

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(11);
        ImGui::PopID();
        return changed;
    }

    static void Muted(const char* text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    }
}

// ---------------------------------------------------------------------------
// Bind state (single in-place rebinder)
// ---------------------------------------------------------------------------
static int* g_bindTarget = nullptr;

static void BindRow(const char* id, int* bind)
{
    bool waiting = (g_bindTarget == bind);
    if (W::KeyChip(id, *bind, waiting))
    {
        g_bindTarget = waiting ? nullptr : bind;
    }

    if (waiting)
    {
        int key = 0;
        if (PollKey(key))
        {
            if (key != VK_ESCAPE)
                *bind = key;
            g_bindTarget = nullptr;
        }
    }
}

// ---------------------------------------------------------------------------
// Sidebar
// ---------------------------------------------------------------------------
struct SideItem { int tab; const char* icon; const char* label; };

static void DrawSideHeader(const char* h)
{
    ImGui::Dummy({ 0, 8.f });
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(
        { p.x + 12.f, p.y }, Theme::TextHeader, h);
    ImGui::Dummy({ 0, ImGui::GetFontSize() + 4.f });
}

static void DrawSideItem(const SideItem& it)
{
    ImGui::PushID(it.tab);
    bool selected = Globals::menu_tab == it.tab;

    const float h = 28.f;
    const float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##si", { w, h });
    bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked())
        Globals::menu_tab = it.tab;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected)
        dl->AddRectFilled(p, { p.x + w, p.y + h }, Theme::ItemActive, 4.f);
    else if (hovered)
        dl->AddRectFilled(p, { p.x + w, p.y + h }, Theme::ItemHover, 4.f);

    ImU32 col = selected ? Theme::Text : Theme::TextDim;

    float fy = p.y + (h - ImGui::GetFontSize()) * 0.5f;
    dl->AddText({ p.x + 12.f, fy }, col, it.icon);
    dl->AddText({ p.x + 36.f, fy }, col, it.label);

    ImGui::PopID();
}

static void RenderSidebar()
{
    DrawSideHeader("Aimbot");
    DrawSideItem({ 0, ICON_FA_CROSSHAIRS, "Legit Bot" });

    DrawSideHeader("Visuals");
    DrawSideItem({ 1, ICON_FA_USERS, "Players" });
    DrawSideItem({ 2, ICON_FA_EYE,   "View" });

    DrawSideHeader("Miscellaneous");
    DrawSideItem({ 3, ICON_FA_HOUSE,       "Main" });
    DrawSideItem({ 4, ICON_FA_FLOPPY_DISK, "Configs" });
}

// ---------------------------------------------------------------------------
// Cards / tabs
// ---------------------------------------------------------------------------
static void BeginCard(const char* id, ImVec2 size)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Card);
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
    ImGui::BeginChild(id, size, true);
}

static void EndCard()
{
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

static void RightAlignCursor(float widgetWidth)
{
    float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - widgetWidth;
    ImGui::SetCursorPosX(x);
}

static void RenderAimbotCard(ImVec2 size)
{
    BeginCard("##aim_card", size);

    W::SectionTitle(ICON_FA_CROSSHAIRS "  Aimbot");

    // Enable + bind chip on the right
    ImGui::BeginGroup();
    W::Checkbox("Enable Aimbot", &Globals::aimbot_enabled);
    ImGui::EndGroup();

    {
        ImVec2 ts = ImGui::CalcTextSize(KeyName(Globals::aimbot_bind));
        float chipW = MaxF(ts.x + 20.f, 60.f);
        ImGui::SameLine();
        RightAlignCursor(chipW);
        BindRow("aim_bind", &Globals::aimbot_bind);
    }

    W::Checkbox("Visibility Check", &Globals::aimbot_visibility);
    W::Checkbox("Aimlock", &Globals::aimbot_aimlock);

    // Draw FOV + color chip
    W::Checkbox("Draw FOV", &Globals::aimbot_draw_fov);
    ImGui::SameLine();
    RightAlignCursor(16.f);
    W::ColorChip("aim_fov_col", Globals::aimbot_fov_color);

    W::Slider("FOV", &Globals::aimbot_fov, 0.f, 30.f, "%.1f");
    W::Slider("Smoothing", &Globals::aimbot_smoothing, 0.f, 10.f, "%.1f");

    static const char* hitboxItems[] = { "Head", "Neck", "Chest", "Pelvis" };
    W::Combo("Hitbox", &Globals::aimbot_hitbox,
        hitboxItems, IM_ARRAYSIZE(hitboxItems));

    EndCard();
}

static void RenderTriggerRcsCard(ImVec2 size)
{
    BeginCard("##trig_card", size);

    W::SectionTitle(ICON_FA_HAND "  Triggerbot");

    W::Checkbox("Enable Triggerbot", &Globals::trigger_enabled);
    {
        ImVec2 ts = ImGui::CalcTextSize(KeyName(Globals::trigger_bind));
        float chipW = MaxF(ts.x + 20.f, 60.f);
        ImGui::SameLine();
        RightAlignCursor(chipW);
        BindRow("trig_bind", &Globals::trigger_bind);
    }

    W::Slider("Trigger FOV", &Globals::trigger_fov, 0.f, 10.f, "%.1f");
    W::Slider("Shot Delay", &Globals::trigger_shot_delay, 0.f, 250.f, "%.0f ms");

    W::Checkbox("Draw FOV", &Globals::trigger_draw_fov);
    ImGui::SameLine();
    RightAlignCursor(16.f);
    W::ColorChip("trig_fov_col", Globals::trigger_fov_color);

    W::SubHeader("Adaptive Recoil Control");

    W::Checkbox("Enable RCS", &Globals::rcs_enabled);
    W::Slider("Recoil Reduction", &Globals::rcs_reduction, 0.f, 1.f, "%.2f");
    W::Slider("RCS Calibration", &Globals::rcs_calibration, 0.f, 2.f, "%.2f");

    ImGui::Dummy({ 0, 6.f });
    W::Muted("Smart Detection: Enabled");

    EndCard();
}

static void RenderLegitBotTab()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float gap = 12.f;
    float colW = (avail.x - gap) * 0.5f;

    RenderAimbotCard(ImVec2(colW, avail.y));
    ImGui::SameLine(0.f, gap);
    RenderTriggerRcsCard(ImVec2(colW, avail.y));
}

static void RenderPlaceholderTab(const char* title, const char* hint)
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    BeginCard("##ph", avail);

    W::SectionTitle(title);
    ImGui::Dummy({ 0, 8.f });
    W::Muted(hint);
    ImGui::Dummy({ 0, 4.f });

    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDisabled);
    ImGui::TextUnformatted("(no implementation yet)");
    ImGui::PopStyleColor();

    EndCard();
}

// Row helper: checkbox left, color chip right (color chip is optional)
static void EspRow(const char* label, bool* enable, float color[4])
{
    W::Checkbox(label, enable);
    if (color)
    {
        ImGui::SameLine();
        RightAlignCursor(16.f);
        char id[32];
        snprintf(id, sizeof(id), "##c_%s", label);
        W::ColorChip(id, color);
    }
}

static void RenderEspCard(ImVec2 size)
{
    BeginCard("##esp_card", size);

    W::SectionTitle(ICON_FA_USERS "  Player ESP");

    // Enable + key bind chip
    W::Checkbox("Enable ESP", &Globals::esp_enabled);
    {
        ImVec2 ts = ImGui::CalcTextSize(KeyName(Globals::esp_bind));
        float chipW = MaxF(ts.x + 20.f, 60.f);
        ImGui::SameLine();
        RightAlignCursor(chipW);
        BindRow("esp_bind", &Globals::esp_bind);
    }

    EspRow("Box", &Globals::esp_box, Globals::esp_box_color);
    W::Slider("Box thickness", &Globals::esp_box_thickness, 0.5f, 4.f, "%.1f");

    EspRow("Skeleton", &Globals::esp_skeleton, Globals::esp_skeleton_color);
    W::Slider("Skeleton thickness", &Globals::esp_skeleton_thickness, 0.5f, 4.f, "%.1f");

    EspRow("Name", &Globals::esp_name, Globals::esp_name_color);
    W::Checkbox("Health bar", &Globals::esp_health);

    EndCard();
}

// ----- Chams cards ---------------------------------------------------------
//
// Phase 1 strips the previous DX11 DrawIndexedInstanced chams because it
// could not cleanly distinguish players from weapon skins / gloves / arms.
// The replacement (Phase 2) will hook a high-level scene draw function and
// filter by entity schema class name. Until then, these toggles only persist
// settings - nothing is actually drawn over the geometry.
// ---------------------------------------------------------------------------
static void RenderPlayerChamsCard(ImVec2 size)
{
    BeginCard("##chams_player_card", size);

    W::SectionTitle(ICON_FA_EYE "  Player Chams");
    W::Muted("Enemies (C_CSPlayerPawn).");
    ImGui::Dummy({ 0, 2.f });

    W::Checkbox("Enable", &Globals::chams_player_enabled);

    ImGui::Dummy({ 0, 4.f });
    W::SubHeader("Visible");
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
    ImGui::TextUnformatted("Color");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    RightAlignCursor(16.f);
    W::ColorChip("chams_pvis", Globals::chams_player_visible_color);

    ImGui::Dummy({ 0, 4.f });
    W::SubHeader("Invisible (XRay)");
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
    ImGui::TextUnformatted("Color");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    RightAlignCursor(16.f);
    W::ColorChip("chams_pinvis", Globals::chams_player_invisible_color);


    EndCard();
}

static void RenderHudCard(ImVec2 size)
{
    BeginCard("##hud_card", size);

    W::SectionTitle(ICON_FA_EYE "  HUD");

    W::Checkbox("Enemy counter", &Globals::hud_enemy_counter);

    ImGui::Dummy({ 0, 4.f });
    W::Muted("More HUD widgets coming soon.");

    EndCard();
}

static void RenderWeaponChamsCard(ImVec2 size)
{
    BeginCard("##chams_weapon_card", size);

    W::SectionTitle(ICON_FA_EYE "  Weapon Chams");
    W::Muted("Self viewmodel weapon (C_CSWeaponBase).");
    ImGui::Dummy({ 0, 2.f });

    W::Checkbox("Enable##cweapon", &Globals::chams_weapon_enabled);

    ImGui::Dummy({ 0, 4.f });
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
    ImGui::TextUnformatted("Color");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    RightAlignCursor(16.f);
    W::ColorChip("chams_weapon_col", Globals::chams_weapon_color);


    EndCard();
}

static void RenderHandsChamsCard(ImVec2 size)
{
    BeginCard("##chams_hands_card", size);

    W::SectionTitle(ICON_FA_EYE "  Hands Chams");
    W::Muted("Self viewmodel arms (C_ViewmodelAttachmentModel).");
    ImGui::Dummy({ 0, 2.f });

    W::Checkbox("Enable##chands", &Globals::chams_hands_enabled);

    ImGui::Dummy({ 0, 4.f });
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
    ImGui::TextUnformatted("Color");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    RightAlignCursor(16.f);
    W::ColorChip("chams_hands_col", Globals::chams_hands_color);


    EndCard();
}


// ---------------------------------------------------------------------------
// Chams diagnostics card. Shows the bringup state of the material-based
// chams pipeline (module presence, pattern hits, material build status,
// live hook-call counters) so the user can read off where bringup died
// without attaching a debugger. Hidden once everything is green and at
// least one frame has gone through the hook with at least one classified
// scene-data of any category.
// ---------------------------------------------------------------------------
static void RenderChamsDiagCard(ImVec2 size)
{
    BeginCard("##chams_diag", size);

    W::SectionTitle(ICON_FA_EYE "  Chams diagnostics");

    const auto& d = Chams::GetDiag();

    auto Pill = [](bool ok, const char* label) {
        ImVec4 col = ok ? ImVec4(0.40f, 0.95f, 0.40f, 1.f)
                        : ImVec4(0.95f, 0.40f, 0.40f, 1.f);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(ok ? "OK" : "--");
        ImGui::PopStyleColor();
        ImGui::SameLine(0.f, 6.f);
        ImGui::TextUnformatted(label);
    };

    // Modules
    Pill(d.mod_materialsystem2,  "materialsystem2.dll");   ImGui::SameLine(0.f, 14.f);
    Pill(d.mod_tier0,            "tier0.dll");             ImGui::SameLine(0.f, 14.f);
    Pill(d.mod_scenesystem,      "scenesystem.dll");
    Pill(d.mod_rendersystemdx11, "rendersystemdx11.dll");  ImGui::SameLine(0.f, 14.f);
    Pill(d.mod_client,           "client.dll");

    // Resolution
    Pill(d.matsys_singleton,       "VMaterialSystem2_001"); ImGui::SameLine(0.f, 14.f);
    Pill(d.tier0_loadkv3,          "tier0!LoadKV3");
    Pill(d.creatematerial_pattern, "CMaterialSystem2::CreateMaterial pat");
    Pill(d.renderobjects_pattern,  "CAnimatableSceneObjectDesc::RenderObjects pat");

    // If we got the hook, show where
    if (d.renderobjects_pattern)
    {
        char buf[160];
        _snprintf_s(buf, _TRUNCATE,
                    "    in %s, variant %d, addr 0x%016llX",
                    d.renderobjects_module, d.renderobjects_variant,
                    (unsigned long long)d.hook_target_addr);
        W::Muted(buf);
    }

    Pill(d.hook_created, "MH_CreateHook");  ImGui::SameLine(0.f, 14.f);
    Pill(d.hook_enabled, "MH_EnableHook");

    // Materials
    Pill(d.material_player, "Player mat");  ImGui::SameLine(0.f, 14.f);
    Pill(d.material_weapon, "Weapon mat");  ImGui::SameLine(0.f, 14.f);
    Pill(d.material_hands,  "Hands mat");

    // Live counters
    char line[160];
    _snprintf_s(line, _TRUNCATE,
                "Hook calls: %llu   overrode: %llu",
                (unsigned long long)d.calls_total,
                (unsigned long long)d.calls_overridden);
    W::Muted(line);
    _snprintf_s(line, _TRUNCATE,
                "Last frame classified: player=%u  weapon=%u  hands=%u",
                d.last_classified_player, d.last_classified_weapon, d.last_classified_hands);
    W::Muted(line);

    EndCard();
}

static void RenderPlayersTab()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float gap = 12.f;
    const float diagH = 200.f;

    // Top: full-width diagnostics strip.
    RenderChamsDiagCard(ImVec2(avail.x, diagH));
    ImGui::Dummy({ 0, gap });

    float rest = avail.y - diagH - gap;
    float colW = (avail.x - gap) * 0.5f;
    float rowH = (rest - gap) * 0.5f;

    // Middle row: ESP + Player Chams
    RenderEspCard(ImVec2(colW, rowH));
    ImGui::SameLine(0.f, gap);
    RenderPlayerChamsCard(ImVec2(colW, rowH));

    ImGui::Dummy({ 0, gap });

    // Bottom row: HUD (single card spanning full width)
    RenderHudCard(ImVec2(avail.x, rest - rowH - gap));
}

static void RenderViewTab()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float gap = 12.f;
    const float diagH = 200.f;

    // Top: full-width diagnostics strip.
    RenderChamsDiagCard(ImVec2(avail.x, diagH));
    ImGui::Dummy({ 0, gap });

    float rest = avail.y - diagH - gap;
    float colW = (avail.x - gap) * 0.5f;

    RenderWeaponChamsCard(ImVec2(colW, rest));
    ImGui::SameLine(0.f, gap);
    RenderHandsChamsCard(ImVec2(colW, rest));
}

// ---------------------------------------------------------------------------
// Top-level Menu::Render
// ---------------------------------------------------------------------------
void Menu::Render()
{
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Bg);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg);
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, Theme::TextDisabled);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme::Card);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::ChipBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::ItemHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::ItemActive);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, Theme::Accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, Theme::AccentHi);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, Theme::Accent);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGui::SetNextWindowSize({ 760, 520 }, ImGuiCond_Once);

    ImGui::Begin("##fccp_menu", &IsOpen,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoResize);

    const float sidebarW = 160.f;

    // Sidebar
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BgDim);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    ImGui::BeginChild("##sidebar", ImVec2(sidebarW, 0), false);
    RenderSidebar();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SameLine(0.f, 0.f);

    // Content
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 14));
    ImGui::BeginChild("##content", ImVec2(0, 0), false);

    switch (Globals::menu_tab)
    {
    case 0: RenderLegitBotTab(); break;
    case 1: RenderPlayersTab(); break;
    case 2: RenderViewTab(); break;
    case 3: RenderPlaceholderTab("Main", "Bunnyhop, autostrafe and other misc."); break;
    case 4: RenderPlaceholderTab("Configs", "Save / load presets here."); break;
    default: break;
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();

    ImGui::PopStyleVar(6);
    ImGui::PopStyleColor(12);

    if (!IsOpen)
        g_bindTarget = nullptr;
}
