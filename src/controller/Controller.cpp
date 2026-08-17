#include "Controller.h"
#include "../utils/FileUtils.h"
#include "file/FileReader.h"
#include "file/FileWriter.h"
#include "gen/TextGenerator.h"
#include "indexer/LineIndexer.h"

Controller::Controller(Win32App &app, Ui& ui) : m_App(app), m_Ui(ui) {
    m_Ui.setCommandHandler([this] (const Command& command) { process(command); });
}

Controller::~Controller() {
    m_Ui.setCommandHandler(nullptr);
    m_Ui.setFileClosed();
    m_Ui.showInfoMsg({});
    m_Ui.showErrorMsg({});
}

void Controller::tick() {
    m_OpenJob.checkFinished();
    m_DownloadJob.checkFinished();
    m_GenJob.checkFinished();
    m_SaveJob.checkFinished();
    m_SearchJob.checkFinished();
}

void Controller::process(const Command& command) {
    std::visit(CommandVisitor{*this}, command);
}

void Controller::CommandVisitor::operator()(const cmd::OpenFile&) const {
    // ReSharper disable once CppLocalVariableMayBeConst
    if (auto filePathOpt = controller.m_App.showTextFileDialog(true))
        controller.openImpl(std::move(*filePathOpt));
}

void Controller::CommandVisitor::operator()(const cmd::OpenUrl& cmd) const {
    if (cmd.url.empty()) {
        controller.m_Ui.showErrorMsg("No URL provided");
        return;
    }
    controller.downloadImpl(cmd.url);
}

void Controller::CommandVisitor::operator()(const cmd::GenRandom& cmd) const {
    uint64_t bytes = 0, lines = 0;
    switch (cmd.type) {
        case cmd::GenRandom::Type::Kb: bytes = static_cast<uint64_t>(cmd.value) << 10; break;
        case cmd::GenRandom::Type::Mb: bytes = static_cast<uint64_t>(cmd.value) << 20; break;
        case cmd::GenRandom::Type::Gb: bytes = static_cast<uint64_t>(cmd.value) << 30; break;
        case cmd::GenRandom::Type::Lines: lines = cmd.value; break;
    }
    if (bytes == 0 && lines == 0) {
        controller.m_Ui.showErrorMsg("Nothing to generate");
        return;
    }
    controller.genImpl(bytes, lines);
}

void Controller::CommandVisitor::operator()(const cmd::SaveAs&) const {
    if (controller.m_OpenJob.isAlive()) {
        // ReSharper disable once CppLocalVariableMayBeConst
        if (auto targetFilePathOpt = controller.m_App.showTextFileDialog(false))
            controller.saveImpl(std::move(*targetFilePathOpt));
    }
}

void Controller::CommandVisitor::operator()(const cmd::Close&) const {
    controller.closeImpl();
}

void Controller::CommandVisitor::operator()(const cmd::Find& cmd) const {
    controller.findImpl(cmd);
}

void Controller::CommandVisitor::operator()(const cmd::CancelFind&) const {
    controller.m_SearchJob.cancel();
}

std::string_view Controller::getTextDataImpl(
    const uint64_t lineIdx, const uint64_t fromCol, const uint64_t colsNum,
    uint64_t& outLineTotalLength
) const {
    if (m_OpenJob.isAlive())
        return m_OpenJob.data().lineIndexerPtr->get(lineIdx, fromCol, colsNum, outLineTotalLength);
    outLineTotalLength = 0;
    return {};
}

void Controller::getTextDataSizeImpl(uint64_t& maxColsNum, uint64_t& rowsNum) const {
    if (m_OpenJob.isAlive()) {
        m_OpenJob.data().lineIndexerPtr->getTextSize(maxColsNum, rowsNum);
        return;
    }
    maxColsNum = rowsNum = 0;
}

bool Controller::isReadingFromTmp() const {
    return m_OpenJob.isAlive() && !m_App.getReadTmpTextFilePath().empty() && m_OpenJob.data().path == m_App.getReadTmpTextFilePath();
}

