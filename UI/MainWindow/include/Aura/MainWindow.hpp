#pragma once

/// @file MainWindow.hpp
/// @brief QML-facing application controller: transport, project, meters.
///
/// The controller lives on the Qt GUI thread and forwards commands to the
/// real-time engine through lock-free queues (see Engine). Meters are polled
/// by QML at the display refresh rate.

#include <QObject>
#include <QString>

namespace Aura::Ui {

class MainWindow : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectNameChanged)
    Q_PROPERTY(double tempo READ tempo WRITE setTempo NOTIFY tempoChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(double playheadBeats READ playheadBeats NOTIFY playheadChanged)
    Q_PROPERTY(double cpuLoad READ cpuLoad NOTIFY metersChanged)
    Q_PROPERTY(double masterPeak READ masterPeak NOTIFY metersChanged)

public:
    explicit MainWindow(QObject* parent = nullptr);

    [[nodiscard]] QString projectName() const { return projectName_; }
    [[nodiscard]] double tempo() const { return tempo_; }
    [[nodiscard]] bool playing() const { return playing_; }
    [[nodiscard]] double playheadBeats() const { return playheadBeats_; }
    [[nodiscard]] double cpuLoad() const { return cpuLoad_; }
    [[nodiscard]] double masterPeak() const { return masterPeak_; }

public slots:
    void play();
    void stop();
    void record();
    void setTempo(double bpm);
    void newProject(const QString& name);
    bool saveProject(const QString& path);
    bool loadProject(const QString& path);

signals:
    void projectNameChanged();
    void tempoChanged();
    void playbackChanged();
    void playheadChanged();
    void metersChanged();

private:
    QString projectName_ = "Untitled";
    double tempo_ = 120.0;
    bool playing_ = false;
    double playheadBeats_ = 0.0;
    double cpuLoad_ = 0.0;
    double masterPeak_ = 0.0;
};

} // namespace Aura::Ui
