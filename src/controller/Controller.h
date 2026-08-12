#pragma once

#include <thread>
#include "../app/Win32App.h"
#include "../ui/Ui.h"
#include "file/FileMapping.h"
#include "indexer/LineIndexer.h"

class Controller {
public:
    Controller(Win32App& app, Ui& ui);
    ~Controller();

    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    Controller(Controller&&) = delete;
    Controller& operator=(Controller&&) = delete;

    bool process(const Command& command) { return std::visit(*this, command); }
    void tick();

    bool operator()(const cmd::Cancel&);
    bool operator()(const cmd::OpenFile&);
    bool operator()(const cmd::OpenUrl&);
    bool operator()(const cmd::GenRandom& cmd);
    bool operator()(const cmd::SaveAs&);
    bool operator()(const cmd::Close&);
private:
    bool isBusy() const;
    bool isReadingFromTmp() const;

    std::string_view getTextDataImpl(uint64_t lineIdx, uint64_t fromCol, uint64_t maxCols, uint64_t& outLineTotalLength) const;

    bool startOpenFileImpl(const std::filesystem::path& path);
    void endOpenFileImpl();

    void closeFileImpl(bool removeTmp);

    bool startGenFileImpl(uint64_t targetBytes, uint64_t targetLines);
    void endGenFileImpl();

    bool startSaveFileImpl(const std::filesystem::path& targetPath);
    void endSaveFileImpl();

    Win32App& m_App;
    Ui& m_Ui;

    FileMapping m_File;
    LineIndexer m_LineIndexer;
public:
    struct AsyncTask {
        std::unique_ptr<Ui::ProgressPopupRAII> progress;

        std::atomic<bool> cancelled{false};
        std::atomic<bool> finished{false};

        enum class Result : uint8_t {
            None, Success, Failed, Cancelled
        } result = Result::None;

        std::jthread thread;

        bool isRunning() const { return !!progress; }

        void cancel();

        void finish() { finished.store(true, std::memory_order_release); }
        bool isFinished() const { return finished.load(std::memory_order_acquire); }

        void reset();
    };
private:
    struct OpenTask : AsyncTask {
        bool wasReadingTmp;
        LineIndexer newLineIndexer;
    } m_OpenTask;

    struct TextGenerationTask : AsyncTask {
        static constexpr size_t kFileWriteBuffSize = 1ull << 20; // 1 mb

        static constexpr char kGenAlphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .";
        static constexpr uint32_t kGenLineLen = 1024;
        static constexpr size_t kGenPoolSize = 64ull << 10; // 64 kb
    } m_TextGenerationTask;

    struct SaveTask : AsyncTask {
        std::filesystem::path targetPath;
    } m_SaveTask;
};