void Controller::removeTmpFiles(const bool r, const bool w) const {
    std::error_code ec;
    if (r && !m_App.getReadTmpTextFilePath().empty())
        std::filesystem::remove(m_App.getReadTmpTextFilePath(), ec);
    if (w && !m_App.getWriteTmpTextFilePath().empty())
        std::filesystem::remove(m_App.getWriteTmpTextFilePath(), ec);
}

void Controller::openGeneratedTmpFile() {
    // exchange tmp files
    if (!m_App.getReadTmpTextFilePath().empty() && !m_App.getWriteTmpTextFilePath().empty()) {
        std::error_code ec;
        std::filesystem::remove(m_App.getReadTmpTextFilePath(), ec);
        std::filesystem::rename(m_App.getWriteTmpTextFilePath(), m_App.getReadTmpTextFilePath(), ec);
    }
    openImpl(m_App.getReadTmpTextFilePath());
}

void Controller::openImpl(std::filesystem::path path) {
    if (m_OpenJob.isAlive()) {
        std::error_code ec;
        if (std::filesystem::equivalent(path, m_OpenJob.data().path, ec))
            return; // already open(ed/ing)
    }

    OpenPayload payload;
    {
        std::string errorMsg;
        payload.lineIndexerPtr = LineIndexer::create(path, errorMsg);
        if (!payload.lineIndexerPtr) {
            m_Ui.showErrorMsg(std::move(errorMsg));
            return;
        }
    }
    payload.path = std::move(path);

    if (!m_OpenJob.isAlive()) {
        startOpenJob(std::move(payload));
        return;
    }

    // std::function requires a copyable closure -> transform unique_ptr to shared_ptr
    closeImpl([this, sp = std::make_shared<OpenPayload>(std::move(payload))] {
        startOpenJob(std::move(*sp));
    });
}

void Controller::startOpenJob(OpenPayload&& payload) {
    assert(!m_OpenJob.isAlive());

    payload.progressPtr = m_Ui.pushProgress("Indexing");

    m_OpenJob.start(
        std::move(payload), &Controller::openJobRoutine,
        [this] (std::optional<std::string> errorOpt, const bool wasCancelled) {
            onOpenJobFinished(std::move(errorOpt), wasCancelled);
        }
    );

    const auto fileName = m_OpenJob.data().path.filename();
    m_App.setWindowTitle(fileName.c_str());
    m_Ui.setFileOpened({
        [this] (const uint64_t line, const uint64_t col, const uint64_t cols, uint64_t& outLen) {
            return getTextDataImpl(line, col, cols, outLen);
        },
        [this] (uint64_t& maxCols, uint64_t& rows) {
            getTextDataSizeImpl(maxCols, rows);
        }
    });
}

std::optional<std::string> Controller::openJobRoutine(const OpenPayload& d, const std::stop_token& st) {
    return d.lineIndexerPtr->startScan(st, d.progressPtr->getProgressTS());
}

// ReSharper disable once CppPassValueParameterByConstReference
void Controller::onOpenJobFinished(std::optional<std::string> errorOpt, const bool wasCancelled) {
    if (!m_OpenJob.isAlive())
        return;
    m_OpenJob.releaseTask();
    m_OpenJob.data().progressPtr.reset();

    if (wasCancelled) {
        closeImpl();
        return;
    }
    if (errorOpt.has_value())
        m_Ui.showErrorMsg(std::move(*errorOpt));
}

void Controller::downloadImpl(std::string url) {
    if (m_DownloadJob.isAlive()) {
        confirmJobCancelThen(
            "Downloading is already running.\nCancel it and start a new one?", m_DownloadJob,
            [this, u = std::move(url)] { downloadImpl(u); }
        );
        return;
    }
    if (m_GenJob.isAlive()) {
        confirmJobCancelThen(
            "The temp file is busy with Generate.\nCancel it and start the download?", m_GenJob,
            [this, u = std::move(url)] { downloadImpl(u); }
        );
        return;
    }

    const auto& tmpFilePath = m_App.getWriteTmpTextFilePath();
    if (tmpFilePath.empty()) {
        m_Ui.showErrorMsg("Cannot get temp text file path");
        return;
    }

    std::string errorMsg;
    auto writer = FileWriter::open(tmpFilePath, http::kWriteBufferSize, 0, errorMsg);
    if (!writer) {
        m_Ui.showErrorMsg(std::move(errorMsg));
        return;
    }

    DownloadPayload payload;
    payload.url = std::move(url);
    payload.targetPath = tmpFilePath;
    payload.writerPtr = std::move(writer);
    payload.responseInfo = http::ResponseInfo();
    payload.progressPtr = m_Ui.pushProgress("Downloading", [this] { cancelJob(m_DownloadJob); });

    m_DownloadJob.start(
        std::move(payload), &Controller::downloadJobRoutine,
        [this] (std::optional<std::string> errorOpt, const bool wasCancelled) {
            onDownloadJobFinished(std::move(errorOpt), wasCancelled);
        }
    );
}

