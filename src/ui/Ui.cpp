#include "Ui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "imgui_internal.h"
#include "../app/Win32App.h"

Ui::Ui(const Win32App& app, std::optional<std::string>& outErrorMsgOpt) : m_TextView(*this) {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.IniFilename = nullptr; // no imgui.ini next to the exe - there is no window layout worth remembering

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
    drawSearchPanel();
    drawProgress();
}

void Ui::render() {
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void Ui::setFileOpened(const TextView::Source* sourcePtr) {
    m_TextView.reset(sourcePtr);
    m_FileOpen = true;
    setSearchIdle();
    if (m_SearchData.panelOpen)
        m_TextView.setSearchNeedle(m_SearchData.needleBuf);
}

void Ui::setFileClosed() {
    m_TextView.reset();
    m_FileOpen = false;
    setSearchIdle();
}

void Ui::setSearchResult(const bool found) {
    m_SearchData.resultOpt = found;
}

void Ui::setSearchIdle() {
    m_SearchData.resultOpt.reset();
}

void Ui::drawMainWindow() {
    const auto mainViewport = ImGui::GetMainViewport();
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

void Ui::drawMenuBar(Popups& outPopups) {
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

    if (m_FileOpen && ImGui::BeginMenu("Search")) {
        if (ImGui::MenuItem("Find...", "Ctrl+F"))
            openSearchPanel();

        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void Ui::processShortcuts(Popups& outPopups) {
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return;

    auto& io = ImGui::GetIO();

    if (!ImGui::IsAnyItemActive()) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O)) {
            io.ClearInputKeys(); // fix sticky ctrl
            execCommand(cmd::OpenFile());
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_U))
            outPopups.url = true;
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_G))
            outPopups.genText = true;

        if (m_FileOpen && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) {
            io.ClearInputKeys(); // fix sticky ctrl
            execCommand(cmd::SaveAs());
        }
    }

    if (m_FileOpen) {
        // using IsKeyPressed() because it reads the key directly, ignoring focus
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
            openSearchPanel();
        if (m_SearchData.panelOpen) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                closeSearchPanel();
            }
            else {
                if (ImGui::IsKeyPressed(ImGuiKey_F3, false))
                    sendFindCommand(io.KeyShift);
            }
        }
    }
}

void Ui::drawModals(const Popups& popups) {
    if (popups.genText)
        ImGui::OpenPopup("Generate txt");
    if (popups.url)
        ImGui::OpenPopup("Open from URL");
    if (!m_MessageData.text.empty() && !ImGui::IsPopupOpen("##message"))
        ImGui::OpenPopup("##message");
    if (!m_ErrorMsg.empty() && !ImGui::IsPopupOpen("Error"))
        ImGui::OpenPopup("Error");

    drawUrlModal();
    drawGenTextModal();
    drawMessageModal();
    drawErrorModal();
}

static bool beginCenteredModal(const char* name) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    return ImGui::BeginPopupModal(
        name, nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
    );
}

static void setNextWindowAtViewportCorner(const ImVec2& pivot, const ImVec2& margin, const ImGuiCond cond) {
    const auto viewport = ImGui::GetMainViewport();
    const ImVec2 corner = viewport->WorkPos + viewport->WorkSize * pivot;
    const ImVec2 inwards(pivot.x > 0.5f ? -margin.x : margin.x, pivot.y > 0.5f ? -margin.y : margin.y);
    ImGui::SetNextWindowPos(corner + inwards, cond, pivot);
}

