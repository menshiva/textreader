#include "Controller.h"
#include "../utils/Random.h"
#include "../utils/ScopedProfiler.h"
#include "file/FileWriter.h"

Controller::Controller(Win32App &app, Ui& ui) : m_App(app), m_Ui(ui), m_LineIndexer(m_File) {
    m_Ui.setCommandHandler(std::bind(&Controller::process, this, std::placeholders::_1));
}

Controller::~Controller() {
    m_TextGenerationTask.cancelled.store(true, std::memory_order_release);
    if (m_TextGenerationTask.thread.joinable())
        m_TextGenerationTask.thread.join();
    m_SaveAsTask.cancelled.store(true, std::memory_order_release);
    if (m_SaveAsTask.thread.joinable())
        m_SaveAsTask.thread.join();

    m_Ui.setCommandHandler(nullptr);
    closeFileImpl(true);
}

void Controller::tick() {
    if (m_TextGenerationTask.isRunning() && m_TextGenerationTask.isFinished())
        endGenFileImpl();
    if (m_SaveAsTask.isRunning() && m_SaveAsTask.isFinished())
        endSaveAsImpl();
}

bool Controller::operator()(const cmd::Cancel&) {
    if (m_TextGenerationTask.isRunning()) {
        m_TextGenerationTask.cancel();
        return true;
    }
    if (m_SaveAsTask.isRunning()) {
        m_SaveAsTask.cancel();
        return true;
    }
    return false;
}

bool Controller::operator()(const cmd::OpenFile&) {
    if (isBusy())
        return false;

    const auto filePathOpt = m_App.showTextFileDialog(true);
    if (filePathOpt.has_value())
        return openFileImpl(filePathOpt.value());

    return false;
}

bool Controller::operator()(const cmd::OpenUrl&) {
    if (isBusy())
        return false;

    // TODO
    return true;
}

bool Controller::operator()(const cmd::GenRandom& cmd) {
    if (isBusy())
        return false;
    uint64_t bytes = 0, lines = 0;
    switch (cmd.type) {
        case cmd::GenRandom::Type::Kb: bytes = static_cast<uint64_t>(cmd.value) << 10; break;
        case cmd::GenRandom::Type::Mb: bytes = static_cast<uint64_t>(cmd.value) << 20; break;
        case cmd::GenRandom::Type::Gb: bytes = static_cast<uint64_t>(cmd.value) << 30; break;
        case cmd::GenRandom::Type::Lines: lines = cmd.value; break;
    }
    return startGenFileImpl(bytes, lines);
}

bool Controller::operator()(const cmd::SaveAs&) {
    if (isBusy() || m_File.getPath().empty())
        return false;

    const auto filePathOpt = m_App.showTextFileDialog(false);
    if (filePathOpt.has_value())
        return startSaveAsImpl(filePathOpt.value());

    return false;
}

bool Controller::operator()(const cmd::Close&) {
    if (isBusy() || m_File.getPath().empty())
        return false;
    closeFileImpl(true);
    return true;
}

bool Controller::isBusy() const {
    return m_TextGenerationTask.isRunning() || m_SaveAsTask.isRunning();
}

bool Controller::isReadingFromTmp() const {
    return m_File.getPath() == m_App.getTmpTextFilePath();
}

std::string_view Controller::getTextDataImpl(
    const uint64_t lineIdx, const uint64_t fromCol, const uint64_t maxCols,
    uint64_t& outLineTotalLength
) const {
    return m_LineIndexer.get(lineIdx, fromCol, maxCols, outLineTotalLength);
}

bool Controller::openFileImpl(const std::filesystem::path& path) {
    if (path == m_File.getPath())
        return false;

    const bool wasReadingTmp = isReadingFromTmp();
    if (!m_File.open(path)) {
        m_Ui.showErrorMsg("Cannot open the file");
        return false;
    }
    m_LineIndexer.build();
    if (wasReadingTmp) {
        std::error_code ec;
        std::filesystem::remove(m_App.getTmpTextFilePath(), ec);
    }

    const auto fileName = path.filename();
    m_App.setWindowTitle(fileName.c_str());
    m_Ui.setFileOpened({
        std::bind(&Controller::getTextDataImpl, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4),
        {m_LineIndexer.maxLineLength(), m_LineIndexer.count()}
    });

    return true;
}

