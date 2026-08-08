#pragma once

#include <functional>
#include <memory>
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

    using CommandHandler = std::function<bool(const Command&)>;
    void setCommandHandler(CommandHandler handler) { m_SendCommand = std::move(handler); }

    static void newFrame();
    void build();
    static void render();

    void showErrorMsg(std::string msg) { m_ErrorMsg = std::move(msg); }
    void setFileOpen(const bool open) { m_FileOpen = open; }
private:
    struct ProgressPopupData {
        const char* name = nullptr;
        bool infinite = false;
        std::atomic<float> progress{0.0f};
    } m_ProgressPopupData;
public:
    struct ProgressPopupRAII {
        explicit ProgressPopupRAII(ProgressPopupData* dataPtr);
        ~ProgressPopupRAII();

        void setProgressTS(float val) const;
    private:
        ProgressPopupData* m_DataPtr;
    };
    std::unique_ptr<ProgressPopupRAII> acquireProgressPopup(const char* name, bool infinite);
private:
    bool sendAndExecCommand(const Command& command) const;

    CommandHandler m_SendCommand;

    std::string m_ErrorMsg;

    bool m_FileOpen = false;
};
