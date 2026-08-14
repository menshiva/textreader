#pragma once

#include <d3d11.h>
#include <filesystem>
#include <functional>
#include "imgui.h"

class Win32App {
public:
    Win32App(const wchar_t* windowName, UINT initW, UINT initH, std::optional<std::string>& outErrorMsgOpt);
    ~Win32App();

    Win32App(const Win32App&) = delete;
    Win32App& operator=(const Win32App&) = delete;
    Win32App(Win32App&&) noexcept = delete;
    Win32App& operator=(Win32App&&) noexcept = delete;

    void pollMessages();
    bool beginFrame();
    void bindAndClear(const ImVec4& clearColor) const;
    void present(bool vsync);
    void queueResize(UINT w, UINT h);

    // While the user drags the border or the caption, Windows runs its own message loop inside
    // DefWindowProc and DispatchMessage does not return until the drag ends - so the main loop
    // stops and the content freezes. Handing the frame over as a callback lets the window
    // procedure keep drawing from inside that loop.
    using FrameCallback = std::function<void()>;
    void setFrameCallback(FrameCallback callback) { m_FrameCallback = std::move(callback); }
    void runFrame();
    void setInResizeMoveLoop(const bool inLoop) { m_InResizeMoveLoop = inLoop; }

    float getDpiScale() const { return m_WindowData.dpiScale; }
    HWND hwnd() const { return m_WindowData.hwnd; }
    ID3D11Device* device() const { return m_D3D11Data.device; }
    ID3D11DeviceContext* context() const { return m_D3D11Data.context; }

    bool shouldClose() const { return m_WindowData.shouldClose; }

    void setWindowTitle(const std::optional<const wchar_t*>& titleOpt) const;
    std::optional<std::filesystem::path> showTextFileDialog(bool open) const;

    const std::filesystem::path& getWinDir() const { return m_WinDir; }
    const std::filesystem::path& getExeDir() const { return m_ExeDir; }
    const std::filesystem::path& getReadTmpTextFilePath() const { return m_ReadTmpTextFilePath; }
    const std::filesystem::path& getWriteTmpTextFilePath() const { return m_WriteTmpTextFilePath; }
private:
    bool CreateDeviceD3D();
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();

    struct WindowData {
        const wchar_t* initialWindowName;
        float dpiScale = 1.0f;

        LPCWSTR lpClassName = nullptr;
        HINSTANCE hInstance = nullptr;
        HWND hwnd = nullptr;

        bool shouldClose = false;
    } m_WindowData;

    FrameCallback m_FrameCallback;
    bool m_InFrame = false; // guards against a WM_TIMER / WM_SIZE frame
    bool m_InResizeMoveLoop = false;

    struct D3D11Data {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        ID3D11RenderTargetView* renderTargetView = nullptr;
        IDXGISwapChain* swapChain = nullptr;
        bool swapChainOccluded = false;
        UINT resizeWidth = 0, resizeHeight = 0;
    } m_D3D11Data;

    std::filesystem::path m_WinDir;
    std::filesystem::path m_ExeDir;
    std::filesystem::path m_ReadTmpTextFilePath;
    std::filesystem::path m_WriteTmpTextFilePath;
};