std::optional<std::string> Controller::downloadJobRoutine(DownloadPayload& d, const std::stop_token& st) {
    return http::download(*d.writerPtr, st, d.progressPtr->getProgressTS(), d.responseInfo, d.url, d.targetPath);
}

// ReSharper disable once CppPassValueParameterByConstReference
void Controller::onDownloadJobFinished(std::optional<std::string> errorOpt, const bool wasCancelled) {
    if (!m_DownloadJob.isAlive())
        return;
    m_DownloadJob.clear();

    if (wasCancelled || errorOpt.has_value()) {
        removeTmpFiles(false, true);
        if (!wasCancelled && errorOpt.has_value())
            m_Ui.showErrorMsg(std::move(*errorOpt));
        return;
    }

    if (!m_OpenJob.isAlive()) {
        openGeneratedTmpFile();
        return;
    }

    m_Ui.showInfoMsg({"File has been downloaded. Open it?", [this] (const bool ok) {
        if (!ok) {
            removeTmpFiles(false, true);
            return;
        }
        closeImpl([this] { openGeneratedTmpFile(); });
    }});
}

void Controller::genImpl(const uint64_t targetBytes, const uint64_t targetLines) {
    if (m_GenJob.isAlive()) {
        confirmJobCancelThen(
            "Generation is already running.\nCancel it and start a new one?", m_GenJob,
            [this, targetBytes, targetLines] { genImpl(targetBytes, targetLines); }
        );
        return;
    }
    if (m_DownloadJob.isAlive()) {
        confirmJobCancelThen(
            "The temp file is busy with Download.\nCancel it and start generating?", m_DownloadJob,
            [this, targetBytes, targetLines] { genImpl(targetBytes, targetLines); }
        );
        return;
    }

    const auto& tmpFilePath = m_App.getWriteTmpTextFilePath();
    if (tmpFilePath.empty()) {
        m_Ui.showErrorMsg("Cannot get temp text file path");
        return;
    }

    if (targetBytes) {
        // ReSharper disable once CppLocalVariableMayBeConst
        if (auto errorOpt = utils::checkDiskSpace(tmpFilePath, targetBytes)) {
            m_Ui.showErrorMsg(std::move(*errorOpt));
            return;
        }
    }

    std::string errorMsg;
    auto writer = FileWriter::open(tmpFilePath, textgen::kWriteBufferSize, targetBytes, errorMsg);
    if (!writer) {
        m_Ui.showErrorMsg(std::move(errorMsg));
        return;
    }

    GenPayload payload{targetBytes, targetLines, std::move(writer)};
    payload.progressPtr = m_Ui.pushProgress("Generating", [this] { cancelJob(m_GenJob); });

    m_GenJob.start(
        std::move(payload), &Controller::genJobRoutine,
        [this] (std::optional<std::string> errorOpt, const bool wasCancelled) {
            onGenJobFinished(std::move(errorOpt), wasCancelled);
        }
    );
}

std::optional<std::string> Controller::genJobRoutine(const GenPayload& d, const std::stop_token& st) {
    return textgen::generate(*d.writerPtr, st, d.progressPtr->getProgressTS(), d.targetBytes, d.targetLines);
}

