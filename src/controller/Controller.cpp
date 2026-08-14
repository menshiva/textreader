#include "Controller.h"
#include "../utils/Random.h"
#include "file/FileWriter.h"
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

void Controller::tick() const {
    m_OpenJob.checkFinished();
    m_DownloadJob.checkFinished();
    m_GenJob.checkFinished();
    m_SaveJob.checkFinished();
}

void Controller::operator()(const cmd::OpenFile&) {
    // ReSharper disable once CppLocalVariableMayBeConst
    if (auto filePathOpt = m_App.showTextFileDialog(true))
        openImpl(std::move(*filePathOpt));
}

void Controller::operator()(const cmd::OpenUrl& cmd) {
    if (cmd.url.empty()) {
        m_Ui.showErrorMsg("No URL provided");
        return;
    }
    downloadImpl(cmd.url);
}

void Controller::operator()(const cmd::GenRandom& cmd) {
    uint64_t bytes = 0, lines = 0;
    switch (cmd.type) {
        case cmd::GenRandom::Type::Kb: bytes = static_cast<uint64_t>(cmd.value) << 10; break;
        case cmd::GenRandom::Type::Mb: bytes = static_cast<uint64_t>(cmd.value) << 20; break;
        case cmd::GenRandom::Type::Gb: bytes = static_cast<uint64_t>(cmd.value) << 30; break;
        case cmd::GenRandom::Type::Lines: lines = cmd.value; break;
    }
    if (bytes == 0 && lines == 0) {
        m_Ui.showErrorMsg("Nothing to generate");
        return;
    }
    genImpl(bytes, lines);
}

void Controller::operator()(const cmd::SaveAs&) {
    if (m_OpenJob.isAlive()) {
        // ReSharper disable once CppLocalVariableMayBeConst
        if (auto targetFilePathOpt = m_App.showTextFileDialog(false))
            saveImpl(std::move(*targetFilePathOpt));
    }
}

