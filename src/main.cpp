#include "app/Win32App.h"
#include "ui/Ui.h"
#include "controller/Controller.h"

static void fatalError(const std::string& msg) {
    MessageBoxA(nullptr, msg.c_str(), "Fatal Error", MB_OK | MB_ICONERROR);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    std::optional<std::string> errorMsgOpt;

    Win32App app(L"TextReader", 1280, 720, errorMsgOpt);
    if (errorMsgOpt.has_value()) {
        fatalError(errorMsgOpt.value());
        return 1;
    }

    Ui ui(app, errorMsgOpt);
    if (errorMsgOpt.has_value()) {
        fatalError(errorMsgOpt.value());
        return 1;
    }

    Controller controller(app, ui);

    // TODO
// #ifdef _DEBUG
    AllocConsole();
    FILE* dummy;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
// #endif

    while (!app.shouldClose()) {
        app.pollMessages();

        if (app.beginFrame()) {
            ui.newFrame();
            ui.build();
            app.bindAndClear(ImVec4(0.45f, 0.55f, 0.60f, 1.00f));
            ui.render();
            app.present(true);
        }

        controller.tick();
    }

    return 0;
}
