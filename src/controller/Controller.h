#pragma once

#include "../app/Win32App.h"
#include "../ui/Ui.h"
#include "async_task/Job.h"

class LineIndexer;
class AsyncTask;
class FileWriter;

class Controller {
public:
    Controller(Win32App& app, Ui& ui);
    ~Controller();

    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    Controller(Controller&&) noexcept = delete;
    Controller& operator=(Controller&&) noexcept = delete;

    void process(const Command& command) { std::visit(*this, command); }
    void tick() const;

    void operator()(const cmd::OpenFile&);
    void operator()(const cmd::OpenUrl&);
    void operator()(const cmd::GenRandom& cmd);
    void operator()(const cmd::SaveAs&);
    void operator()(const cmd::Close&);
private:
    std::string_view getTextDataImpl(uint64_t lineIdx, uint64_t fromCol, uint64_t colsNum, uint64_t& outLineTotalLength) const;
    void getTextDataSizeImpl(uint64_t& maxColsNum, uint64_t& rowsNum) const;

    bool isReadingFromTmp() const;
    void removeTmpFiles(bool r, bool w) const;
    void exchangeTmpFiles() const;
    void openGeneratedTmpFile();

    template <typename Payload>
    void confirmJobCancelThen(std::string question, Job<Payload>& job, std::function<void()> then) {
        m_Ui.showInfoMsg({std::move(question), [&job, t = std::move(then)] (const bool ok) mutable {
            if (ok)
                job.cancel(std::move(t));
        }});
    }

    struct OpenPayload {
        std::filesystem::path path;
        std::unique_ptr<LineIndexer> lineIndexerPtr;
    };
    Job<OpenPayload> m_OpenJob;

    void openImpl(std::filesystem::path path);
    void startOpenJob(OpenPayload&& payload);
    static std::optional<std::string> openJobRoutine(const OpenPayload& d, const std::stop_token& st, std::atomic<float>& progress);
    void onOpenJobFinished(std::optional<std::string> errorOpt, bool wasCancelled);

    struct DownloadPayload {
        std::string url;
        std::unique_ptr<FileWriter> writer;
    };
    Job<DownloadPayload> m_DownloadJob;

    void downloadImpl(std::string url);
    static std::optional<std::string> downloadJobRoutine(const DownloadPayload& d, const std::stop_token& st, std::atomic<float>& progress);
    void onDownloadJobFinished(std::optional<std::string> errorOpt, bool wasCancelled);

    struct GenPayload {
        uint64_t targetBytes = 0;
        uint64_t targetLines = 0;
        std::unique_ptr<FileWriter> writer;
    };
    Job<GenPayload> m_GenJob;

    void genImpl(uint64_t targetBytes, uint64_t targetLines);
    static std::optional<std::string> genJobRoutine(const GenPayload& d, const std::stop_token& st, std::atomic<float>& progress);
    void onGenJobFinished(std::optional<std::string> errorOpt, bool wasCancelled);

    struct SavePayload {
        std::filesystem::path sourcePath;
        std::filesystem::path targetPath;
        bool existedBefore = false;
    };
    Job<SavePayload> m_SaveJob;

    void saveImpl(std::filesystem::path targetPath);
    static std::optional<std::string> saveJobRoutine(const SavePayload& d, const std::stop_token& st, std::atomic<float>& progress);
    void onSaveJobFinished(std::optional<std::string> errorOpt, bool wasCancelled);

    void closeImpl(std::function<void()> deferredFunc = nullptr);

    Win32App& m_App;
    Ui& m_Ui;
};
