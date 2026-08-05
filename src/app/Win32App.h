#pragma once

#include <d3d11.h>
#include <cstdint>

class Win32App {
public:
    Win32App(const wchar_t* windowName, uint32_t initW, uint32_t initH);
    ~Win32App();

    void pollMessages();
    bool beginFrame();
    static void present(bool vsync);

    bool isInitialized() const { return m_Initialized; }
    bool shouldClose() const { return m_ShouldClose; }
    float getDpiScale() const { return m_DpiScale; }
    const HWND& hwnd() const { return m_Hwnd; }
    ID3D11Device* device() const { return m_pd3dDevice; }
    ID3D11DeviceContext* context() const { return m_pd3dDeviceContext; }
    ID3D11RenderTargetView*& renderTargetView() { return m_mainRenderTargetView; }
private:
    bool CreateDeviceD3D();
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();

    float m_DpiScale;

    WNDCLASSEXW m_Wc;
    HWND m_Hwnd = nullptr;

    bool m_Initialized = false;
    bool m_ShouldClose = false;

    ID3D11Device* m_pd3dDevice = nullptr;
    ID3D11DeviceContext* m_pd3dDeviceContext = nullptr;
    ID3D11RenderTargetView* m_mainRenderTargetView = nullptr;
};
