#include "Controller.h"
#include "../app/Win32App.h"
#include "../ui/Ui.h"

Controller::Controller(Win32App &app, Ui& ui) : m_LineIndexer(m_File), m_App(app), m_Ui(ui) {}

void Controller::operator()(const cmd::OpenFile&) {
    const auto filePathOpt = m_App.showTextFileDialog(true);
    if (!filePathOpt.has_value())
        return;
    const auto& filePath = filePathOpt.value();

    if (!m_File.open(filePath))
        return; // TODO: handle error
    m_LineIndexer.build();

    const auto fileName = filePath.filename();
    m_App.setWindowTitle(fileName.c_str());
    m_Ui.setFileOpen(true);

    // TODO
}

void Controller::operator()(const cmd::OpenUrl&) {
    // TODO
}

void Controller::operator()(const cmd::GenRandom&) {
    // TODO
}

void Controller::operator()(const cmd::SaveAs&) {
    const auto filePathOpt = m_App.showTextFileDialog(false);
    int lol = 0;
    ++lol;
}

void Controller::operator()(const cmd::Close &) {
    m_LineIndexer.clear();
    m_File.close();

    m_App.resetWindowTitle();
    m_Ui.setFileOpen(false);

    // TODO
}