// ReSharper disable once CppPassValueParameterByConstReference
void Controller::onGenJobFinished(std::optional<std::string> errorOpt, const bool wasCancelled) {
    if (!m_GenJob.isAlive())
        return;
    m_GenJob.clear();

    if (wasCancelled || errorOpt.has_value()) {
        removeTmpFiles(false, true);
        if (!wasCancelled && errorOpt.has_value())
            m_Ui.showErrorMsg(std::move(*errorOpt));
        return;
    }

    if (!m_OpenJob.isAlive()) {
        openGeneratedTmpFile();
        return;
    }

    m_Ui.showInfoMsg({"File has been generated. Open it?", [this] (const bool ok) {
        if (!ok) {
            removeTmpFiles(false, true);
            return;
        }
        closeImpl([this] { openGeneratedTmpFile(); });
    }});
}

void Controller::saveImpl(std::filesystem::path targetPath) {
    if (!m_OpenJob.isAlive())
        return;

    std::error_code ec;
    const bool existedBefore = std::filesystem::exists(targetPath, ec);
    if (existedBefore && std::filesystem::equivalent(m_OpenJob.data().path, targetPath, ec)) {
        m_Ui.showErrorMsg("Source and destination are the same");
        return;
    }

    if (m_SaveJob.isAlive()) {
        confirmJobCancelThen(
            "Saving is already running.\nCancel it and start a new one?", m_SaveJob,
            [this, t = std::move(targetPath)] { saveImpl(t); }
        );
        return;
    }

    SavePayload payload{m_OpenJob.data().path, std::move(targetPath), existedBefore};
    payload.progressPtr = m_Ui.pushProgress("Saving", [this] { cancelJob(m_SaveJob); });

    m_SaveJob.start(
        std::move(payload), &Controller::saveJobRoutine,
        [this] (std::optional<std::string> errorOpt, const bool wasCancelled) {
            onSaveJobFinished(std::move(errorOpt), wasCancelled);
        }
    );
}

namespace {
    struct CopyProgressData {
        std::atomic<float>* progressPtr;
        const std::stop_token* stPtr;
        BOOL* cancelFlagPtr;
    };
    DWORD CALLBACK copyProgressRoutine(
        const LARGE_INTEGER totalFileSize, const LARGE_INTEGER totalBytesTransferred,
        LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE, const LPVOID lpData
    ) {
        if (const auto d = static_cast<CopyProgressData*>(lpData)) {
            if (d->stPtr->stop_requested()) {
                *d->cancelFlagPtr = TRUE;
                return PROGRESS_CANCEL;
            }
            if (totalFileSize.QuadPart > 0) {
                const float progress = static_cast<float>(totalBytesTransferred.QuadPart) / static_cast<float>(totalFileSize.QuadPart);
                d->progressPtr->store(progress, std::memory_order_relaxed);
            }
        }
        return PROGRESS_CONTINUE;
    }
}

std::optional<std::string> Controller::saveJobRoutine(const SavePayload& d, const std::stop_token& st) {
    BOOL cancelFlag = FALSE;
    CopyProgressData data{&d.progressPtr->getProgressTS(), &st, &cancelFlag};
    if (CopyFileExW(
        d.sourcePath.c_str(), d.targetPath.c_str(), &copyProgressRoutine,
        &data, &cancelFlag, 0 // overwrite allowed: the save dialog already asked
    ))
        return std::nullopt;
    return "Cannot save the file";
}

// ReSharper disable once CppPassValueParameterByConstReference
void Controller::onSaveJobFinished(std::optional<std::string> errorOpt, const bool wasCancelled) {
    if (!m_SaveJob.isAlive())
        return;

    const auto targetPath = m_SaveJob.data().targetPath;
    const bool existedBefore = m_SaveJob.data().existedBefore;
    m_SaveJob.clear();

    if (!wasCancelled && !errorOpt.has_value())
        return;

    if (!existedBefore) {
        std::error_code ec;
        std::filesystem::remove(targetPath, ec);
    }
    if (!wasCancelled && errorOpt.has_value())
        m_Ui.showErrorMsg(std::move(*errorOpt));
}

