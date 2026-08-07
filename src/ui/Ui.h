#pragma once

#include <atomic>
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

    void showErrorMsg(std::string msg) { m_ErrorMsg = std::move(msg); }

    void setFileOpen(const bool open) { m_FileOpen = open; }

    enum class GenTxtState : uint8_t { None, Idle, Running };
    void setGenTxtState(const GenTxtState state) { m_GenTxtState = state; }
    void setGenTxtProgressTS(float val);
private:
    std::optional<Command> m_Command;

    std::string m_ErrorMsg;

    bool m_FileOpen = false;

    GenTxtState m_GenTxtState = GenTxtState::None;
    std::atomic<float> m_GenTxtProgress{0.0f};
};
