#pragma once

#include <optional>
#include "../controller/Commands.h"

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
    void build();
    static void render();

    std::optional<Command> takeCommand();

    void setFileOpen(const bool open) { m_FileOpen = open; }
private:
    std::optional<Command> m_Command;

    bool m_FileOpen = false;
};
