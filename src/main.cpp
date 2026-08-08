#include "app/Win32App.h"
#include "ui/Ui.h"
#include "controller/Controller.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    bool appInitialized = false;
    Win32App app(L"TextReader", 1280, 720, appInitialized);
    if (!appInitialized)
        return 1;

    Ui ui(app);
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
