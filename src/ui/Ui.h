#pragma once

#include "imgui.h"

class Win32App;

class Ui {
public:
    explicit Ui(const Win32App& app);
    ~Ui();

    static void newFrame();
    static void build();
    static void render(Win32App& app, const ImVec4& clearColor);
};
