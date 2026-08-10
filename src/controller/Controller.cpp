#include "Controller.h"
#include "../utils/Random.h"
#include "../utils/ScopedProfiler.h"
#include "file/FileWriter.h"

Controller::Controller(Win32App &app, Ui& ui) : m_App(app), m_Ui(ui), m_LineIndexer(m_File) {
    m_Ui.setCommandHandler(std::bind(&Controller::process, this, std::placeholders::_1));
    m_Ui.setTextViewSource({
        std::bind(&Controller::getLineCountImpl, this),
        std::bind(&Controller::getLineImpl, this, std::placeholders::_1),
    });
}

Controller::~Controller() {
    m_Ui.setTextViewSource({});
    m_Ui.setCommandHandler(nullptr);
    closeFileImpl(true);
}

void Controller::tick() {
    if (m_TextGenerationData.progress && m_TextGenerationData.done.load(std::memory_order_acquire))
        endGenFileImpl();
}

bool Controller::operator()(const cmd::Cancel&) {
    if (m_TextGenerationData.progress)
        endGenFileImpl();
    return false;
}

bool Controller::operator()(const cmd::OpenFile&) {
    const auto filePathOpt = m_App.showTextFileDialog(true);
    if (filePathOpt.has_value())
        return openFileImpl(filePathOpt.value());
    return false;
}

bool Controller::operator()(const cmd::OpenUrl&) {
    // TODO
    return false;
}

bool Controller::operator()(const cmd::GenRandom& cmd) {
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
    const auto filePathOpt = m_App.showTextFileDialog(false);
    int lol = 0;
    ++lol;
    return false;
}

bool Controller::operator()(const cmd::Close&) {
    closeFileImpl(true);
    return false;
}

bool Controller::isReadingFromTmp() const {
    return m_File.getPath() == m_App.getTmpTextFilePath();
}

std::string_view Controller::getLineImpl(const uint64_t i) const {
    return m_LineIndexer.get(i);
}

uint64_t Controller::getLineCountImpl() const {
    return m_LineIndexer.count();
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
    if (wasReadingTmp)
        std::filesystem::remove(m_App.getTmpTextFilePath());

    const auto fileName = path.filename();
    m_App.setWindowTitle(fileName.c_str());
    m_Ui.setFileOpen(true);

    return true;
}

void Controller::closeFileImpl(const bool removeTmp) {
    m_App.setWindowTitle(std::nullopt);
    m_Ui.setFileOpen(false);

    const bool wasReadingTmp = isReadingFromTmp();
    m_LineIndexer.clear();
    m_File.close();
    if (removeTmp && wasReadingTmp)
        std::filesystem::remove(m_App.getTmpTextFilePath());
}

bool Controller::startGenFileImpl(const uint64_t targetBytes, const uint64_t targetLines) {
    if (m_TextGenerationData.progress)
        return false;
    if (!targetBytes && !targetLines)
        return false;

    const auto& tmpFilePath = m_App.getTmpTextFilePath();
    if (tmpFilePath.empty()) {
        m_Ui.showErrorMsg("Cannot get temp text file path");
        return false;
    }

    if (isReadingFromTmp())
        closeFileImpl(false);

    auto fileWriterPtr = FileWriter::open(tmpFilePath, TextGenerationData::kFileWriteBuffSize, targetBytes);
    if (!fileWriterPtr) {
        m_Ui.showErrorMsg("Cannot create temp text file");
        return false;
    }

    m_TextGenerationData.progress = m_Ui.acquireProgressPopup("Generating", false);
    if (!m_TextGenerationData.progress) {
        m_Ui.showErrorMsg("Already busy");
        return false;
    }

    m_TextGenerationData.failed = false;
    // ReSharper disable once CppPassValueParameterByConstReference
    m_TextGenerationData.thread = std::jthread([this, w = std::move(fileWriterPtr), targetBytes, targetLines] (const std::stop_token st) {
        XorShift32 rnd;

        static constexpr uint32_t alphabetLen = sizeof(TextGenerationData::kGenAlphabet) - 1;

        // TODO: profile
        {
            ScopedProfiler sw("pool");

            std::vector<char> pool(TextGenerationData::kGenPoolSize);
            for (size_t i = 0; i < TextGenerationData::kGenPoolSize; ++i)
                pool[i] = TextGenerationData::kGenAlphabet[rnd.next() % alphabetLen];
            static constexpr size_t poolSpan = TextGenerationData::kGenPoolSize - TextGenerationData::kGenLineLen;

            char* begin = w->getBuffer();
            const char* end = begin + TextGenerationData::kFileWriteBuffSize;
            char* p = begin;

            const auto flushBuffer = [&] (const float percent, const size_t count = TextGenerationData::kFileWriteBuffSize) -> bool {
                if (w->submit(count)) {
                    m_TextGenerationData.progress->setProgressTS(percent);
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
                while (!st.stop_requested() && remaining >= 2) {
                    // handle \r\n
                    const uint32_t maxLen = static_cast<uint32_t>(remaining - 2 < TextGenerationData::kGenLineLen - 1 ? remaining - 2 : TextGenerationData::kGenLineLen - 1);
                    uint32_t len = rnd.next() % TextGenerationData::kGenLineLen;
                    if (len > maxLen) len = maxLen;
                    if (remaining - 2 - len == 1) ++len;

                    if (!appendChunkAndFlush(len, static_cast<float>(targetBytes - remaining) / targetBytesFlt)) {
                        m_TextGenerationData.failed = true;
                        break;
                    }
                    remaining -= len + 2;
                }
            }
            else {
                const float targetLinesFlt = static_cast<float>(targetLines);
                for (uint64_t n = 0; !st.stop_requested() && n < targetLines; ++n) {
                    const uint32_t left = rnd.next() % TextGenerationData::kGenLineLen;
                    if (!appendChunkAndFlush(left, static_cast<float>(n) / targetLinesFlt)) {
                        m_TextGenerationData.failed = true;
                        break;
                    }
                }
            }

            if (!w->finish(static_cast<size_t>(p - begin)))
                m_TextGenerationData.failed = true;
        }

        if (!st.stop_requested())
            m_TextGenerationData.done.store(true, std::memory_order_release);
    });

    return true;
}

void Controller::endGenFileImpl() {
    if (!m_TextGenerationData.progress)
        return;

    if (m_TextGenerationData.thread.joinable()) {
        m_TextGenerationData.thread.request_stop();
        m_TextGenerationData.thread.join();
    }
    m_TextGenerationData.progress.reset();
    const bool failed = std::exchange(m_TextGenerationData.failed, false);
    const bool done = m_TextGenerationData.done.exchange(false, std::memory_order_acq_rel);

    const auto& tmpFilePath = m_App.getTmpTextFilePath();
    if (tmpFilePath.empty()) {
        m_Ui.showErrorMsg("Cannot get temp text file path");
        return;
    }

    if (done && !failed) {
        openFileImpl(tmpFilePath);
        return;
    }

    if (failed)
        m_Ui.showErrorMsg("Cannot create temp text file");
    std::filesystem::remove(tmpFilePath);
}
