#include "Ui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "../app/Win32App.h"

Ui::Ui(const Win32App& app, std::optional<std::string>& outErrorMsgOpt) : m_TextView(*this) {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(app.getDpiScale()); // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = app.getDpiScale(); // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

    // Setup Platform/Renderer backends
    if (!ImGui_ImplWin32_Init(app.hwnd())) {
        outErrorMsgOpt = "Cannot initialize ImGui Win32 backend";
        return;
    }
    m_Win32Initialized = true;
    if (!ImGui_ImplDX11_Init(app.device(), app.context())) {
        outErrorMsgOpt = "Cannot initialize ImGui DirectX 11 backend";
        return;
    }
    m_Dx11Initialized = true;

    // load fonts
    {
        auto fontPath = app.getExeDir() / L"fonts" / L"JetBrainsMono-Regular.ttf";
        auto font = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 18.0f, nullptr, nullptr);
        if (!font) {
            // fallback to consola.ttf
            fontPath = app.getWinDir() / L"Fonts" / L"consola.ttf";
            font = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 16.0f, nullptr, nullptr);
        }
        if (!font) {
            outErrorMsgOpt = "Cannot load the font";
            return;
        }
    }
}

Ui::~Ui() {
    if (m_Dx11Initialized)
        ImGui_ImplDX11_Shutdown();
    if (m_Win32Initialized)
        ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void Ui::newFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Ui::build() {
    drawMainWindow();
    drawProgress();
}

void Ui::drawMainWindow() {
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(mainViewport->WorkPos);
    ImGui::SetNextWindowSize(mainViewport->WorkSize);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); // remove padding - useful for text view
    ImGui::Begin(
        "Main window", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus
    );
    ImGui::PopStyleVar();

    if (m_FileOpen)
        m_TextView.draw();

    Popups pending;
    drawMenuBar(pending);
    processShortcuts(pending);
    drawModals(pending);

    ImGui::End();
}

void Ui::drawMenuBar(Popups& outPopups) const {
    if (!ImGui::BeginMenuBar())
        return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open...", "Ctrl+O"))
            execCommand(cmd::OpenFile());
        if (ImGui::MenuItem("From URL...", "Ctrl+U"))
            outPopups.url = true;
        if (ImGui::MenuItem("Generate...", "Ctrl+G"))
            outPopups.genText = true;

        if (m_FileOpen) {
            ImGui::Separator();

            if (ImGui::MenuItem("Save as...", "Ctrl+S"))
                execCommand(cmd::SaveAs());

            ImGui::Separator();

            if (ImGui::MenuItem("Close"))
                execCommand(cmd::Close());
        }

        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void Ui::processShortcuts(Popups& outPopups) const {
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || ImGui::IsAnyItemActive())
        return;

    auto& io = ImGui::GetIO();

    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O)) {
        io.ClearInputKeys();
        execCommand(cmd::OpenFile());
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_U))
        outPopups.url = true;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_G))
        outPopups.genText = true;

    if (m_FileOpen && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) {
        io.ClearInputKeys();
        execCommand(cmd::SaveAs());
    }
}

void Ui::drawModals(const Popups& popups) {
    if (popups.genText)
        ImGui::OpenPopup("Generate txt");
    if (popups.url)
        ImGui::OpenPopup("Open from URL");
    if (!m_InfoMsgData.infoMsg.empty() && !ImGui::IsPopupOpen("##info"))
        ImGui::OpenPopup("##info");
    if (!m_ErrorMsg.empty() && !ImGui::IsPopupOpen("Error"))
        ImGui::OpenPopup("Error");

    drawUrlModal();
    drawGenTextModal();
    drawInfoModal();
    drawErrorModal();
}

static bool beginCenteredModal(const char* name) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    return ImGui::BeginPopupModal(
        name, nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
    );
}

