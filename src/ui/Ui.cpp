#include "Ui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "imgui_internal.h"
#include "../app/Win32App.h"

Ui::Ui(const Win32App& app) : m_TextView() {
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
    ImGui_ImplWin32_Init(app.hwnd());
    ImGui_ImplDX11_Init(app.device(), app.context());

    // Load Fonts
    // - If fonts are not explicitly loaded, Dear ImGui will select an embedded font: either AddFontDefaultVector() or AddFontDefaultBitmap().
    //   This selection is based on (style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi) reaching a small threshold.
    // - You can load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - If a file cannot be loaded, AddFont functions will return a nullptr. Please handle those errors in your code (e.g. use an assertion, display an error and quit).
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use FreeType for higher quality font rendering.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //style.FontSizeBase = 20.0f;
    //io.Fonts->AddFontDefaultVector();
    //io.Fonts->AddFontDefaultBitmap();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

    const auto font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 14.0f, nullptr);
    IM_ASSERT(font != nullptr);
}

Ui::~Ui() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void Ui::newFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Ui::build() {
    const ImGuiIO& io = ImGui::GetIO();

    {
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

        // menu bar
        bool openGenText = false;
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open...", "Ctrl+O"))
                    sendAndExecCommand(cmd::OpenFile());
                if (ImGui::MenuItem("From URL...", "Ctrl+U"))
                    sendAndExecCommand(cmd::OpenUrl());
                if (ImGui::MenuItem("Generate...", "Ctrl+G"))
                    openGenText = true;

                if (m_FileOpen) {
                    ImGui::Separator();

                    if (ImGui::MenuItem("Save as...", "Ctrl+S"))
                        sendAndExecCommand(cmd::SaveAs());

                    ImGui::Separator();

                    if (ImGui::MenuItem("Close"))
                        sendAndExecCommand(cmd::Close());
                }

                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // modal opening
        {
            if (m_ProgressPopupData.name) {
                if (!ImGui::IsPopupOpen("##progress")) {
                    ImGui::OpenPopup("##progress");
                }
            }
            else if (openGenText) {
                ImGui::OpenPopup("Generate txt");
            }

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
                    if (sendAndExecCommand(cmd::GenRandom(static_cast<uint32_t>(genTxtValue), static_cast<cmd::GenRandom::Type>(genTxtItemIdx))))
                        ImGui::CloseCurrentPopup();
                }
                if (genButtonDisabled)
                    ImGui::EndDisabled();

                if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, 0)))
                    ImGui::CloseCurrentPopup();

                ImGui::EndPopup();
            }

            ImGui::SetNextWindowPos(mainViewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal(
                "##progress", nullptr,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
            )) {
                if (m_ProgressPopupData.name) {
                    float fraction;
                    char buf[64];
                    if (m_ProgressPopupData.infinite) {
                        fraction = -FLT_MIN;
                        ImFormatString(buf, IM_COUNTOF(buf), "%s...", m_ProgressPopupData.name);
                    }
                    else {
                        fraction = m_ProgressPopupData.progress.load(std::memory_order_relaxed);
                        ImFormatString(buf, IM_COUNTOF(buf), "%s: %.0f%%", m_ProgressPopupData.name, fraction * 100 + 0.01f);
                    }
                    ImGui::ProgressBar(fraction, ImVec2(0.0f, 0.0f), buf);

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, 0.0f)))
                        sendAndExecCommand(cmd::Cancel());
                }
                else {
                    ImGui::CloseCurrentPopup();
                }
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

    // TODO: remove
    {
        static bool show_demo_window = true;
        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);
    }

    // TODO: remove
    // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
    {
        static float f = 0.0f;
        static int counter = 0;

        ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

        ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)

        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f

        if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
            counter++;
        ImGui::SameLine();
        ImGui::Text("counter = %d", counter);

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
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

Ui::ProgressPopupRAII::ProgressPopupRAII(ProgressPopupData* dataPtr) : m_DataPtr(dataPtr) {}

Ui::ProgressPopupRAII::~ProgressPopupRAII() {
    m_DataPtr->name = nullptr;
}

void Ui::ProgressPopupRAII::setProgressTS(const float val) const {
    m_DataPtr->progress.store(val, std::memory_order_relaxed);
}

std::unique_ptr<Ui::ProgressPopupRAII> Ui::acquireProgressPopup(const char *name, const bool infinite) {
    if (m_ProgressPopupData.name)
        return nullptr;

    m_ProgressPopupData.name = name;
    m_ProgressPopupData.infinite = infinite;
    if (!m_ProgressPopupData.infinite)
        m_ProgressPopupData.progress.store(0.0f, std::memory_order_relaxed);

    return std::make_unique<ProgressPopupRAII>(&m_ProgressPopupData);
}

bool Ui::sendAndExecCommand(const Command& command) const {
    if (m_SendCommand)
        return m_SendCommand(command);
    return false;
}
