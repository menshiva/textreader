#include "Ui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "../app/Win32App.h"

Ui::Ui(const Win32App& app) {
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

        ImGui::Begin(
            "Main window", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus
        );

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open...", "Ctrl+O"))
                    m_Command = cmd::OpenFile();
                if (ImGui::MenuItem("From URL...", "Ctrl+U"))
                    m_Command = cmd::OpenUrl();
                if (ImGui::MenuItem("Generate...", "Ctrl+G"))
                    m_GenTxtState = GenTxtState::Idle;

                if (m_FileOpen) {
                    ImGui::Separator();

                    if (ImGui::MenuItem("Save as...", "Ctrl+S"))
                        m_Command = cmd::SaveAs();

                    ImGui::Separator();

                    if (ImGui::MenuItem("Close"))
                        m_Command = cmd::Close();
                }

                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        {
            ImGui::SetNextWindowPos(mainViewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

            if (!m_ErrorMsg.empty()) {
                if (!ImGui::IsPopupOpen("Error"))
                    ImGui::OpenPopup("Error");
            }
            else if (m_GenTxtState != GenTxtState::None) {
                if (!ImGui::IsPopupOpen("Generate txt")) {
                    setGenTxtProgressTS(0.0f);
                    ImGui::OpenPopup("Generate txt");
                }
            }
        }

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

            if (m_GenTxtState != GenTxtState::None) {
                if (m_GenTxtState == GenTxtState::Idle) {
                    setGenTxtProgressTS(0.0f);

                    const bool genButtonDisabled = genTxtValue < 1;
                    if (genButtonDisabled)
                        ImGui::BeginDisabled();
                    if (ImGui::Button("Generate", ImVec2(-FLT_MIN, 0.0f)))
                        m_Command = cmd::GenRandom(static_cast<uint32_t>(genTxtValue), static_cast<cmd::GenRandom::Type>(genTxtItemIdx));
                    if (genButtonDisabled)
                        ImGui::EndDisabled();
                }
                else {
                    ImGui::ProgressBar(m_GenTxtProgress.load(std::memory_order_relaxed));
                }

                if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, 0))) {
                    if (m_GenTxtState == GenTxtState::Running) {
                        m_Command = cmd::CancelGenRandom();
                    }
                    else {
                        m_GenTxtState = GenTxtState::None;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            else {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
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

std::optional<Command> Ui::takeCommand() {
    if (!m_Command.has_value())
        return std::nullopt;
    return std::exchange(m_Command, std::nullopt);
}

void Ui::setGenTxtProgressTS(const float val) {
    m_GenTxtProgress.store(val, std::memory_order_relaxed);
}
