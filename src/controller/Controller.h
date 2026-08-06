#pragma once

#include "Commands.h"
#include "file/FileMapping.h"
#include "indexer/LineIndexer.h"

class Win32App;
class Ui;

class Controller {
public:
    Controller(Win32App& app, Ui& ui);
    ~Controller() = default;

    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    Controller(Controller&&) = delete;
    Controller& operator=(Controller&&) = delete;

    void process(const Command& command) { std::visit(*this, command); }

    void operator()(const cmd::OpenFile&);
    void operator()(const cmd::OpenUrl&);
    void operator()(const cmd::GenRandom&);
    void operator()(const cmd::SaveAs&);
    void operator()(const cmd::Close&);
private:
    FileMapping m_File;
    LineIndexer m_LineIndexer;

    Win32App& m_App;
    Ui& m_Ui;
};
