#include "Controller.h"
#include <random>
#include "../app/Win32App.h"
#include "../ui/Ui.h"
#include "../utils/profiler/ScopedProfiler.h"

Controller::Controller(Win32App &app, Ui& ui) : m_LineIndexer(m_File), m_App(app), m_Ui(ui) {}

Controller::~Controller() {
    closeFileImpl(true);
}

void Controller::tick() {
    if (m_TextGenDone.exchange(false, std::memory_order_acq_rel))
        endGenFileImpl();
}

void Controller::operator()(const cmd::OpenFile&) {
    const auto filePathOpt = m_App.showTextFileDialog(true);
    if (filePathOpt.has_value())
        openFileImpl(filePathOpt.value());
}

void Controller::operator()(const cmd::OpenUrl&) {
    // TODO
}

void Controller::operator()(const cmd::GenRandom& cmd) {
    uint64_t bytes = 0, lines = 0;
    switch (cmd.type) {
        case cmd::GenRandom::Type::Kb: bytes = static_cast<uint64_t>(cmd.value) << 10; break;
        case cmd::GenRandom::Type::Mb: bytes = static_cast<uint64_t>(cmd.value) << 20; break;
        case cmd::GenRandom::Type::Gb: bytes = static_cast<uint64_t>(cmd.value) << 30; break;
        case cmd::GenRandom::Type::Lines: lines = cmd.value; break;
    }
    startGenFileImpl(bytes, lines);
}

void Controller::operator()(const cmd::CancelGenRandom&) {
    resetTextGenWorkingThread();
    m_App.removeTmpTextFiles(false, true);
    m_Ui.setGenTxtState(Ui::GenTxtState::Idle);
}

void Controller::operator()(const cmd::SaveAs&) {
    const auto filePathOpt = m_App.showTextFileDialog(false);
    int lol = 0;
    ++lol;
}

void Controller::operator()(const cmd::Close&) {
    closeFileImpl(true);
}

bool Controller::isReadingTmpFile() const {
    return m_File.getPath() == m_App.getTmpTextFileReadPath();
}

void Controller::openFileImpl(const std::filesystem::path& path) {
    if (path == m_File.getPath())
        return;

    const bool wasReadingTmp = isReadingTmpFile();
    if (!m_File.open(path)) {
        m_Ui.showErrorMsg("Something went wrong while opening the file");
        return;
    }
    m_LineIndexer.build();
    if (wasReadingTmp)
        m_App.removeTmpTextFiles(true, false);

    const auto fileName = path.filename();
    m_App.setWindowTitle(fileName.c_str());
    m_Ui.setFileOpen(true);

    // TODO
}

void Controller::resetTextGenWorkingThread() {
    if (m_TextGenWorkingThread.joinable()) {
        m_TextGenWorkingThread.request_stop();
        m_TextGenWorkingThread.join();
    }
    m_TextGenDone.store(false, std::memory_order_release);
}

