#pragma once

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace Chams {
    bool Init(ID3D11Device* pDevice, ID3D11DeviceContext* pContext);
    void Destroy();
    void OnNewFrame();   // call from Present before rendering — resets per-frame DSV tracking
}