void Ui::drawUrlModal() {
    if (!beginCenteredModal("Open from URL"))
        return;

    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * kUrlFieldWidthEm);

    const bool enter = ImGui::InputTextWithHint(
        "##url", "URL", m_UrlBuf, IM_COUNTOF(m_UrlBuf),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll
    );

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const bool openDisabled = m_UrlBuf[0] == '\0';
    if (openDisabled)
        ImGui::BeginDisabled();
    if (ImGui::Button("Open", ImVec2(-FLT_MIN, 0.0f)) || (enter && !openDisabled)) {
        execCommand(cmd::OpenUrl(m_UrlBuf));
        ImGui::CloseCurrentPopup();
    }
    if (openDisabled)
        ImGui::EndDisabled();

    if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void Ui::drawGenTextModal() {
    if (!beginCenteredModal("Generate txt"))
        return;

    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();

    ImGui::InputInt("##GenTxtInput", &m_GenTextData.value, 0, 0);

    ImGui::SameLine();

    if (ImGui::BeginCombo("##GenTxtCombo", cmd::GenRandom::kTypeNames[m_GenTextData.typeIdx], ImGuiComboFlags_WidthFitPreview)) {
        for (int n = 0; n < cmd::GenRandom::kTypeCount; ++n) {
            const bool isSelected = m_GenTextData.typeIdx == n;
            if (ImGui::Selectable(cmd::GenRandom::kTypeNames[n], isSelected))
                m_GenTextData.typeIdx = n;
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const bool genButtonDisabled = m_GenTextData.value < 1;
    if (genButtonDisabled)
        ImGui::BeginDisabled();
    if (ImGui::Button("Generate", ImVec2(-FLT_MIN, 0.0f))) {
        execCommand(cmd::GenRandom(static_cast<uint32_t>(m_GenTextData.value), static_cast<cmd::GenRandom::Type>(m_GenTextData.typeIdx)));
        ImGui::CloseCurrentPopup();
    }
    if (genButtonDisabled)
        ImGui::EndDisabled();

    if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void Ui::drawMessageModal() {
    if (!beginCenteredModal("##message"))
        return;

    ImGui::TextUnformatted(m_MessageData.text.c_str(), m_MessageData.text.c_str() + m_MessageData.text.size());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();

    std::optional<bool> answerOpt;
    if (ImGui::Button("OK", ImVec2(-FLT_MIN, 0.0f)))
        answerOpt = true;
    if (m_MessageData.confirmCallback && ImGui::Button("Cancel", ImVec2(-FLT_MIN, 0.0f)))
        answerOpt = false;

    ImGui::SetItemDefaultFocus();

    MessageData answered;
    if (answerOpt.has_value()) {
        answered = std::move(m_MessageData);
        m_MessageData = {};
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();

    if (answered.confirmCallback)
        answered.confirmCallback(*answerOpt);
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

void Ui::drawSearchPanel() {
    if (!m_FileOpen || !m_SearchData.panelOpen)
        return;

    setNextWindowAtViewportCorner(ImVec2(1.0f, 1.0f), ImVec2(kFloatingWindowMargin, kFloatingWindowMargin), ImGuiCond_FirstUseEver);

    bool stayOpen = true;
    ImGui::Begin(
        "Find###search", &stayOpen,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
    );

    if (m_SearchData.focusRequested) {
        m_SearchData.focusRequested = false;
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * kSearchFieldWidthEm);
    const bool enter = ImGui::InputTextWithHint(
        "##needle", "Text to find", m_SearchData.needleBuf, IM_COUNTOF(m_SearchData.needleBuf),
        ImGuiInputTextFlags_EnterReturnsTrue
    );
    if (ImGui::IsItemEdited())
        m_TextView.setSearchNeedle(m_SearchData.needleBuf);
    if (enter) {
        ImGui::SetKeyboardFocusHere(-1); // revert focus
        sendFindCommand(ImGui::GetIO().KeyShift);
    }

    ImGui::SameLine();
    if (ImGui::ArrowButton("##prev", ImGuiDir_Up))
        sendFindCommand(true);
    ImGui::SetItemTooltip("Previous (Shift+F3)");

    ImGui::SameLine();
    if (ImGui::ArrowButton("##next", ImGuiDir_Down))
        sendFindCommand(false);
    ImGui::SetItemTooltip("Next (F3)");

    if (m_SearchData.running) {
        const float progress = m_SearchData.progress.load(std::memory_order_relaxed);
        ImGui::Text("Searching... %.0f%%", progress * 100 + 0.01f);
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel"))
            execCommand(cmd::CancelFind());
    }
    else {
        if (m_SearchData.resultOpt.has_value()) {
            const bool value = *m_SearchData.resultOpt;
            ImGui::TextUnformatted(value ? "Found" : "Not Found");
        }
        else {
            ImGui::TextUnformatted(" "); // keeps the panel from resizing
        }
    }

    ImGui::End();

    if (!stayOpen)
        closeSearchPanel();
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

    setNextWindowAtViewportCorner(ImVec2(1.0f, 0.0f), ImVec2(kFloatingWindowMargin, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_Once);

    ImGui::Begin(
        "Loading tasks###progress", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
    );
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);

    ImGui::Dummy(ImVec2(kProgressBarWidth, 0.0f));

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
        ImGui::ProgressBar(fraction, ImVec2(kProgressBarWidth, 0.0f), buf);

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

void Ui::openSearchPanel() {
    m_SearchData.panelOpen = true;
    m_SearchData.focusRequested = true;
    m_TextView.setSearchNeedle(m_SearchData.needleBuf);
}

void Ui::closeSearchPanel() {
    m_SearchData.panelOpen = false;
    setSearchIdle();
    m_TextView.setSearchNeedle({});
    execCommand(cmd::CancelFind());
}

void Ui::sendFindCommand(const bool backwards) {
    if (m_SearchData.needleBuf[0] == '\0')
        return;

    m_TextView.setSearchNeedle(m_SearchData.needleBuf);

    cmd::Find command;
    command.needle = m_SearchData.needleBuf;
    command.backwards = backwards;
    command.fromLineIdx = m_TextView.getFirstWholeVisibleLineIdx();
    command.continueFromCurrentMatch = m_TextView.isCurrentSearchMatchVisible();
    execCommand(command);
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

Ui::SearchProgressRAII::SearchProgressRAII(SearchData* searchDataPtr) : m_SearchDataPtr(searchDataPtr) {
    m_SearchDataPtr->progress.store(0.0f, std::memory_order_relaxed);
    m_SearchDataPtr->resultOpt.reset();
    m_SearchDataPtr->running = true;
}

Ui::SearchProgressRAII::~SearchProgressRAII() {
    m_SearchDataPtr->running = false;
}

std::atomic<float>& Ui::SearchProgressRAII::getProgressTS() const {
    return m_SearchDataPtr->progress;
}

std::unique_ptr<Ui::SearchProgressRAII> Ui::beginSearch() {
    return std::make_unique<SearchProgressRAII>(&m_SearchData);
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
