#pragma once

#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include "../controller/Commands.h"
#include "../controller/search/TextSearcher.h"
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

    void setSearchResult(bool found);
    void setSearchIdle();
    void showSearchMatch(const uint64_t colIdx, const uint64_t lineIdx) { m_TextView.showMatch(colIdx, lineIdx); }

    struct InfoMsgData { std::string infoMsg; std::function<void(bool)> callback;};
    void showInfoMsg(InfoMsgData msg) { m_InfoMsgData = std::move(msg); }

    void showErrorMsg(std::string msg) { m_ErrorMsg = std::move(msg); }
private:
    void drawMainWindow();

    struct Popups { bool genText = false; bool url = false; };
    void drawMenuBar(Popups& outPopups);
    void processShortcuts(Popups& outPopups);
    void drawModals(const Popups& popups);
    void drawUrlModal();
    void drawGenTextModal();
    void drawInfoModal();
    void drawErrorModal();
    void drawSearchPanel();
    void drawProgress();

    void openSearchPanel();
    void closeSearchPanel();
    void sendFindCommand(bool backwards);

    void execCommand(const Command& command) const;

    bool m_Win32Initialized = false;
    bool m_Dx11Initialized = false;

    CommandHandler m_SendCommand;

    bool m_FileOpen = false;
    TextView m_TextView;

    InfoMsgData m_InfoMsgData;
    std::string m_ErrorMsg;

    static constexpr float kUrlFieldWidthEm = 24.0f;
    char m_UrlBuf[2048] = "";

    static constexpr const char* kGenTextUnits[] = {"Kb", "Mb", "Gb", "Lines"};
    struct GenTextData {
        int value = 1;
        int unitIdx = 1; // kGenTextUnits
    } m_GenTextData;

    static constexpr float kSearchFieldWidthEm = 20.0f;
    struct SearchData {
        bool panelOpen = false;
        bool focusRequested = false;
        char needleBuf[search::kMaxNeedleBytes] = "";

        bool running = false;
        std::atomic<float> progress = 0.0f;
        std::optional<bool> resultOpt;
    } m_SearchData;

    static constexpr float kProgressBarWidth = 320.0f;
    struct ProgressData {
        std::string name;
        std::function<void()> cancelClickCallback = nullptr;

        bool cancelled = false;
        std::atomic<float> progress = -1.0f;
    };
    std::list<ProgressData> m_ProgressDataQueue;

    static constexpr float kFloatingWindowMargin = 24.0f;
public:
    struct ProgressRAII {
        ProgressRAII(std::list<ProgressData>* queuePtr, const std::list<ProgressData>::iterator& it);
        ~ProgressRAII();

        ProgressRAII(const ProgressRAII&) = delete;
        ProgressRAII& operator=(const ProgressRAII&) = delete;
        ProgressRAII(ProgressRAII&&) noexcept = delete;
        ProgressRAII& operator=(ProgressRAII&&) noexcept = delete;

        std::atomic<float>& getProgressTS() const;
        void setCancelled() const;
    private:
        std::list<ProgressData>* m_ProgressDataQueuePtr;
        std::list<ProgressData>::iterator m_Iterator;
    };
    std::unique_ptr<ProgressRAII> pushProgress(std::string name, std::function<void()> cancelClickCallback = nullptr);

    struct SearchProgressRAII {
        explicit SearchProgressRAII(SearchData* searchDataPtr);
        ~SearchProgressRAII();

        SearchProgressRAII(const SearchProgressRAII&) = delete;
        SearchProgressRAII& operator=(const SearchProgressRAII&) = delete;
        SearchProgressRAII(SearchProgressRAII&&) noexcept = delete;
        SearchProgressRAII& operator=(SearchProgressRAII&&) noexcept = delete;

        std::atomic<float>& getProgressTS() const;
    private:
        SearchData* m_SearchDataPtr;
    };
    std::unique_ptr<SearchProgressRAII> beginSearch();
};
