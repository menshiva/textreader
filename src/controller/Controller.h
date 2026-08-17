#pragma once

#include "../app/Win32App.h"
#include "../ui/Ui.h"
#include "async_task/Job.h"
#include "net/TextDownloader.h"
#include "search/TextSearcher.h"

class LineIndexer;
class FileWriter;
class FileReader;

class Controller {
public:
    Controller(Win32App& app, Ui& ui);
    ~Controller();

    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    Controller(Controller&&) noexcept = delete;
    Controller& operator=(Controller&&) noexcept = delete;

    void process(const Command& command);
    void tick();
private:
    struct CommandVisitor {
        Controller& controller;

        void operator()(const cmd::OpenFile&) const;
        void operator()(const cmd::OpenUrl& cmd) const;
        void operator()(const cmd::GenRandom& cmd) const;
        void operator()(const cmd::SaveAs&) const;
        void operator()(const cmd::Close&) const;
        void operator()(const cmd::Find& cmd) const;
        void operator()(const cmd::CancelFind&) const;
    };

    std::string_view getTextDataImpl(uint64_t lineIdx, uint64_t fromCol, uint64_t colsNum, uint64_t& outLineTotalLength) const;
    void getTextDataSizeImpl(uint64_t& maxColsNum, uint64_t& rowsNum) const;

    struct TextSource final : TextView::Source {
        explicit TextSource(const Controller& owner) : controller(owner) {}

        std::string_view getLine(uint64_t lineIdx, uint64_t fromCol, uint64_t colsNum, uint64_t& outLineTotalLength) const override;
        void getTextSize(uint64_t& maxColsNum, uint64_t& rowsNum) const override;

        const Controller& controller;
    };
    TextSource m_TextSource{*this};

    bool isReadingFromTmp() const;
    void removeTmpFiles(bool r, bool w) const;
    void openGeneratedTmpFile();

    Win32App& m_App;
    Ui& m_Ui;

    struct PendingAction { std::function<bool()> isReady; std::function<void()> run; };
    std::optional<PendingAction> m_PendingActionOpt;

    void setPendingAction(std::function<bool()> isReady, std::function<void()> run);
    void runPendingAction();

    template <typename Payload>
    static void cancelJob(Job<Payload>& job) {
        if (job.hasData() && job.data().progressPtr)
            job.data().progressPtr->setCancelled();
        job.cancel();
    }

    template <typename Payload>
    void confirmJobCancelThen(std::string question, Job<Payload>& job, std::function<void()> then) {
        m_Ui.showMessage({std::move(question), [this, &job, t = std::move(then)] (const bool ok) mutable {
            if (!ok) {
                m_PendingActionOpt.reset();
                return;
            }
            cancelJob(job);
            setPendingAction([&job] { return !job.isRunning(); }, std::move(t));
        }});
    }

    struct OpenPayload {
        std::filesystem::path path;
        std::unique_ptr<LineIndexer> lineIndexerPtr;
        std::unique_ptr<Ui::ProgressRAII> progressPtr;
    };
    Job<OpenPayload> m_OpenJob;

    void openImpl(std::filesystem::path path);
    void startOpenJob(OpenPayload&& payload);
    static std::optional<std::string> openJobRoutine(const OpenPayload& d, const std::stop_token& st);
    void onOpenJobFinished(std::optional<std::string> errorOpt, bool wasCancelled);

    struct DownloadPayload {
        std::string url;
        std::filesystem::path targetPath;
        std::unique_ptr<FileWriter> writerPtr;
        http::ResponseInfo responseInfo;
        std::unique_ptr<Ui::ProgressRAII> progressPtr;
    };
    Job<DownloadPayload> m_DownloadJob;

    void downloadImpl(std::string url);
    static std::optional<std::string> downloadJobRoutine(DownloadPayload& d, const std::stop_token& st);
    void onDownloadJobFinished(std::optional<std::string> errorOpt, bool wasCancelled);

    struct GenPayload {
        uint64_t targetBytes = 0;
        uint64_t targetLines = 0;
        std::unique_ptr<FileWriter> writerPtr;
        std::unique_ptr<Ui::ProgressRAII> progressPtr;
    };
    Job<GenPayload> m_GenJob;

    void genImpl(uint64_t targetBytes, uint64_t targetLines);
    static std::optional<std::string> genJobRoutine(const GenPayload& d, const std::stop_token& st);
    void onGenJobFinished(std::optional<std::string> errorOpt, bool wasCancelled);

    struct SavePayload {
        std::filesystem::path sourcePath;
        std::filesystem::path targetPath;
        bool existedBefore = false;
        std::unique_ptr<Ui::ProgressRAII> progressPtr;
    };
    Job<SavePayload> m_SaveJob;

    void saveImpl(std::filesystem::path targetPath);
    static std::optional<std::string> saveJobRoutine(const SavePayload& d, const std::stop_token& st);
    void onSaveJobFinished(std::optional<std::string> errorOpt, bool wasCancelled);

    struct SearchPayload {
        std::unique_ptr<FileReader> readerPtr;
        search::Request request;
        search::Result result;
        std::unique_ptr<Ui::SearchProgressRAII> progressPtr;
    };
    Job<SearchPayload> m_SearchJob;
    std::optional<uint64_t> m_CurrentMatchOffsetOpt;

    void findImpl(cmd::Find cmd);
    static std::optional<std::string> searchJobRoutine(SearchPayload& d, const std::stop_token& st);
    void onSearchJobFinished(std::optional<std::string> errorOpt, bool wasCancelled);

    void closeImpl();
};
