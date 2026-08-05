#pragma once

class Win32App;

class Ui {
public:
    explicit Ui(const Win32App& app);
    ~Ui();

    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;
    Ui(Ui&&) = delete;
    Ui& operator=(Ui&&) = delete;

    static void newFrame();
    static void build();
    static void render();
};
