#pragma once

/// @file MainWindow.hpp
/// @brief QML-facing project, transport, mixer and audio-output controller.

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include "Aura/AudioEngine.hpp"
#include "Aura/Project.hpp"
#include "Aura/ProjectPlaybackSession.hpp"

namespace Aura::Ui {

class MainWindow : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectNameChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectPathChanged)
    Q_PROPERTY(double tempo READ tempo WRITE setTempo NOTIFY tempoChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(double playheadBeats READ playheadBeats NOTIFY playheadChanged)
    Q_PROPERTY(double timelineLengthBeats READ timelineLengthBeats NOTIFY projectChanged)
    Q_PROPERTY(bool playbackReady READ playbackReady NOTIFY audioStatusChanged)
    Q_PROPERTY(bool audioOutputRunning READ audioOutputRunning NOTIFY audioStatusChanged)
    Q_PROPERTY(QString audioOutputStatus READ audioOutputStatus NOTIFY audioStatusChanged)
    Q_PROPERTY(double cpuLoad READ cpuLoad NOTIFY metersChanged)
    Q_PROPERTY(double masterPeak READ masterPeak NOTIFY metersChanged)
    Q_PROPERTY(qulonglong underrunCount READ underrunCount NOTIFY metersChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY projectChanged)

  public:
    explicit MainWindow(QObject* parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] QString projectName() const;
    [[nodiscard]] QString projectPath() const;
    [[nodiscard]] double tempo() const { return project_.tempo(); }
    [[nodiscard]] bool playing() const { return playing_; }
    [[nodiscard]] bool dirty() const { return project_.isDirty(); }
    [[nodiscard]] double playheadBeats() const { return playheadBeats_; }
    [[nodiscard]] double timelineLengthBeats() const;
    [[nodiscard]] bool playbackReady() const { return playbackSession_.isLoaded(); }
    [[nodiscard]] bool audioOutputRunning() const { return audioOutputRunning_; }
    [[nodiscard]] QString audioOutputStatus() const { return audioOutputStatus_; }
    [[nodiscard]] double cpuLoad() const { return cpuLoad_; }
    [[nodiscard]] double masterPeak() const { return masterPeak_; }
    [[nodiscard]] qulonglong underrunCount() const { return underrunCount_; }
    [[nodiscard]] QString statusMessage() const { return statusMessage_; }
    [[nodiscard]] QVariantList tracks() const;

  public slots:
    void play();
    void stop();
    void rewind();
    void restartAudio();
    void seekBeats(double beats);
    void record();
    void setTempo(double bpm);
    void newProject(const QString& name);
    bool saveProject(const QString& path);
    bool loadProject(const QString& path);
    bool importWav(const QString& path);
    void setTrackVolumeDb(const QString& trackId, double volumeDb);
    void setTrackPan(const QString& trackId, double pan);

  signals:
    void projectNameChanged();
    void projectPathChanged();
    void tempoChanged();
    void playbackChanged();
    void playheadChanged();
    void metersChanged();
    void projectChanged();
    void dirtyChanged();
    void statusMessageChanged();
    void audioStatusChanged();
    void operationFailed(const QString& message);

  private slots:
    void refreshPlaybackState();

  private:
    void setStatus(QString message);
    void setAudioOutputStatus(QString message);
    void reportFailure(const QString& message);
    void notifyProjectChanged();
    void quiesceAudio(bool clearSession);
    void updateAudioOutputStatusForRunningStream();
    void applyPendingPosition();
    [[nodiscard]] QString defaultOutputDescription();

    Aura::Project::Project project_;
    Aura::Session::ProjectPlaybackSession playbackSession_;
    Aura::Audio::AudioEngine audioEngine_;
    QTimer pollTimer_;
    bool playing_ = false;
    bool audioOutputRunning_ = false;
    bool wasAudioOutputRunning_ = false;
    double playheadBeats_ = 0.0;
    double pendingPositionBeats_ = 0.0;
    double cpuLoad_ = 0.0;
    double masterPeak_ = 0.0;
    qulonglong underrunCount_ = 0;
    QString outputDescription_;
    QString audioOutputStatus_ = QStringLiteral("Audio output not initialized");
    QString statusMessage_ = QStringLiteral("Ready");
};

} // namespace Aura::Ui
