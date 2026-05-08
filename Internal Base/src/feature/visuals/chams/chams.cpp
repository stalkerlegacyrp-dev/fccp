#include "Chams.h"

#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstdint>
#include <cstring>

#include "../../../sdk/utils/Globals.h"
#include "../../../../ext/minhook/MinHook.h"

#pragma comment(lib, "d3dcompiler.lib")

// -------------------------------------------------------------------------
// DX11 DrawIndexedInstanced hook-based chams
// Multiple shader types, selectable per-pass (visible / invisible)
// Types: 0=Flat, 1=Shaded, 2=Neon, 3=Metallic, 4=Glow, 5=Wireframe
// -------------------------------------------------------------------------

static constexpr int CHAMS_TYPE_COUNT = 6;

typedef void(__stdcall* DrawIndexedInstancedFn)(
    ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);

static DrawIndexedInstancedFn oDrawIndexedInstanced = nullptr;
static ID3D11Device* g_pDevice = nullptr;
static ID3D11PixelShader* g_shaders[CHAMS_TYPE_COUNT] = {};
static ID3D11DepthStencilState* pOccludedState = nullptr;
static ID3D11DepthStencilState* pVisibleState = nullptr;
static ID3D11BlendState* pBlendState = nullptr;
static ID3D11BlendState* pAdditiveBlend = nullptr;
static ID3D11RasterizerState* pWireframeRS = nullptr;
static ID3D11Buffer* pColorBuffer = nullptr;
static bool                     g_chamsInitialized = false;
static void* g_hookedAddr = nullptr;

// Per-frame DSV tracking for viewmodel detection.
// CS2 renders players/world with one DepthStencilView, then switches
// to a different DSV for the viewmodel pass (hands, gloves, sleeves).
static ID3D11DepthStencilView* g_sceneDSV = nullptr;
static bool                     g_sceneDSVCaptured = false;

// -------------------------------------------------------------------------
// Pixel shader sources
// -------------------------------------------------------------------------

// 0: Flat - solid color
static const char* psFlatCode =
"cbuffer cbColor : register(b0) { float4 color; };"
"struct PS_INPUT { float4 pos : SV_POSITION; };"
"float4 main(PS_INPUT input) : SV_Target {"
"    return color;"
"}";

// 1: Shaded - directional lighting with noise texture
static const char* psShadedCode =
"cbuffer cbColor : register(b0) { float4 color; };"
"struct PS_INPUT { float4 pos : SV_POSITION; };"
"float hash(float2 p) {"
"    p = frac(p * float2(443.897, 441.423));"
"    p += dot(p, p.yx + 19.19);"
"    return frac(p.x * p.y);"
"}"
"float noise(float2 uv) {"
"    float2 i = floor(uv);"
"    float2 f = frac(uv);"
"    f = f * f * (3.0 - 2.0 * f);"
"    float a = hash(i);"
"    float b = hash(i + float2(1.0, 0.0));"
"    float c = hash(i + float2(0.0, 1.0));"
"    float d = hash(i + float2(1.0, 1.0));"
"    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);"
"}"
"float4 main(PS_INPUT input) : SV_Target {"
"    float2 uv = input.pos.xy * 0.001;"
"    float light = 0.5 + 0.5 * (1.0 - uv.y);"
"    float n = noise(uv * 15.0);"
"    float glow = n * n;"
"    float3 c = color.rgb * light + glow * color.rgb * 0.8;"
"    return float4(c, color.a);"
"}";

// 2: Neon - bright edges with pulsing glow
static const char* psNeonCode =
"cbuffer cbColor : register(b0) { float4 color; };"
"struct PS_INPUT { float4 pos : SV_POSITION; };"
"float4 main(PS_INPUT input) : SV_Target {"
"    float2 uv = input.pos.xy * 0.002;"
"    float edge = abs(frac(uv.x * 8.0) - 0.5) * 2.0;"
"    edge = pow(edge, 3.0);"
"    float scan = sin(uv.y * 120.0) * 0.5 + 0.5;"
"    scan = pow(scan, 8.0) * 0.3;"
"    float intensity = 1.5 + edge * 2.0 + scan;"
"    float3 c = color.rgb * intensity;"
"    return float4(c, color.a);"
"}";

