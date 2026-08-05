#include "Win32App.h"
#include "imgui_impl_win32.h"

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
static LRESULT WINAPI WndProc(const HWND hWnd, const UINT msg, const WPARAM wParam, const LPARAM lParam) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    auto* self = reinterpret_cast<Win32App*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    switch (msg) {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED && self)
                self->queueResize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
                return 0;
            break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

Win32App::Win32App(const wchar_t* windowName, const UINT initW, const UINT initH) {
    // Make process DPI aware and obtain main monitor scale
    ImGui_ImplWin32_EnableDpiAwareness();
    m_DpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    // Create application window
    m_lpClassName = L"TextReaderWindowClass";
    m_hInstance = GetModuleHandle(nullptr);
    const WNDCLASSEXW wc = {
        sizeof(WNDCLASSEXW), 0, WndProc, 0L, 0L, m_hInstance,
        nullptr, nullptr, nullptr, nullptr, m_lpClassName,
        nullptr
    };
    RegisterClassExW(&wc);
    m_Hwnd = ::CreateWindowW(
        m_lpClassName, windowName, WS_OVERLAPPEDWINDOW, 100, 100,
        static_cast<int>(initW * m_DpiScale), static_cast<int>(initH * m_DpiScale), nullptr, nullptr, m_hInstance, nullptr
    );
    if (!m_Hwnd)
        return;
    SetWindowLongPtrW(m_Hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // Initialize Direct3D
    if (!CreateDeviceD3D())
        return;

    // Show the window
    ShowWindow(m_Hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_Hwnd);

    m_Initialized = true;
}

Win32App::~Win32App() {
    CleanupDeviceD3D();
    if (m_Hwnd)
        DestroyWindow(m_Hwnd);
    UnregisterClassW(m_lpClassName, m_hInstance);
}

void Win32App::pollMessages() {
    // Poll and handle messages (inputs, window resize, etc.)
    // See the WndProc() function above for our to dispatch events to the Win32 backend.
    MSG msg;
    while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&msg);
        ::DispatchMessage(&msg);
        if (msg.message == WM_QUIT)
            m_ShouldClose = true;
    }
}

bool Win32App::beginFrame() {
    if (m_ShouldClose)
        return false;

    // Handle window being minimized or screen locked
    if (m_SwapChainOccluded && m_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
        Sleep(10);
        return false;
    }
    m_SwapChainOccluded = false;

    // Handle window resize (we don't resize directly in the WM_SIZE handler)
    if (m_ResizeWidth != 0 && m_ResizeHeight != 0) {
        CleanupRenderTarget();
        (void) m_pSwapChain->ResizeBuffers(0, m_ResizeWidth, m_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
        m_ResizeWidth = m_ResizeHeight = 0;
        CreateRenderTarget();
    }

    return true;
}

void Win32App::bindAndClear(const ImVec4& clearColor) const {
    const float clearColorWithAlpha[4] = { clearColor.x * clearColor.w, clearColor.y * clearColor.w, clearColor.z * clearColor.w, clearColor.w };
    m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, nullptr);
    m_pd3dDeviceContext->ClearRenderTargetView(m_mainRenderTargetView, clearColorWithAlpha);
}

void Win32App::present(const bool vsync) {
    const HRESULT hr = m_pSwapChain->Present(vsync, 0);
    m_SwapChainOccluded = hr == DXGI_STATUS_OCCLUDED;
}

void Win32App::queueResize(const UINT w, const UINT h) {
    m_ResizeWidth = w;
    m_ResizeHeight = h;
}

bool Win32App::CreateDeviceD3D() {
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_Hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    constexpr UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    constexpr D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2,
        D3D11_SDK_VERSION, &sd, &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext
    );
    if (res == DXGI_ERROR_UNSUPPORTED) {
        // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2,
            D3D11_SDK_VERSION, &sd, &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext
        );
    }
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void Win32App::CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (m_pSwapChain) { m_pSwapChain->Release(); m_pSwapChain = nullptr; }
    if (m_pd3dDeviceContext) { m_pd3dDeviceContext->Release(); m_pd3dDeviceContext = nullptr; }
    if (m_pd3dDevice) { m_pd3dDevice->Release(); m_pd3dDevice = nullptr; }
}

void Win32App::CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    (void) m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    (void) m_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_mainRenderTargetView);
    pBackBuffer->Release();
}

void Win32App::CleanupRenderTarget() {
    if (m_mainRenderTargetView) { m_mainRenderTargetView->Release(); m_mainRenderTargetView = nullptr; }
}