void Controller::findImpl(cmd::Find cmd) {
    if (!m_OpenJob.isAlive() || cmd.needle.empty())
        return;

    if (m_SearchJob.isAlive()) {
        m_SearchJob.cancel([this, c = std::move(cmd)] () mutable { findImpl(std::move(c)); });
        return;
    }

    const auto& indexer = *m_OpenJob.data().lineIndexerPtr;

    search::Request request;
    request.needle = std::move(cmd.needle);
    request.backwards = cmd.backwards;
    request.contentStartOffsetBytes = indexer.getContentStartOffsetBytes();
    request.endOffsetBytes = indexer.getScannedBytes(); // searching during indexing covers what was indexed by now

    if (cmd.continueFromCurrentMatch && m_CurrentMatchOffsetOpt.has_value()) {
        request.startOffsetBytes = cmd.backwards ? *m_CurrentMatchOffsetOpt : *m_CurrentMatchOffsetOpt + 1;
    }
    else {
        const uint64_t lineStartOffsetBytes = indexer.computeLineOffsetBytes(cmd.fromLineIdx);
        request.startOffsetBytes = lineStartOffsetBytes == UINT64_MAX ? request.contentStartOffsetBytes : lineStartOffsetBytes;
    }

    std::string errorMsg;
    auto reader = FileReader::open(m_OpenJob.data().path, search::kReadBufferSize, errorMsg);
    if (!reader) {
        m_Ui.showErrorMsg(std::move(errorMsg));
        return;
    }

    SearchPayload payload;
    payload.readerPtr = std::move(reader);
    payload.request = std::move(request);
    payload.progressPtr = m_Ui.beginSearch();

    m_SearchJob.start(
        std::move(payload), &Controller::searchJobRoutine,
        [this] (std::optional<std::string> errorOpt, const bool wasCancelled) {
            onSearchJobFinished(std::move(errorOpt), wasCancelled);
        }
    );
}

std::optional<std::string> Controller::searchJobRoutine(SearchPayload& d, const std::stop_token& st) {
    return search::find(*d.readerPtr, st, d.progressPtr->getProgressTS(), d.request, d.result);
}

// ReSharper disable once CppPassValueParameterByConstReference
void Controller::onSearchJobFinished(std::optional<std::string> errorOpt, const bool wasCancelled) {
    if (!m_SearchJob.isAlive())
        return;

    const auto result = m_SearchJob.data().result;
    m_SearchJob.clear();

    if (wasCancelled || errorOpt.has_value()) {
        m_Ui.setSearchIdle();
        if (!wasCancelled && errorOpt.has_value())
            m_Ui.showErrorMsg(std::move(*errorOpt));
        return;
    }

    uint64_t colIdx = 0, lineIdx = 0;
    if (!result.found || !m_OpenJob.isAlive() || !m_OpenJob.data().lineIndexerPtr->locate(result.offsetBytes, colIdx, lineIdx)) {
        m_CurrentMatchOffsetOpt.reset();
        m_Ui.setSearchResult(false);
        return;
    }

    m_CurrentMatchOffsetOpt = result.offsetBytes;
    m_Ui.setSearchResult(true);
    m_Ui.showSearchMatch(colIdx, lineIdx);
}

void Controller::closeImpl(std::function<void()> deferredFunc) {
    if (!m_OpenJob.isAlive()) {
        if (deferredFunc)
            deferredFunc();
        return;
    }

    if (isReadingFromTmp() && m_SaveJob.isAlive()) {
        confirmJobCancelThen(
            "The file you are closing is being saved.\nCancel saving?", m_SaveJob,
            [this, def = std::move(deferredFunc)] { closeImpl(def); }
        );
        return;
    }

    if (m_SearchJob.isAlive()) {
        m_SearchJob.cancel([this, def = std::move(deferredFunc)] { closeImpl(def); });
        return;
    }

    if (m_OpenJob.isRunning()) {
        // still indexing
        cancelJob(m_OpenJob, std::move(deferredFunc));
        return;
    }

    const bool wasReadingFromTmp = isReadingFromTmp();

    m_CurrentMatchOffsetOpt.reset();
    m_Ui.setFileClosed();
    m_App.setWindowTitle(std::nullopt);
    m_OpenJob.clear();

    if (wasReadingFromTmp)
        removeTmpFiles(true, false);

    if (deferredFunc)
        deferredFunc();
}
