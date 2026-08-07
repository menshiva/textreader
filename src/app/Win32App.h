#pragma once

#include <d3d11.h>
#include <optional>
#include <filesystem>
#include "imgui.h"

class Win32App {
public:
    Win32App(const wchar_t* windowName, UINT initW, UINT initH);
    ~Win32App();

    Win32App(const Win32App&) = delete;
    Win32App& operator=(const Win32App&) = delete;
    Win32App(Win32App&&) = delete;
    Win32App& operator=(Win32App&&) = delete;

    void pollMessages();
    bool beginFrame();
    void bindAndClear(const ImVec4& clearColor) const;
    void present(bool vsync);

    void queueResize(UINT w, UINT h);

    std::optional<std::filesystem::path> showTextFileDialog(bool open) const;

    void setWindowTitle(const wchar_t* title) const;
    void resetWindowTitle() const;

    bool isInitialized() const { return m_Initialized; }
    bool shouldClose() const { return m_ShouldClose; }

    struct TmpFileDescriptor {
        ~TmpFileDescriptor();
        FILE* file = nullptr;
    };
    std::unique_ptr<TmpFileDescriptor> getTmpTextFileWriteDescriptor() const;
    void exchangeTmpTextFiles() const;
    void removeTmpTextFiles(bool removeR, bool removeW) const;
    const std::filesystem::path& getTmpTextFileReadPath() const { return m_TmpTextFileReadPath; }

    float getDpiScale() const { return m_DpiScale; }
    HWND hwnd() const { return m_Hwnd; }
    ID3D11Device* device() const { return m_pd3dDevice; }
    ID3D11DeviceContext* context() const { return m_pd3dDeviceContext; }
private:
    bool CreateDeviceD3D();
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();

    const wchar_t* m_InitialWindowName;
    float m_DpiScale = 1.0f;

    LPCWSTR m_lpClassName = nullptr;
    HINSTANCE m_hInstance = nullptr;
    HWND m_Hwnd = nullptr;

    bool m_Initialized = false;
    bool m_ShouldClose = false;

    std::filesystem::path m_TmpTextFileReadPath;
    std::filesystem::path m_TmpTextFileWritePath;

    ID3D11Device* m_pd3dDevice = nullptr;
    ID3D11DeviceContext* m_pd3dDeviceContext = nullptr;
    ID3D11RenderTargetView* m_mainRenderTargetView = nullptr;
    IDXGISwapChain* m_pSwapChain = nullptr;
    bool m_SwapChainOccluded = false;
    UINT m_ResizeWidth = 0, m_ResizeHeight = 0;
};