void Controller::closeFileImpl(const bool removeTmp) {
    m_App.setWindowTitle(std::nullopt);
    m_Ui.setFileClosed();

    const bool wasReadingTmp = isReadingFromTmp();
    m_LineIndexer.clear();
    m_File.close();
    if (removeTmp && wasReadingTmp) {
        std::error_code ec;
        std::filesystem::remove(m_App.getTmpTextFilePath(), ec);
    }
}

bool Controller::startGenFileImpl(const uint64_t targetBytes, const uint64_t targetLines) {
    if (!targetBytes && !targetLines)
        return false;

    const auto& tmpFilePath = m_App.getTmpTextFilePath();
    if (tmpFilePath.empty()) {
        m_Ui.showErrorMsg("Cannot get temp text file path");
        return false;
    }

    if (isReadingFromTmp())
        closeFileImpl(false);

    auto fileWriterPtr = FileWriter::open(tmpFilePath, TextGenerationTask::kFileWriteBuffSize, targetBytes);
    if (!fileWriterPtr) {
        m_Ui.showErrorMsg("Cannot create temp text file");
        return false;
    }

    m_TextGenerationTask.reset();
    m_TextGenerationTask.progress = m_Ui.acquireProgressPopup("Generating", false);
    if (!m_TextGenerationTask.progress) {
        m_Ui.showErrorMsg("Already busy");
        return false;
    }

    m_TextGenerationTask.thread = std::jthread([taskPtr = &m_TextGenerationTask, w = std::move(fileWriterPtr), targetBytes, targetLines] {
        XorShift32 rnd;

        static constexpr uint32_t alphabetLen = sizeof(TextGenerationTask::kGenAlphabet) - 1;

        // TODO: profile
        {
            ScopedProfiler sw("pool");

            std::vector<char> pool(TextGenerationTask::kGenPoolSize);
            for (size_t i = 0; i < TextGenerationTask::kGenPoolSize; ++i)
                pool[i] = TextGenerationTask::kGenAlphabet[rnd.next() % alphabetLen];
            static constexpr size_t poolSpan = TextGenerationTask::kGenPoolSize - TextGenerationTask::kGenLineLen;

            char* begin = w->getBuffer();
            const char* end = begin + TextGenerationTask::kFileWriteBuffSize;
            char* p = begin;

            const auto flushBuffer = [&] (const float percent, const size_t count = TextGenerationTask::kFileWriteBuffSize) -> bool {
                if (w->submit(count)) {
                    taskPtr->progress->setProgressTS(percent);
                    p = begin;
                    return true;
                }
                return false;
            };
            const auto appendChunkAndFlush = [&] (uint32_t left, const float percent) -> bool {
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

            if (targetBytes) {
                const float targetBytesFlt = static_cast<float>(targetBytes);
                uint64_t remaining = targetBytes;
                while (remaining >= 2) {
                    if (taskPtr->cancelled.load(std::memory_order_relaxed)) {
                        taskPtr->result = AsyncTask::Result::Cancelled;
                        break;
                    }

                    // handle \r\n
                    const uint32_t maxLen = static_cast<uint32_t>(remaining - 2 < TextGenerationTask::kGenLineLen - 1 ? remaining - 2 : TextGenerationTask::kGenLineLen - 1);
                    uint32_t len = rnd.next() % TextGenerationTask::kGenLineLen;
                    if (len > maxLen) len = maxLen;
                    if (remaining - 2 - len == 1) ++len;

                    if (!appendChunkAndFlush(len, static_cast<float>(targetBytes - remaining) / targetBytesFlt)) {
                        taskPtr->result = AsyncTask::Result::Failed;
                        break;
                    }
                    remaining -= len + 2;
                }
            }
            else {
                const float targetLinesFlt = static_cast<float>(targetLines);
                for (uint64_t n = 0; n < targetLines; ++n) {
                    if (taskPtr->cancelled.load(std::memory_order_relaxed)) {
                        taskPtr->result = AsyncTask::Result::Cancelled;
                        break;
                    }

                    const uint32_t left = rnd.next() % TextGenerationTask::kGenLineLen;
                    if (!appendChunkAndFlush(left, static_cast<float>(n) / targetLinesFlt)) {
                        taskPtr->result = AsyncTask::Result::Failed;
                        break;
                    }
                }
            }

            if (!w->finish(static_cast<size_t>(p - begin)))
                if (taskPtr->result == AsyncTask::Result::None)
                    taskPtr->result = AsyncTask::Result::Failed;
        }

        if (taskPtr->result == AsyncTask::Result::None) {
            if (taskPtr->cancelled.load(std::memory_order_relaxed))
                taskPtr->result = AsyncTask::Result::Cancelled;
            else
                taskPtr->result = AsyncTask::Result::Success;
        }
        taskPtr->finish();
    });

    return true;
}

void Controller::endGenFileImpl() {
    if (!m_TextGenerationTask.isRunning())
        return;

    if (m_TextGenerationTask.thread.joinable())
        m_TextGenerationTask.thread.join();

    const auto result = m_TextGenerationTask.result;
    m_TextGenerationTask.reset();

    const auto& tmpFilePath = m_App.getTmpTextFilePath();
    if (tmpFilePath.empty()) {
        m_Ui.showErrorMsg("Cannot get temp text file path");
        return;
    }

    if (result == AsyncTask::Result::Success) {
        openFileImpl(tmpFilePath);
        return;
    }

    if (result == AsyncTask::Result::Failed)
        m_Ui.showErrorMsg("Cannot create temp text file");

    std::error_code ec;
    std::filesystem::remove(tmpFilePath, ec);
}

namespace {
    struct CopyProgressData {
        Controller::AsyncTask* taskPtr;
        BOOL* cancelFlagPtr;
    };
}
static DWORD CALLBACK copyProgressRoutine(
    const LARGE_INTEGER totalFileSize, const LARGE_INTEGER totalBytesTransferred,
    LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE, const LPVOID lpData
) {
    if (const auto dataPtr = static_cast<CopyProgressData*>(lpData)) {
        if (dataPtr->taskPtr->cancelled.load(std::memory_order_acquire)) {
            *dataPtr->cancelFlagPtr = TRUE;
            return PROGRESS_CANCEL;
        }
        if (totalFileSize.QuadPart > 0 && dataPtr->taskPtr->isRunning())
            dataPtr->taskPtr->progress->setProgressTS(static_cast<float>(totalBytesTransferred.QuadPart) / static_cast<float>(totalFileSize.QuadPart));
    }
    return PROGRESS_CONTINUE;
}

bool Controller::startSaveAsImpl(const std::filesystem::path& targetPath) {
    const auto& sourcePath = m_File.getPath();

    std::error_code ec;
    if (std::filesystem::exists(targetPath, ec)) {
        if (std::filesystem::equivalent(sourcePath, targetPath, ec)) {
            m_Ui.showErrorMsg("Source and destination are the same");
            return false;
        }
    }

    m_SaveAsTask.reset();
    m_SaveAsTask.progress = m_Ui.acquireProgressPopup("Saving", false);
    if (!m_SaveAsTask.progress) {
        m_Ui.showErrorMsg("Already busy");
        return false;
    }
    m_SaveAsTask.targetPath = targetPath;

    m_SaveAsTask.thread = std::jthread([taskPtr = &m_SaveAsTask, source = sourcePath, target = targetPath] {
        BOOL cancelFlag = FALSE;

        CopyProgressData data{taskPtr, &cancelFlag};
        const BOOL ok = CopyFileExW(
            source.c_str(), target.c_str(),
            &copyProgressRoutine, &data, &cancelFlag,
            0
        );

        if (ok) {
            taskPtr->result = AsyncTask::Result::Success;
        }
        else {
            if (taskPtr->cancelled.load(std::memory_order_acquire))
                taskPtr->result = AsyncTask::Result::Cancelled;
            else
                taskPtr->result = AsyncTask::Result::Failed;
        }

        taskPtr->finish();
    });

    return true;
}

void Controller::endSaveAsImpl() {
    if (!m_SaveAsTask.isRunning())
        return;

    if (m_SaveAsTask.thread.joinable())
        m_SaveAsTask.thread.join();

    const auto result = m_SaveAsTask.result;
    const auto targetPath = std::move(m_SaveAsTask.targetPath);
    m_SaveAsTask.reset();

    if (result == AsyncTask::Result::Success) {
        openFileImpl(targetPath);
        return;
    }
    if (result == AsyncTask::Result::Failed)
        m_Ui.showErrorMsg("Cannot save the file");
}

void Controller::AsyncTask::cancel() {
    cancelled.store(true, std::memory_order_release);
    progress->setCancelled();
}

void Controller::AsyncTask::reset() {
    cancelled.store(false, std::memory_order_relaxed);
    finished.store(false, std::memory_order_relaxed);
    result = Result::None;
    progress.reset();
}