void Controller::startGenFileImpl(const uint64_t targetBytes, const uint64_t targetLines) {
    if (!targetBytes && !targetLines)
        return;

    resetTextGenWorkingThread();

    auto tmpFileDescPtr = m_App.getTmpTextFileWriteDescriptor();
    if (!tmpFileDescPtr) {
        m_Ui.showErrorMsg("Cannot create or access temp file (write)");
        return;
    }

    m_Ui.setGenTxtState(Ui::GenTxtState::Running);

    // ReSharper disable once CppPassValueParameterByConstReference
    m_TextGenWorkingThread = std::jthread([this, desc = std::move(tmpFileDescPtr), targetBytes, targetLines] (const std::stop_token st) {
        // tried to use std::mt19937 and std::uniform_int_distribution<int>, but it was too slow
        // using https://prng.di.unimi.it/splitmix64.c instead
        struct SplitMix64 {
            uint64_t x;

            uint64_t next() {
                uint64_t z = (x += 0x9e3779b97f4a7c15);
                z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
                z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
                return z ^ (z >> 31);
            }
        };

        std::random_device rd;
        SplitMix64 rnd{(static_cast<uint64_t>(rd()) << 32) | rd()}; // pack 2 rd() results into uint64_t

        static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .";
        static constexpr uint32_t alphabetLen = sizeof(alphabet) - 1;
        static constexpr uint32_t lineLen = 1024;

        // TODO: profile

        {
            ScopedProfiler sw("pool");

            static constexpr size_t poolSize = 64ull << 10; // 64 kb
            std::vector<char> pool(poolSize);
            for (size_t i = 0; i < poolSize; ++i)
                pool[i] = alphabet[rnd.next() % alphabetLen];
            static constexpr size_t poolSpan = poolSize - lineLen;

            static constexpr size_t buffSize = 1ull << 20; // 1 mb
            std::vector<char> storage(buffSize);
            char* const begin = storage.data();
            const char* const end = storage.data() + buffSize;
            char* p = begin;

            const auto flushBuffer = [&] (const size_t count, const float percent) {
                fwrite(begin, 1, count, desc->file);
                m_Ui.setGenTxtProgressTS(percent);
                p = begin;
            };
            const auto appendChunkAndFlush = [&] (uint32_t left, const float percent) {
                while (left > 0) {
                    if (p == end)
                        flushBuffer(buffSize, percent);

                    const uint32_t chunk = std::min<uint32_t>(static_cast<uint32_t>(end - p), left);
                    memcpy(p, pool.data() + rnd.next() % poolSpan, chunk);
                    p += chunk;
                    left -= chunk;
                }

                if (p == end)
                    flushBuffer(buffSize, percent);
                *p++ = '\r';
                if (p == end)
                    flushBuffer(buffSize, percent);
                *p++ = '\n';
            };

            if (targetBytes) {
                const float targetBytesFlt = static_cast<float>(targetBytes);
                uint64_t remaining = targetBytes;
                while (!st.stop_requested() && remaining >= 2) {
                    // handle \r\n
                    const uint32_t maxLen = static_cast<uint32_t>(remaining - 2 < lineLen - 1 ? remaining - 2 : lineLen - 1);
                    uint32_t len = rnd.next() % lineLen;
                    if (len > maxLen) len = maxLen;
                    if (remaining - 2 - len == 1) ++len;

                    appendChunkAndFlush(len, static_cast<float>(targetBytes - remaining) / targetBytesFlt);
                    remaining -= len + 2;
                }
            }
            else {
                const float targetLinesFlt = static_cast<float>(targetLines);
                for (uint64_t n = 0; !st.stop_requested() && n < targetLines; ++n) {
                    const uint32_t left = rnd.next() % lineLen;
                    appendChunkAndFlush(left, static_cast<float>(n) / targetLinesFlt);
                }
            }

            if (p != begin)
                flushBuffer(p - begin, 1.0f);
        }

        if (!st.stop_requested())
            m_TextGenDone.store(true, std::memory_order_release);
    });
}

void Controller::endGenFileImpl() {
    resetTextGenWorkingThread();

    if (m_App.getTmpTextFileReadPath().empty()) {
        m_App.removeTmpTextFiles(true, true);
        m_Ui.setGenTxtState(Ui::GenTxtState::Idle);
        m_Ui.showErrorMsg("Cannot create or access temp file (read)");
        return;
    }

    if (isReadingTmpFile())
        closeFileImpl(false);
    m_App.exchangeTmpTextFiles();
    m_Ui.setGenTxtState(Ui::GenTxtState::None);
    openFileImpl(m_App.getTmpTextFileReadPath());
}

void Controller::closeFileImpl(const bool removeTmp) {
    const bool wasReadingTmp = isReadingTmpFile();
    m_LineIndexer.clear();
    m_File.close();
    if (removeTmp && wasReadingTmp)
        m_App.removeTmpTextFiles(true, false);

    m_App.resetWindowTitle();
    m_Ui.setFileOpen(false);

    // TODO
}