void Controller::operator()(const cmd::Close&) {
    closeImpl();
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

void Controller::exchangeTmpFiles() const {
    if (!m_App.getReadTmpTextFilePath().empty() && !m_App.getWriteTmpTextFilePath().empty()) {
        std::error_code ec;
        std::filesystem::remove(m_App.getReadTmpTextFilePath(), ec);
        std::filesystem::rename(m_App.getWriteTmpTextFilePath(), m_App.getReadTmpTextFilePath(), ec);
    }
}

void Controller::openGeneratedTmpFile() {
    exchangeTmpFiles();
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

    m_OpenJob.start(
        std::move(payload),
        m_Ui.pushProgress("Indexing", false),
        &Controller::openJobRoutine,
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

std::optional<std::string> Controller::openJobRoutine(const OpenPayload& d, const std::stop_token& st, std::atomic<float>& progress) {
    d.lineIndexerPtr->startScan(st, progress);
    return std::nullopt; // TODO: make startScan return an error
}

// ReSharper disable once CppPassValueParameterByConstReference
void Controller::onOpenJobFinished(std::optional<std::string> errorOpt, const bool wasCancelled) {
    if (!m_OpenJob.isAlive())
        return;
    m_OpenJob.releaseTask();

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

    // TODO: writer
    DownloadPayload payload;
    payload.url = std::move(url);

    m_DownloadJob.start(
        std::move(payload),
        m_Ui.pushProgress("Downloading", false, [this] { m_DownloadJob.cancel(); }),
        &Controller::downloadJobRoutine,
        [this] (std::optional<std::string> errorOpt, const bool wasCancelled) {
            onDownloadJobFinished(std::move(errorOpt), wasCancelled);
        }
    );
}

std::optional<std::string> Controller::downloadJobRoutine(const DownloadPayload& d, const std::stop_token& st, std::atomic<float>& progress) {
    // TODO
    return std::nullopt;
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

namespace {
    constexpr size_t kGenFileWriteBuffSize = 1ull << 20;  // 1 mb
    constexpr char kGenAlphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .";
    constexpr uint32_t kGenLineLen = 1024;
    constexpr size_t kGenPoolSize = 64ull << 10;       // 64 kb
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

    auto writer = FileWriter::open(tmpFilePath, kGenFileWriteBuffSize, targetBytes);
    if (!writer) {
        m_Ui.showErrorMsg("Cannot create temp text file for writing");
        return;
    }

    m_GenJob.start(
        GenPayload{targetBytes, targetLines, std::move(writer)},
        m_Ui.pushProgress("Generating", false, [this] { m_GenJob.cancel(); }),
        &Controller::genJobRoutine,
        [this] (std::optional<std::string> errorOpt, const bool wasCancelled) {
            onGenJobFinished(std::move(errorOpt), wasCancelled);
        }
    );
}

std::optional<std::string> Controller::genJobRoutine(const GenPayload& d, const std::stop_token& st, std::atomic<float>& progress) {
    bool error = false;
    XorShift32 rnd;
    constexpr uint32_t alphabetLen = sizeof(kGenAlphabet) - 1;

    std::vector<char> pool(kGenPoolSize);
    for (size_t i = 0; i < kGenPoolSize; ++i)
        pool[i] = kGenAlphabet[rnd.next() % alphabetLen];
    constexpr size_t poolSpan = kGenPoolSize - kGenLineLen;

    char* const begin = d.writer->getBuffer();
    const char* const end = begin + kGenFileWriteBuffSize;
    char* p = begin;

    const auto flushBuffer = [&](const float percent, const size_t count = kGenFileWriteBuffSize) -> bool {
        if (!d.writer->submit(count))
            return false;
        progress.store(percent, std::memory_order_relaxed);
        p = begin;
        return true;
    };
    const auto appendChunkAndFlush = [&](uint32_t left, const float percent) -> bool {
        while (left > 0) {
            if (p == end && !flushBuffer(percent))
                return false;
            const uint32_t chunk = std::min<uint32_t>(static_cast<uint32_t>(end - p), left);
            memcpy(p, pool.data() + rnd.next() % poolSpan, chunk);
            p += chunk;
            left -= chunk;
        }
        if (p == end && !flushBuffer(percent))
            return false;
        *p++ = '\r';
        if (p == end && !flushBuffer(percent))
            return false;
        *p++ = '\n';
        return true;
    };

    if (d.targetBytes) {
        const uint64_t targetBytes = d.targetBytes;
        const auto targetBytesFlt = static_cast<float>(targetBytes);
        uint64_t remaining = targetBytes;
        while (remaining >= 2) {
            if (st.stop_requested())
                break;

            // handle \r\n
            const auto maxLen = static_cast<uint32_t>(
                remaining - 2 < kGenLineLen - 1 ? remaining - 2 : kGenLineLen - 1);
            uint32_t len = rnd.next() % kGenLineLen;
            if (len > maxLen) len = maxLen;
            if (remaining - 2 - len == 1) ++len;

            if (!appendChunkAndFlush(len, static_cast<float>(targetBytes - remaining) / targetBytesFlt)) {
                error = true;
                break;
            }
            remaining -= len + 2;
        }
    }
    else {
        const uint64_t targetLines = d.targetLines;
        const auto targetLinesFlt = static_cast<float>(targetLines);
        for (uint64_t n = 0; n < targetLines; ++n) {
            if (st.stop_requested())
                break;

            const uint32_t left = rnd.next() % kGenLineLen;
            if (!appendChunkAndFlush(left, static_cast<float>(n) / targetLinesFlt)) {
                error = true;
                break;
            }
        }
    }

    if (!d.writer->finish(static_cast<size_t>(p - begin)))
        error = true;

    if (error)
        return "Cannot write the generated file (out of disk space?)";

    progress.store(1.0f, std::memory_order_relaxed);
    return std::nullopt;
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

    m_SaveJob.start(
        SavePayload{m_OpenJob.data().path, std::move(targetPath), existedBefore},
        m_Ui.pushProgress("Saving", false, [this] { m_SaveJob.cancel(); }),
        &Controller::saveJobRoutine,
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

std::optional<std::string> Controller::saveJobRoutine(const SavePayload& d, const std::stop_token& st, std::atomic<float>& progress) {
    BOOL cancelFlag = FALSE;
    CopyProgressData data{&progress, &st, &cancelFlag};
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

    if (m_OpenJob.isRunning()) {
        // still indexing
        m_OpenJob.cancel(std::move(deferredFunc));
        return;
    }

    const bool wasReadingFromTmp = isReadingFromTmp();

    m_Ui.setFileClosed();
    m_App.setWindowTitle(std::nullopt);
    m_OpenJob.clear();

    if (wasReadingFromTmp)
        removeTmpFiles(true, false);

    if (deferredFunc)
        deferredFunc();
}
