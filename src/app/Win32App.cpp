#include "Win32App.h"
#include "imgui_impl_win32.h"
#include <shobjidl.h>
#include <wrl/client.h>

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
static LRESULT WINAPI WndProc(const HWND hWnd, const UINT msg, const WPARAM wParam, const LPARAM lParam) {
    if (const LRESULT handled = ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return handled;

    static constexpr UINT_PTR kResizeMoveLoopTimerId = 1;

    auto* self = reinterpret_cast<Win32App*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
    switch (msg) {
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
        case WM_SIZE: {
            if (wParam != SIZE_MINIMIZED && self) {
                self->queueResize(LOWORD(lParam), HIWORD(lParam));
                self->runFrame();
            }
            return 0;
        }
        case WM_ERASEBKGND: {
            // we paint every pixel ourselves
            return 1;
        }
        case WM_ENTERSIZEMOVE: {
            // dragging the frame puts windows into its own modal loop inside DefWindowProc (DispatchMessage does not return and the main loop stops running)
            // https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-entersizemove
            SetTimer(hWnd, kResizeMoveLoopTimerId, USER_TIMER_MINIMUM, nullptr);
            if (self)
                self->setInResizeMoveLoop(true);
            break;
        }
        case WM_TIMER: {
            if (wParam == kResizeMoveLoopTimerId && self)
                self->runFrame();
            break;
        }
        case WM_EXITSIZEMOVE: {
            KillTimer(hWnd, kResizeMoveLoopTimerId);
            if (self)
                self->setInResizeMoveLoop(false);
            break;
        }
        case WM_SYSCOMMAND: {
            if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
                return 0;
            break;
        }
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

Win32App::ComApartment::ComApartment() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    m_MustUninitialize = hr != RPC_E_CHANGED_MODE;
}

Win32App::ComApartment::~ComApartment() {
    if (m_MustUninitialize)
        CoUninitialize();
}

Win32App::Win32App(const wchar_t* windowName, const UINT initW, const UINT initH, std::optional<std::string>& outErrorMsgOpt) {
    m_WindowData.initialWindowName = windowName;

    // Make process DPI aware and obtain main monitor scale
    ImGui_ImplWin32_EnableDpiAwareness();
    m_WindowData.dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    // Create application window
    m_WindowData.lpClassName = L"TextReaderWindowClass";
    m_WindowData.hInstance = GetModuleHandle(nullptr);
    const WNDCLASSEXW wc = {
        sizeof(WNDCLASSEXW), 0, WndProc, 0L, 0L, m_WindowData.hInstance,
        nullptr, nullptr, nullptr, nullptr, m_WindowData.lpClassName,
        nullptr
    };
    if (!RegisterClassExW(&wc)) {
        outErrorMsgOpt = "Cannot register the window class";
        return;
    }
    m_WindowData.hwnd = ::CreateWindowW(
        m_WindowData.lpClassName, m_WindowData.initialWindowName, WS_OVERLAPPEDWINDOW, 100, 100,
        static_cast<int>(initW * m_WindowData.dpiScale), static_cast<int>(initH * m_WindowData.dpiScale), nullptr, nullptr, m_WindowData.hInstance, nullptr
    );
    if (!m_WindowData.hwnd) {
        outErrorMsgOpt = "Cannot create the window";
        return;
    }
    SetWindowLongPtrW(m_WindowData.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // Initialize Direct3D
    if (!createDeviceD3D()) {
        outErrorMsgOpt = "Cannot initialize Direct3D 11";
        return;
    }

    ShowWindow(m_WindowData.hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_WindowData.hwnd);

    {
        wchar_t dir[MAX_PATH];
        if (GetWindowsDirectoryW(dir, MAX_PATH))
            m_WinDir = std::filesystem::path(dir);
    }

    {
        wchar_t dir[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
        if (n > 0 && n < MAX_PATH)
            m_ExeDir = std::filesystem::path(dir).parent_path();
    }

    {
        wchar_t dir[MAX_PATH + 1];
        const DWORD n = GetTempPathW(MAX_PATH + 1, dir); // always has '\' at the end
        if (n != 0 && n <= MAX_PATH) {
            m_ReadTmpTextFilePath = std::filesystem::path(dir) / L"TextReader (tmp)";
            m_WriteTmpTextFilePath = std::filesystem::path(dir) / L"TextReader (tmp write).txt";
        }
    }
}

Win32App::~Win32App() {
    std::error_code ec;
    if (!m_ReadTmpTextFilePath.empty())
        std::filesystem::remove(m_ReadTmpTextFilePath, ec);
    if (!m_WriteTmpTextFilePath.empty())
        std::filesystem::remove(m_WriteTmpTextFilePath, ec);

    cleanupDeviceD3D();
    if (m_WindowData.hwnd)
        DestroyWindow(m_WindowData.hwnd);
    UnregisterClassW(m_WindowData.lpClassName, m_WindowData.hInstance);
}

void Win32App::pollMessages() {
    // Poll and handle messages (inputs, window resize, etc.)
    // See the WndProc() function above for our to dispatch events to the Win32 backend.
    MSG msg;
    while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&msg);
        ::DispatchMessage(&msg);
        if (msg.message == WM_QUIT)
            m_WindowData.shouldClose = true;
    }
}

bool Win32App::beginFrame() {
    if (m_WindowData.shouldClose)
        return false;

    // handle window resize
    if (m_D3D11Data.resizeWidth != 0 && m_D3D11Data.resizeHeight != 0) {
        cleanupRenderTarget();
        const HRESULT hr = m_D3D11Data.swapChain->ResizeBuffers(0, m_D3D11Data.resizeWidth, m_D3D11Data.resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
        createRenderTarget();
        if (SUCCEEDED(hr) && m_D3D11Data.renderTargetView)
            m_D3D11Data.resizeWidth = m_D3D11Data.resizeHeight = 0;
    }

    // handle window being minimized or screen locked
    if (!m_InResizeMoveLoop && m_D3D11Data.swapChainOccluded && m_D3D11Data.swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
        Sleep(10);
        return false;
    }
    m_D3D11Data.swapChainOccluded = false;

    return !!m_D3D11Data.renderTargetView; // nothing to draw into - skip the frame
}

void Win32App::bindAndClear(const ImVec4& clearColor) const {
    const float clearColorWithAlpha[4] = { clearColor.x * clearColor.w, clearColor.y * clearColor.w, clearColor.z * clearColor.w, clearColor.w };
    m_D3D11Data.context->OMSetRenderTargets(1, &m_D3D11Data.renderTargetView, nullptr);
    m_D3D11Data.context->ClearRenderTargetView(m_D3D11Data.renderTargetView, clearColorWithAlpha);
}

void Win32App::present(const bool vsync) {
    const HRESULT hr = m_D3D11Data.swapChain->Present(vsync, 0);
    m_D3D11Data.swapChainOccluded = hr == DXGI_STATUS_OCCLUDED;
}

void Win32App::queueResize(const UINT w, const UINT h) {
    m_D3D11Data.resizeWidth = w;
    m_D3D11Data.resizeHeight = h;
}

void Win32App::runFrame() {
    if (m_InFrame || !m_FrameCallback)
        return;
    m_InFrame = true;
    m_FrameCallback();
    m_InFrame = false;
}

void Win32App::setWindowTitle(const std::optional<const wchar_t*>& titleOpt) const {
    SetWindowTextW(m_WindowData.hwnd, titleOpt.value_or(m_WindowData.initialWindowName));
}

std::optional<std::filesystem::path> Win32App::showTextFileDialog(const bool open) const {
    static constexpr COMDLG_FILTERSPEC kFilters[] = {
        {L"Text files (*.txt)", L"*.txt"},
        {L"All files (*.*)", L"*.*"}
    };

    Microsoft::WRL::ComPtr<IFileDialog> dlg;
    if (FAILED(CoCreateInstance(open ? CLSID_FileOpenDialog : CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return std::nullopt;

    if (open)
        (void) dlg->SetTitle(L"Open text file");
    else
        (void) dlg->SetTitle(L"Save as");
    (void) dlg->SetFileTypes(ARRAYSIZE(kFilters), kFilters);
    (void) dlg->SetFileTypeIndex(1);
    if (!open)
        (void) dlg->SetDefaultExtension(L"txt");

    DWORD flags = 0;
    if (SUCCEEDED(dlg->GetOptions(&flags))) {
        if (open)
            flags = flags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST;
        else
            flags = flags | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT;
    }
    (void) dlg->SetOptions(flags);

    if (FAILED(dlg->Show(m_WindowData.hwnd))) // cancelled
        return std::nullopt;

    Microsoft::WRL::ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item)))
        return std::nullopt;

    PWSTR raw = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)))
        return std::nullopt;

    std::filesystem::path p(raw);
    CoTaskMemFree(raw);

    return p;
}