// 3: Metallic - shiny reflective look with gradient
static const char* psMetallicCode =
"cbuffer cbColor : register(b0) { float4 color; };"
"struct PS_INPUT { float4 pos : SV_POSITION; };"
"float4 main(PS_INPUT input) : SV_Target {"
"    float2 uv = input.pos.xy * 0.001;"
"    float grad = 0.3 + 0.7 * (1.0 - uv.y);"
"    float spec = pow(abs(sin(uv.x * 20.0 + uv.y * 10.0)), 16.0);"
"    float fresnel = pow(abs(frac(uv.x * 3.0) - 0.5) * 2.0, 2.0);"
"    fresnel = 0.2 + 0.8 * fresnel;"
"    float3 base = color.rgb * grad * fresnel;"
"    float3 highlight = float3(1.0, 1.0, 1.0) * spec * 0.6;"
"    return float4(base + highlight, color.a);"
"}";

// 4: Glow - soft bloom/aura effect
static const char* psGlowCode =
"cbuffer cbColor : register(b0) { float4 color; };"
"struct PS_INPUT { float4 pos : SV_POSITION; };"
"float4 main(PS_INPUT input) : SV_Target {"
"    float2 uv = input.pos.xy * 0.001;"
"    float pulse = sin(uv.y * 6.28 + uv.x * 3.14) * 0.15 + 0.85;"
"    float soft = 0.6 + 0.4 * (1.0 - uv.y);"
"    float3 c = color.rgb * pulse * soft * 1.8;"
"    float a = color.a * (0.5 + 0.5 * pulse);"
"    return float4(c, a);"
"}";

// 5: Wireframe - used with wireframe rasterizer state, simple solid fill
static const char* psWireframeCode =
"cbuffer cbColor : register(b0) { float4 color; };"
"struct PS_INPUT { float4 pos : SV_POSITION; };"
"float4 main(PS_INPUT input) : SV_Target {"
"    return float4(color.rgb * 1.5, color.a);"
"}";

static const char* g_shaderSources[CHAMS_TYPE_COUNT] = {
    psFlatCode, psShadedCode, psNeonCode,
    psMetallicCode, psGlowCode, psWireframeCode
};

// -------------------------------------------------------------------------
// DrawIndexedInstanced hook
// -------------------------------------------------------------------------

static void ApplyPass(
    ID3D11DeviceContext* ctx,
    int shaderType,
    ID3D11DepthStencilState* depthState,
    const float col[4],
    UINT IndexCountPerInstance,
    UINT InstanceCount,
    UINT StartIndexLocation,
    INT  BaseVertexLocation,
    UINT StartInstanceLocation)
{
    int idx = shaderType;
    if (idx < 0 || idx >= CHAMS_TYPE_COUNT) idx = 0;

    ctx->UpdateSubresource(pColorBuffer, 0, nullptr, col, 0, 0);
    ctx->PSSetConstantBuffers(0, 1, &pColorBuffer);
    ctx->OMSetDepthStencilState(depthState, 0);
    ctx->PSSetShader(g_shaders[idx], nullptr, 0);

    // Wireframe type needs rasterizer state change
    ID3D11RasterizerState* oldRS = nullptr;
    if (idx == 5 && pWireframeRS)
    {
        ctx->RSGetState(&oldRS);
        ctx->RSSetState(pWireframeRS);
    }

    // Glow type uses additive blending
    ID3D11BlendState* oldBlendForGlow = nullptr;
    float oldFactorGlow[4];
    UINT oldMaskGlow;
    if (idx == 4 && pAdditiveBlend)
    {
        ctx->OMGetBlendState(&oldBlendForGlow, oldFactorGlow, &oldMaskGlow);
        float bf[4] = { 0, 0, 0, 0 };
        ctx->OMSetBlendState(pAdditiveBlend, bf, 0xffffffff);
    }

    oDrawIndexedInstanced(ctx, IndexCountPerInstance, InstanceCount,
        StartIndexLocation, BaseVertexLocation, StartInstanceLocation);

    if (idx == 5 && oldRS)
    {
        ctx->RSSetState(oldRS);
        oldRS->Release();
    }
    if (idx == 4 && oldBlendForGlow)
    {
        ctx->OMSetBlendState(oldBlendForGlow, oldFactorGlow, oldMaskGlow);
        oldBlendForGlow->Release();
    }
}

