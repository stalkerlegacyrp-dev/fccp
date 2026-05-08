#include "Hooks.h"

#include "../../ext/imgui/imgui.h"
#include "../../ext/imgui/imgui_impl_win32.h"
#include "../../ext/imgui/imgui_impl_dx11.h"
#include "../../ext/minhook/MinHook.h"
#include "../../ext/iconfont/IconsFontAwesome6.h"
#include "../../ext/iconfont/fa_solid_900.h"

#include "../../src/menu/Menu.h"
#include "../../src/feature/visuals/Visuals.h"
#include "../../src/sdk/entity/EntityManager.h"
#include "../../src/sdk/utils/Globals.h"
#include "../../src/sdk/memory/Offsets.h"
#include "../../src/sdk/memory/PatternScan.h"
#include "../feature/misc/Misc.h"
#include "../feature/combat/Combat.h"

#pragma comment(lib, "d3d11.lib")

static void LoadMenuFonts()
{
    ImGuiIO& io = ImGui::GetIO();

    // Base font: ImGui's default proggy clean (compiled into imgui).
    ImFontConfig baseCfg{};
    baseCfg.SizePixels = 13.f;
    io.Fonts->AddFontDefault(&baseCfg);

    // Merge FontAwesome 6 Solid (subset) into the same atlas slot.
    static const ImWchar faRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };

    ImFontConfig faCfg{};
    faCfg.MergeMode = true;
    faCfg.PixelSnapH = true;
    faCfg.GlyphMinAdvanceX = 13.f;
    faCfg.FontDataOwnedByAtlas = false;

    io.Fonts->AddFontFromMemoryTTF(
        (void*)IconFontData::FaSolid_data,
        (int)IconFontData::FaSolid_size,
        13.f, &faCfg, faRanges
    );

    io.Fonts->Build();
}

static ID3D11Device* g_Device = nullptr;
static ID3D11DeviceContext* g_Context = nullptr;
static ID3D11RenderTargetView* g_RTV = nullptr;
static HWND                     g_Window = nullptr;
static bool                     g_Init = false;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND, UINT, WPARAM, LPARAM
);

LRESULT __stdcall Hooks::hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Always feed ImGui so it tracks state correctly (don't gate by IsOpen).
    ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

    if (Menu::IsOpen)
    {
        // Swallow input only — let the game keep handling paint/resize/destroy.
        if ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
            msg == WM_MOUSEHOVER || msg == WM_MOUSELEAVE ||
            msg == WM_KEYDOWN || msg == WM_KEYUP ||
            msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP ||
            msg == WM_CHAR || msg == WM_INPUT)
        {
            return TRUE;
        }
    }

    return CallWindowProc(oWndProc, hWnd, msg, wParam, lParam);
}

HRESULT __stdcall Hooks::hkPresent(IDXGISwapChain* swapChain, UINT sync, UINT flags)
{
    if (!g_Init)
    {
        if (FAILED(swapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_Device)))
            return oPresent(swapChain, sync, flags);

        g_Device->GetImmediateContext(&g_Context);

        DXGI_SWAP_CHAIN_DESC sd{};
        swapChain->GetDesc(&sd);
        g_Window = sd.OutputWindow;

        ID3D11Texture2D* backBuffer = nullptr;
        swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        g_Device->CreateRenderTargetView(backBuffer, nullptr, &g_RTV);
        backBuffer->Release();

        oWndProc = (WNDPROC)SetWindowLongPtr(
            g_Window, GWLP_WNDPROC, (LONG_PTR)hkWndProc
        );

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;

        LoadMenuFonts();

        ImGui_ImplWin32_Init(g_Window);
        ImGui_ImplDX11_Init(g_Device, g_Context);

        g_Init = true;
    }

    // Force-show ImGui's software cursor while the menu is open so the
    // game's hidden / clipped system cursor doesn't leave the user blind.
    {
        ImGuiIO& io = ImGui::GetIO();
        io.MouseDrawCursor = Menu::IsOpen;
        if (Menu::IsOpen)
            ClipCursor(nullptr);
    }

    {
        DXGI_SWAP_CHAIN_DESC sd{};
        if (SUCCEEDED(swapChain->GetDesc(&sd)))
        {
            Globals::ScreenWidth = (int)sd.BufferDesc.Width;
            Globals::ScreenHeight = (int)sd.BufferDesc.Height;
        }
    }

    EntityManager::Get().Update();

    uintptr_t client = Memory::GetModuleBase("client.dll");
    memcpy(
        &Globals::ViewMatrix,
        (void*)(client + Offsets::dwViewMatrix),
        sizeof(Globals::ViewMatrix)
    );

    Combat::Run();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (GetAsyncKeyState(VK_INSERT) & 1)
        Menu::IsOpen = !Menu::IsOpen;

    if (Menu::IsOpen)
        Menu::Render();

    Visuals::Render();
    Misc::Render();
    Combat::Render();

    ImGui::Render();
    g_Context->OMSetRenderTargets(1, &g_RTV, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    return oPresent(swapChain, sync, flags);
}

void Hooks::Setup()
{
    if (MH_Initialize() != MH_OK)
        return;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DummyDX";

    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(
        wc.lpszClassName,
        L"",
        WS_OVERLAPPEDWINDOW,
        0, 0, 100, 100,
        nullptr, nullptr,
        wc.hInstance, nullptr
    );

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl;

    if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &sd,
        &sc,
        &dev,
        &fl,
        &ctx)))
    {
        void** vtable = *reinterpret_cast<void***>(sc);
        void* present = vtable[8];

        MH_CreateHook(present, reinterpret_cast<void*>(&hkPresent), reinterpret_cast<void**>(&oPresent));
        MH_EnableHook(MH_ALL_HOOKS);

        sc->Release();
        dev->Release();
        ctx->Release();
    }

    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

void Hooks::Destroy()
{
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    if (g_Window && oWndProc)
        SetWindowLongPtr(g_Window, GWLP_WNDPROC, (LONG_PTR)oWndProc);

    if (!g_Init)
        return;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (g_RTV)     g_RTV->Release();
    if (g_Context) g_Context->Release();
    if (g_Device)  g_Device->Release();

    g_Init = false;
}