void Ui::drawUrlModal() const {
    if (!beginCenteredModal("Open from URL"))
        return;

    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24.0f);

    static char urlBuf[2048] = "";
    const bool enter = ImGui::InputTextWithHint("##url", "URL", urlBuf, IM_COUNTOF(urlBuf), ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const bool openDisabled = urlBuf[0] == '\0';
    if (openDisabled)
        ImGui::BeginDisabled();
    if (ImGui::Button("Open", ImVec2(-FLT_MIN, 0.0f)) || (enter && !openDisabled)) {
        execCommand(cmd::OpenUrl(urlBuf));
        ImGui::CloseCurrentPopup();
    }
    if (openDisabled)
        ImGui::EndDisabled();

    if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void Ui::drawGenTextModal() const {
    if (!beginCenteredModal("Generate txt"))
        return;

    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();

    static int genTxtValue = 1;
    ImGui::InputInt("##GenTxtInput", &genTxtValue, 0, 0);

    ImGui::SameLine();

    static constexpr const char* genTxtComboItems[] = { "Kb", "Mb", "Gb", "Lines" };
    static int genTxtItemIdx = 1;
    if (ImGui::BeginCombo("##GenTxtCombo", genTxtComboItems[genTxtItemIdx], ImGuiComboFlags_WidthFitPreview)) {
        for (int n = 0; n < IM_COUNTOF(genTxtComboItems); ++n) {
            const bool isSelected = genTxtItemIdx == n;
            if (ImGui::Selectable(genTxtComboItems[n], isSelected))
                genTxtItemIdx = n;
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const bool genButtonDisabled = genTxtValue < 1;
    if (genButtonDisabled)
        ImGui::BeginDisabled();
    if (ImGui::Button("Generate", ImVec2(-FLT_MIN, 0.0f))) {
        execCommand(cmd::GenRandom(static_cast<uint32_t>(genTxtValue), static_cast<cmd::GenRandom::Type>(genTxtItemIdx)));
        ImGui::CloseCurrentPopup();
    }
    if (genButtonDisabled)
        ImGui::EndDisabled();

    if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void Ui::drawInfoModal() {
    if (!beginCenteredModal("##info"))
        return;

    ImGui::TextUnformatted(m_InfoMsgData.infoMsg.c_str(), m_InfoMsgData.infoMsg.c_str() + m_InfoMsgData.infoMsg.size());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();

    if (ImGui::Button("OK", ImVec2(-FLT_MIN, 0.0f))) {
        const auto data = std::move(m_InfoMsgData);
        ImGui::CloseCurrentPopup();
        if (data.callback)
            data.callback(true);
    }

    if (m_InfoMsgData.callback && ImGui::Button("Cancel", ImVec2(-FLT_MIN, 0.0f))) {
        const auto data = std::move(m_InfoMsgData);
        ImGui::CloseCurrentPopup();
        data.callback(false);
    }

    ImGui::SetItemDefaultFocus();
    ImGui::EndPopup();
}

void Ui::drawErrorModal() {
    if (!beginCenteredModal("Error"))
        return;

    ImGui::TextUnformatted(m_ErrorMsg.c_str(), m_ErrorMsg.c_str() + m_ErrorMsg.size());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();

    if (ImGui::Button("OK", ImVec2(-FLT_MIN, 0.0f))) {
        m_ErrorMsg.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::SetItemDefaultFocus();
    ImGui::EndPopup();
}

void Ui::drawProgress() {
    const auto& windowBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const auto& menuBg = ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, menuBg);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, menuBg);
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, menuBg);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(menuBg.x, menuBg.y, menuBg.z, windowBg.w));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, ImVec2(0.5f, 0.5f));

    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const auto& viewportMin = mainViewport->WorkPos;
    const auto& viewportMax = viewportMin + mainViewport->WorkSize;

    ImGui::SetNextWindowPos(ImVec2(viewportMax.x - 24.0f, viewportMin.y), 0, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_Once);

    ImGui::Begin(
        "Loading tasks###progress", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav
    );
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);

    static constexpr float width = 320.0f;
    ImGui::Dummy(ImVec2(width, 0.0f));

    int i = 0;
    char buf[64];
    for (auto& progressData : m_ProgressDataQueue) {
        ImGui::PushID(i); // for ImGui::CloseButton()

        if (i > 0) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }

        float fraction = progressData.progress.load(std::memory_order_relaxed);
        if (!progressData.cancelled && fraction >= 0.0f) {
            ImFormatString(buf, IM_COUNTOF(buf), "%s: %.0f%%", progressData.name.c_str(), fraction * 100 + 0.01f);
        }
        else {
            fraction = -static_cast<float>(ImGui::GetTime());
            if (!progressData.cancelled)
                ImFormatString(buf, IM_COUNTOF(buf), "%s...", progressData.name.c_str());
            else
                ImFormatString(buf, IM_COUNTOF(buf), "%s: cancelling...", progressData.name.c_str());
        }
        ImGui::ProgressBar(fraction, ImVec2(width, 0.0f), buf);

        if (progressData.cancelClickCallback) {
            const float buttonSize = ImGui::GetFontSize();
            const float barHeight = ImGui::GetFrameHeight();

            ImGui::SameLine();
            const auto pos = ImGui::GetCursorScreenPos();

            const bool wasCancelled = progressData.cancelled; // fix ImGui::EndDisabled() when progressData.cancelled becomes true after CloseButton click
            if (wasCancelled)
                ImGui::BeginDisabled();
            if (ImGui::CloseButton(ImGui::GetID("##cancel"), ImVec2(pos.x, pos.y + (barHeight - buttonSize) * 0.5f))) {
                progressData.cancelClickCallback();
                progressData.cancelled = true;
            }
            if (wasCancelled)
                ImGui::EndDisabled();

            ImGui::Dummy(ImVec2(buttonSize, buttonSize)); // move cursor
        }

        ImGui::PopID();
        ++i;
    }

    ImGui::End();
}

void Ui::render() {
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void Ui::setFileOpened(TextView::Source source) {
    m_TextView.reset(std::move(source));
    m_FileOpen = true;
}

void Ui::setFileClosed() {
    m_TextView.reset();
    m_FileOpen = false;
}

void Ui::execCommand(const Command& command) const {
    if (m_SendCommand)
        m_SendCommand(command);
}

Ui::ProgressRAII::ProgressRAII(
    std::list<ProgressData>* queuePtr, const std::list<ProgressData>::iterator &it
) : m_ProgressDataQueuePtr(queuePtr), m_Iterator(it) {}

Ui::ProgressRAII::~ProgressRAII() {
    m_ProgressDataQueuePtr->erase(m_Iterator);
}

std::atomic<float>& Ui::ProgressRAII::getProgressTS() const {
    return m_Iterator->progress;
}

void Ui::ProgressRAII::setCancelled() const {
    m_Iterator->cancelled = true;
}

std::unique_ptr<Ui::ProgressRAII> Ui::pushProgress(std::string name, std::function<void()> cancelClickCallback) {
    return std::make_unique<ProgressRAII>(
        &m_ProgressDataQueue,
        m_ProgressDataQueue.emplace(m_ProgressDataQueue.cend(), std::move(name), std::move(cancelClickCallback))
    );
}
