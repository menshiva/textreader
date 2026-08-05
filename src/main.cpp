#include "app/Win32App.h"
#include "ui/Ui.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    Win32App app(L"TextReader", 1280, 720);
    if (!app.isInitialized())
        return 1;
    Ui ui(app);

    while (!app.shouldClose()) {
        app.pollMessages();
        if (!app.beginFrame())
            continue;
        ui.newFrame();
        ui.build();
        app.bindAndClear(ImVec4(0.45f, 0.55f, 0.60f, 1.00f));
        ui.render();
        app.present(true);
    }

    return 0;
}
