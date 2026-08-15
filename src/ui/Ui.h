#pragma once

#include <memory>
#include "../controller/Commands.h"
#include "text_view/TextView.h"

class Win32App;

class Ui {
public:
    Ui(const Win32App& app, std::optional<std::string>& outErrorMsgOpt);
    ~Ui();

    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;
    Ui(Ui&&) noexcept = delete;
    Ui& operator=(Ui&&) noexcept = delete;

    using CommandHandler = std::function<void(const Command&)>;
    void setCommandHandler(CommandHandler handler) { m_SendCommand = std::move(handler); }

    static void newFrame();
    void build();
    static void render();

    void setFileOpened(TextView::Source source);
    void setFileClosed();

    struct InfoMsgData { std::string infoMsg; std::function<void(bool)> callback;};
    void showInfoMsg(InfoMsgData msg) { m_InfoMsgData = std::move(msg); }

    void showErrorMsg(std::string msg) { m_ErrorMsg = std::move(msg); }
private:
    void drawMainWindow();

    struct Popups { bool genText = false; bool url = false; };
    void drawMenuBar(Popups& outPopups) const;
    void processShortcuts(Popups& outPopups) const;
    void drawModals(const Popups& popups);
    void drawUrlModal() const;
    void drawGenTextModal() const;
    void drawInfoModal();
    void drawErrorModal();

    void drawProgress();

    void execCommand(const Command& command) const;

    bool m_Win32Initialized = false;
    bool m_Dx11Initialized = false;

    CommandHandler m_SendCommand;

    bool m_FileOpen = false;
    TextView m_TextView;

    InfoMsgData m_InfoMsgData;
    std::string m_ErrorMsg;

    struct ProgressData {
        std::string name;
        std::function<void()> cancelClickCallback = nullptr;

        bool cancelled = false;
        std::atomic<float> progress = -1.0f;
    };
    std::list<ProgressData> m_ProgressDataQueue;
public:
    struct ProgressRAII {
        ProgressRAII(std::list<ProgressData>* queuePtr, const std::list<ProgressData>::iterator& it);
        ~ProgressRAII();

        std::atomic<float>& getProgressTS() const;
        void setCancelled() const;
    private:
        std::list<ProgressData>* m_ProgressDataQueuePtr;
        std::list<ProgressData>::iterator m_Iterator;
    };
    std::unique_ptr<ProgressRAII> pushProgress(std::string name, std::function<void()> cancelClickCallback = nullptr);
};
