#pragma once

#include <filesystem>
#include <thread>
#include "Commands.h"

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

    bool process(const Command& command) { return std::visit(*this, command); }
    void tick();

    bool operator()(const cmd::Cancel&);
    bool operator()(const cmd::OpenFile&);
    bool operator()(const cmd::OpenUrl&);
    bool operator()(const cmd::GenRandom& cmd);
    bool operator()(const cmd::SaveAs&);
    bool operator()(const cmd::Close&);
private:
    void openFileImpl(const std::filesystem::path& path);

    bool startGenFileImpl(uint64_t targetBytes, uint64_t targetLines);
    void endGenFileImpl();

    void closeFileImpl(bool removeTmp);

    Win32App& m_App;
    Ui& m_Ui;

    struct TextGenerationData {
        std::jthread thread;
        bool running = false;
        bool failed = false;
        std::atomic<bool> done = false;
    } m_TextGenerationData;

    /*FileMapping m_File;
    LineIndexer m_LineIndexer;

    */
};