static void __stdcall hkDrawIndexedInstanced(
    ID3D11DeviceContext* ctx,
    UINT IndexCountPerInstance,
    UINT InstanceCount,
    UINT StartIndexLocation,
    INT  BaseVertexLocation,
    UINT StartInstanceLocation)
{
    bool anyEnabled = Globals::chams_enabled ||
        Globals::chams_world_enabled ||
        Globals::chams_view_enabled;

    if (!anyEnabled || !g_chamsInitialized)
    {
        oDrawIndexedInstanced(ctx, IndexCountPerInstance, InstanceCount,
            StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
        return;
    }

    UINT stride = 0, offset = 0;
    ID3D11Buffer* vb = nullptr;
    ctx->IAGetVertexBuffers(0, 1, &vb, &stride, &offset);
    if (vb) vb->Release();

    // Get current DepthStencilView — CS2 uses a different DSV for
    // the viewmodel pass (hands/gloves/sleeves) vs the main scene.
    ID3D11DepthStencilView* currentDSV = nullptr;
    ctx->OMGetRenderTargets(0, nullptr, &currentDSV);

    // "Player-like" draw: matches character model vertex layout
    // (body + glasses + gloves of other players AND self viewmodel)
    bool isPlayerLike =
        InstanceCount == 1 &&
        StartInstanceLocation == 0 &&
        BaseVertexLocation == 0 &&
        IndexCountPerInstance > 300 && IndexCountPerInstance < 50000 &&
        stride >= 32 && stride <= 50;

    bool isPlayerBody = false;
    bool isViewmodel = false;
    bool isWorld = false;

    if (isPlayerLike)
    {
        // Capture the scene DSV from the first player-like draw each frame.
        // Players/world render first, viewmodel comes later with a new DSV.
        if (!g_sceneDSVCaptured && currentDSV)
        {
            g_sceneDSV = currentDSV;
            g_sceneDSV->AddRef();
            g_sceneDSVCaptured = true;
        }

        if (g_sceneDSVCaptured && currentDSV && currentDSV != g_sceneDSV)
        {
            // Different DSV from the scene → viewmodel rendering pass
            isViewmodel = true;
        }
        else
        {
            // Same DSV as scene → player body / accessories (glasses, gloves of others)
            isPlayerBody = true;
        }
    }
    else
    {
        // Not player-like geometry → world / environment
        isWorld =
            InstanceCount == 1 &&
            StartInstanceLocation == 0 &&
            IndexCountPerInstance > 300 && IndexCountPerInstance < 50000 &&
            stride >= 20 && stride <= 52;
    }

    if (currentDSV) currentDSV->Release();

    // Select which settings to use based on category
    bool shouldRender = false;
    int visType = 0, invisType = 0;
    const float* visColor = nullptr;
    const float* invisColor = nullptr;

    if (isPlayerBody && Globals::chams_enabled)
    {
        shouldRender = true;
        visType = Globals::chams_visible_type;
        invisType = Globals::chams_invisible_type;
        visColor = Globals::chams_visible_color;
        invisColor = Globals::chams_invisible_color;
    }
    else if (isViewmodel && Globals::chams_view_enabled)
    {
        shouldRender = true;
        visType = Globals::chams_view_visible_type;
        invisType = Globals::chams_view_invisible_type;
        visColor = Globals::chams_view_visible_color;
        invisColor = Globals::chams_view_invisible_color;
    }
    else if (isWorld && Globals::chams_world_enabled)
    {
        shouldRender = true;
        visType = Globals::chams_world_visible_type;
        invisType = Globals::chams_world_invisible_type;
        visColor = Globals::chams_world_visible_color;
        invisColor = Globals::chams_world_invisible_color;
    }

    if (shouldRender)
    {
        // Save original DX11 state
        ID3D11PixelShader* oldPS = nullptr;
        ctx->PSGetShader(&oldPS, nullptr, nullptr);

        ID3D11DepthStencilState* oldDepthState = nullptr;
        UINT stencilRef = 0;
        ctx->OMGetDepthStencilState(&oldDepthState, &stencilRef);

        ID3D11BlendState* oldBlend = nullptr;
        float oldFactor[4];
        UINT oldMask;
        ctx->OMGetBlendState(&oldBlend, oldFactor, &oldMask);

        float blendFactor[4] = { 0, 0, 0, 0 };
        ctx->OMSetBlendState(pBlendState, blendFactor, 0xffffffff);

        ID3D11ShaderResourceView* nullSRV[16] = {};
        ctx->PSSetShaderResources(0, 16, nullSRV);

        // Occluded (through-wall) pass
        ApplyPass(ctx, invisType, pOccludedState, invisColor,
            IndexCountPerInstance, InstanceCount,
            StartIndexLocation, BaseVertexLocation, StartInstanceLocation);

        // Visible pass
        ApplyPass(ctx, visType, pVisibleState, visColor,
            IndexCountPerInstance, InstanceCount,
            StartIndexLocation, BaseVertexLocation, StartInstanceLocation);

        // Restore
        ctx->PSSetShader(oldPS, nullptr, 0);
        ctx->OMSetDepthStencilState(oldDepthState, stencilRef);
        ctx->OMSetBlendState(oldBlend, oldFactor, oldMask);

        if (oldPS) oldPS->Release();
        if (oldDepthState) oldDepthState->Release();
        if (oldBlend) oldBlend->Release();
        return;
    }

    oDrawIndexedInstanced(ctx, IndexCountPerInstance, InstanceCount,
        StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

bool Chams::Init(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
    if (g_chamsInitialized) return true;
    if (!pDevice || !pContext) return false;

    g_pDevice = pDevice;

    // Compile all pixel shaders
    for (int i = 0; i < CHAMS_TYPE_COUNT; ++i)
    {
        ID3DBlob* blob = nullptr;
        ID3DBlob* errorBlob = nullptr;

        if (FAILED(D3DCompile(g_shaderSources[i],
            strlen(g_shaderSources[i]), nullptr, nullptr, nullptr,
            "main", "ps_4_0", 0, 0, &blob, &errorBlob)))
        {
            if (errorBlob) errorBlob->Release();
            return false;
        }
        pDevice->CreatePixelShader(blob->GetBufferPointer(),
            blob->GetBufferSize(), nullptr, &g_shaders[i]);
        blob->Release();
    }

    // Constant buffer for color (float4 = 16 bytes)
    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.ByteWidth = 16;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    pDevice->CreateBuffer(&cbd, nullptr, &pColorBuffer);

    // Alpha blend state
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    pDevice->CreateBlendState(&blendDesc, &pBlendState);

    // Additive blend state (for Glow type)
    D3D11_BLEND_DESC addDesc = {};
    addDesc.RenderTarget[0].BlendEnable = TRUE;
    addDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    addDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    addDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    addDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    addDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    addDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    addDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    pDevice->CreateBlendState(&addDesc, &pAdditiveBlend);

    // Wireframe rasterizer state
    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_WIREFRAME;
    rsDesc.CullMode = D3D11_CULL_NONE;
    rsDesc.DepthClipEnable = TRUE;
    pDevice->CreateRasterizerState(&rsDesc, &pWireframeRS);

    // Depth stencil: occluded (depth GREATER = behind walls)
    D3D11_DEPTH_STENCIL_DESC occludedDesc = {};
    occludedDesc.DepthEnable = TRUE;
    occludedDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    occludedDesc.DepthFunc = D3D11_COMPARISON_GREATER;
    pDevice->CreateDepthStencilState(&occludedDesc, &pOccludedState);

    // Depth stencil: visible (depth LESS_EQUAL = in front)
    D3D11_DEPTH_STENCIL_DESC visibleDesc = {};
    visibleDesc.DepthEnable = TRUE;
    visibleDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    visibleDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    pDevice->CreateDepthStencilState(&visibleDesc, &pVisibleState);

    // Hook DrawIndexedInstanced (vtable[20] on ID3D11DeviceContext)
    void** vtable = *reinterpret_cast<void***>(pContext);
    g_hookedAddr = vtable[20];

    if (MH_CreateHook(g_hookedAddr, reinterpret_cast<void*>(&hkDrawIndexedInstanced),
        reinterpret_cast<void**>(&oDrawIndexedInstanced)) != MH_OK)
        return false;

    if (MH_EnableHook(g_hookedAddr) != MH_OK)
        return false;

    g_chamsInitialized = true;
    return true;
}

void Chams::OnNewFrame()
{
    // Reset per-frame DSV tracking so we re-capture the scene DSV each frame.
    if (g_sceneDSV) { g_sceneDSV->Release(); g_sceneDSV = nullptr; }
    g_sceneDSVCaptured = false;
}

void Chams::Destroy()
{
    if (g_sceneDSV) { g_sceneDSV->Release(); g_sceneDSV = nullptr; }
    g_sceneDSVCaptured = false;

    if (g_hookedAddr)
    {
        MH_DisableHook(g_hookedAddr);
        g_hookedAddr = nullptr;
    }
    oDrawIndexedInstanced = nullptr;

    for (int i = 0; i < CHAMS_TYPE_COUNT; ++i)
    {
        if (g_shaders[i]) { g_shaders[i]->Release(); g_shaders[i] = nullptr; }
    }

    if (pOccludedState) { pOccludedState->Release();  pOccludedState = nullptr; }
    if (pVisibleState) { pVisibleState->Release();   pVisibleState = nullptr; }
    if (pBlendState) { pBlendState->Release();     pBlendState = nullptr; }
    if (pAdditiveBlend) { pAdditiveBlend->Release();  pAdditiveBlend = nullptr; }
    if (pWireframeRS) { pWireframeRS->Release();    pWireframeRS = nullptr; }
    if (pColorBuffer) { pColorBuffer->Release();    pColorBuffer = nullptr; }

    g_chamsInitialized = false;
}