bool Win32App::createDeviceD3D() {
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    // sd.BufferDesc.RefreshRate.Numerator = 60;
    // sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_WindowData.hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    constexpr UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    constexpr D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2,
        D3D11_SDK_VERSION, &sd, &m_D3D11Data.swapChain, &m_D3D11Data.device, &featureLevel, &m_D3D11Data.context
    );
    if (res == DXGI_ERROR_UNSUPPORTED) {
        // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2,
            D3D11_SDK_VERSION, &sd, &m_D3D11Data.swapChain, &m_D3D11Data.device, &featureLevel, &m_D3D11Data.context
        );
    }
    if (res != S_OK)
        return false;

    createRenderTarget();
    return true;
}

void Win32App::cleanupDeviceD3D() {
    cleanupRenderTarget();
    if (m_D3D11Data.swapChain) {
        m_D3D11Data.swapChain->Release();
        m_D3D11Data.swapChain = nullptr;
    }
    if (m_D3D11Data.context) {
        m_D3D11Data.context->Release();
        m_D3D11Data.context = nullptr;
    }
    if (m_D3D11Data.device) {
        m_D3D11Data.device->Release();
        m_D3D11Data.device = nullptr;
    }
}

void Win32App::createRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(m_D3D11Data.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer)
        return;

    if (FAILED(m_D3D11Data.device->CreateRenderTargetView(backBuffer, nullptr, &m_D3D11Data.renderTargetView)))
        m_D3D11Data.renderTargetView = nullptr;

    backBuffer->Release();
}

void Win32App::cleanupRenderTarget() {
    if (m_D3D11Data.context)
        m_D3D11Data.context->OMSetRenderTargets(0, nullptr, nullptr);

    if (m_D3D11Data.renderTargetView) {
        m_D3D11Data.renderTargetView->Release();
        m_D3D11Data.renderTargetView = nullptr;
    }
}
