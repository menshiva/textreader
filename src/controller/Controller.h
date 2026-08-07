#pragma once

#include <thread>
#include "Commands.h"
#include "file/FileMapping.h"
#include "indexer/LineIndexer.h"

class Win32App;
class Ui;

class Controller {
public:
    Controller(Win32App& app, Ui& ui);
    ~Controller();

    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    Controller(Controller&&) = delete;
    Controller& operator=(Controller&&) = delete;

    void process(const Command& command) { std::visit(*this, command); }
    void tick();

    void operator()(const cmd::OpenFile&);
    void operator()(const cmd::OpenUrl&);
    void operator()(const cmd::GenRandom& cmd);
    void operator()(const cmd::CancelGenRandom&);
    void operator()(const cmd::SaveAs&);
    void operator()(const cmd::Close&);
private:
    bool isReadingTmpFile() const;

    void openFileImpl(const std::filesystem::path& path);

    void resetTextGenWorkingThread();
    void startGenFileImpl(uint64_t targetBytes, uint64_t targetLines);
    void endGenFileImpl();

    void closeFileImpl(bool removeTmp);

    FileMapping m_File;
    LineIndexer m_LineIndexer;

    Win32App& m_App;
    Ui& m_Ui;

    std::jthread m_TextGenWorkingThread;
    std::atomic<bool> m_TextGenDone{false};
};
