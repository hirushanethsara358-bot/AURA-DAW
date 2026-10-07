/// @file MainWindow.cpp
/// @brief Implementation of the QML application controller.

#include "Aura/MainWindow.hpp"

#include <algorithm>

namespace Aura::Ui {

MainWindow::MainWindow(QObject* parent) : QObject(parent) {}

void MainWindow::play() {
    // TODO(Phase 2): forward to Transport + AudioEngine via lock-free queue.
    playing_ = true;
    emit playbackChanged();
}

void MainWindow::stop() {
    playing_ = false;
    emit playbackChanged();
}

void MainWindow::record() {
    playing_ = true;
    emit playbackChanged();
}

void MainWindow::setTempo(double bpm) {
    const double clamped = std::clamp(bpm, 20.0, 999.0);
    if (clamped != tempo_) {
        tempo_ = clamped;
        emit tempoChanged();
    }
}

void MainWindow::newProject(const QString& name) {
    projectName_ = name.isEmpty() ? "Untitled" : name;
    playheadBeats_ = 0.0;
    emit projectNameChanged();
    emit playheadChanged();
}

bool MainWindow::saveProject(const QString& path) {
    // TODO(Phase 2): serialize Aura::Project::Project to .aura.
    (void)path;
    return true;
}

bool MainWindow::loadProject(const QString& path) {
    // TODO(Phase 2): load .aura and rebuild the session.
    (void)path;
    return true;
}

} // namespace Aura::Ui
