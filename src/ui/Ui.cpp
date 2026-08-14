#include "Ui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "../app/Win32App.h"

Ui::Ui(const Win32App& app, std::optional<std::string>& outErrorMsgOpt) {
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
    auto& io = ImGui::GetIO();
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();

    {
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

        // menu bar
        bool openGenText = false;
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open...", "Ctrl+O"))
                    execCommand(cmd::OpenFile());
                if (ImGui::MenuItem("From URL...", "Ctrl+U"))
                    execCommand(cmd::OpenUrl());
                if (ImGui::MenuItem("Generate...", "Ctrl+G"))
                    openGenText = true;

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

        // shortcuts
        {
            if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) && !ImGui::IsAnyItemActive()) {
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O)) {
                    io.ClearInputKeys();
                    execCommand(cmd::OpenFile());
                }
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_U)) {
                    io.ClearInputKeys();
                    execCommand(cmd::OpenUrl());
                }
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_G))
                    openGenText = true;

                if (m_FileOpen) {
                    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) {
                        io.ClearInputKeys();
                        execCommand(cmd::SaveAs());
                    }
                }
            }
        }

        // modal opening
        {
            if (openGenText) {
                ImGui::OpenPopup("Generate txt");
            }
            if (!m_InfoMsgData.infoMsg.empty())
                if (!ImGui::IsPopupOpen("##info"))
                    ImGui::OpenPopup("##info");
            if (!m_ErrorMsg.empty()) {
                if (!ImGui::IsPopupOpen("Error"))
                    ImGui::OpenPopup("Error");
            }
        }

        // modals
        {
            ImGui::SetNextWindowPos(mainViewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal(
                "Generate txt", nullptr,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
            )) {
                static int genTxtValue = 1;
                ImGui::InputInt("##GenTxtInput", &genTxtValue, 0, 0);

                ImGui::SameLine();

                constexpr static const char* genTxtComboItems[] = { "Kb", "Mb", "Gb", "Lines" };
                static int genTxtItemIdx = 1;
                if (ImGui::BeginCombo("##GenTxtCombo", genTxtComboItems[genTxtItemIdx], ImGuiComboFlags_WidthFitPreview)) {
                    for (int n = 0; n < IM_COUNTOF(genTxtComboItems); ++n) {
                        const bool is_selected = genTxtItemIdx == n;
                        if (ImGui::Selectable(genTxtComboItems[n], is_selected))
                            genTxtItemIdx = n;
                        if (is_selected)
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

            ImGui::SetNextWindowPos(mainViewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal(
                "##info", nullptr,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
            )) {
                ImGui::TextUnformatted(m_InfoMsgData.infoMsg.c_str(), m_InfoMsgData.infoMsg.c_str() + m_InfoMsgData.infoMsg.size());

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::Button("OK", ImVec2(-FLT_MIN, 0.0f))) {
                    const auto data = std::move(m_InfoMsgData);
                    ImGui::CloseCurrentPopup();
                    if (data.callback)
                        data.callback(true);
                }

                if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, 0.0f))) {
                    const auto data = std::move(m_InfoMsgData);
                    ImGui::CloseCurrentPopup();
                    if (data.callback)
                        data.callback(false);
                }

                ImGui::SetItemDefaultFocus();
                ImGui::EndPopup();
            }

            ImGui::SetNextWindowPos(mainViewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal(
                "Error", nullptr,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
            )) {
                ImGui::TextUnformatted(m_ErrorMsg.c_str(), m_ErrorMsg.c_str() + m_ErrorMsg.size());

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::Button("OK", ImVec2(-FLT_MIN, 0.0f))) {
                    m_ErrorMsg.clear();
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SetItemDefaultFocus();
                ImGui::EndPopup();
            }
        }

        ImGui::End();
    }

    // loading tasks window
    {
        const auto& windowBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        const auto& menuBg = ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg);
        ImGui::PushStyleColor(ImGuiCol_TitleBg, menuBg);
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, menuBg);
        ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, menuBg);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(menuBg.x, menuBg.y, menuBg.z, windowBg.w));
        // ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, ImVec2(0.5f, 0.5f));

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

        constexpr static float width = 320.0f;
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

            float fraction;
            if (!progressData.infinite && !progressData.cancelled) {
                fraction = progressData.progress.load(std::memory_order_relaxed);
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

std::unique_ptr<Ui::ProgressRAII> Ui::pushProgress(std::string name, const bool infinite, std::function<void()> cancelClickCallback) {
    return std::make_unique<ProgressRAII>(
        &m_ProgressDataQueue,
        m_ProgressDataQueue.emplace(m_ProgressDataQueue.cend(), std::move(name), infinite, std::move(cancelClickCallback), false, 0.0f)
    );
}
