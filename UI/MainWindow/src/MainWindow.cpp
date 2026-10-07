/// @file MainWindow.cpp
/// @brief Implementation of the QML project, transport and audio controller.

#include "Aura/MainWindow.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include <QFileInfo>
#include <QVariantMap>

#include "Aura/Sampler.hpp"

namespace Aura::Ui {
namespace {

constexpr double kBeatsPerBar = 4.0;

std::string toUtf8(const QString& text) {
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString fromUtf8(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

MainWindow::MainWindow(QObject* parent) : QObject(parent) {
    audioEngine_.setCallback(&playbackSession_);
    outputDescription_ = defaultOutputDescription();
    setAudioOutputStatus(outputDescription_ + QStringLiteral(" · idle"));

    connect(&pollTimer_, &QTimer::timeout, this, &MainWindow::refreshPlaybackState);
    pollTimer_.setInterval(33);
    pollTimer_.start();
}

MainWindow::~MainWindow() {
    pollTimer_.stop();
    audioEngine_.stop();
    playbackSession_.stop();
    audioEngine_.setCallback(nullptr);
    playbackSession_.clear();
}

QString MainWindow::projectName() const {
    return fromUtf8(project_.name());
}

QString MainWindow::projectPath() const {
    return fromUtf8(project_.filePath());
}

double MainWindow::timelineLengthBeats() const {
    double lastBar = 1.0;
    for (const auto& track : project_.tracks()) {
        for (const auto& clip : track.clips) {
            lastBar = std::max(lastBar, clip.startBar + clip.lengthBars);
        }
    }
    return lastBar * kBeatsPerBar;
}

QVariantList MainWindow::tracks() const {
    QVariantList result;
    result.reserve(static_cast<qsizetype>(project_.tracks().size()));
    for (const auto& track : project_.tracks()) {
        QVariantMap trackData;
        trackData.insert(QStringLiteral("id"), fromUtf8(track.id));
        trackData.insert(QStringLiteral("name"), fromUtf8(track.name));
        trackData.insert(QStringLiteral("type"),
                         QString::fromLatin1(Aura::Project::toString(track.type)));
        trackData.insert(QStringLiteral("volumeDb"), track.mixer.volumeDb);
        trackData.insert(QStringLiteral("pan"), track.mixer.pan);
        trackData.insert(QStringLiteral("mute"), track.mixer.mute);
        trackData.insert(QStringLiteral("solo"), track.mixer.solo);

        QVariantList clips;
        clips.reserve(static_cast<qsizetype>(track.clips.size()));
        for (const auto& clip : track.clips) {
            QVariantMap clipData;
            clipData.insert(QStringLiteral("id"), fromUtf8(clip.id));
            clipData.insert(QStringLiteral("name"), fromUtf8(clip.name));
            clipData.insert(QStringLiteral("startBar"), clip.startBar);
            clipData.insert(QStringLiteral("lengthBars"), clip.lengthBars);
            clipData.insert(QStringLiteral("sourcePath"), fromUtf8(clip.sourcePath));
            clips.push_back(clipData);
        }
        trackData.insert(QStringLiteral("clips"), clips);
        result.push_back(trackData);
    }
    return result;
}

void MainWindow::play() {
    if (playing_) {
        return;
    }

    if (!playbackSession_.isLoaded()) {
        std::string error;
        const Aura::Audio::AudioDeviceConfig config = audioEngine_.config();
        if (!playbackSession_.loadProject(project_, config.sampleRate, error)) {
            reportFailure(fromUtf8(error));
            return;
        }
        applyPendingPosition();
        emit audioStatusChanged();
    }

    const std::string startError = audioEngine_.start();
    if (!startError.empty()) {
        setAudioOutputStatus(outputDescription_ + QStringLiteral(" · unavailable"));
        reportFailure(QStringLiteral("Could not start audio output: %1").arg(fromUtf8(startError)));
        return;
    }

    playbackSession_.play();
    const bool started = audioEngine_.isRunning() && playbackSession_.isPlaying();
    if (started != playing_) {
        playing_ = started;
        emit playbackChanged();
    }
    audioOutputRunning_ = audioEngine_.isRunning();
    wasAudioOutputRunning_ = audioOutputRunning_;
    if (audioOutputRunning_) {
        updateAudioOutputStatusForRunningStream();
    }
    if (playing_) {
        setStatus(QStringLiteral("Playing."));
    } else {
        reportFailure(QStringLiteral("The audio stream opened, but the clip could not start."));
    }
    emit audioStatusChanged();
}

void MainWindow::stop() {
    playbackSession_.stop();
    pendingPositionBeats_ =
        playbackSession_.isLoaded() ? playbackSession_.positionBars() * kBeatsPerBar : 0.0;
    if (playing_) {
        playing_ = false;
        emit playbackChanged();
    }
    setStatus(QStringLiteral("Playback stopped."));
}

void MainWindow::rewind() {
    stop();
    seekBeats(0.0);
    setStatus(QStringLiteral("Playhead returned to the start."));
}

void MainWindow::restartAudio() {
    if (!playbackSession_.isLoaded()) {
        play();
        return;
    }

    audioEngine_.stop();
    wasAudioOutputRunning_ = false;
    audioOutputRunning_ = false;
    emit audioStatusChanged();

    const std::string startError = audioEngine_.start();
    if (!startError.empty()) {
        setAudioOutputStatus(outputDescription_ + QStringLiteral(" · disconnected"));
        reportFailure(
            QStringLiteral("Could not restart audio output: %1").arg(fromUtf8(startError)));
        return;
    }

    audioOutputRunning_ = audioEngine_.isRunning();
    wasAudioOutputRunning_ = audioOutputRunning_;
    if (audioOutputRunning_) {
        updateAudioOutputStatusForRunningStream();
    }
    const bool nowPlaying = audioOutputRunning_ && playbackSession_.isPlaying();
    if (nowPlaying != playing_) {
        playing_ = nowPlaying;
        emit playbackChanged();
    }
    emit audioStatusChanged();
    setStatus(playing_ ? QStringLiteral("Audio output restarted; playback resumed.")
                       : QStringLiteral("Audio output restarted."));
}

void MainWindow::seekBeats(double beats) {
    if (!std::isfinite(beats)) {
        return;
    }
    const double maxBeats = std::max(kBeatsPerBar, timelineLengthBeats());
    const double target = std::clamp(beats, 0.0, maxBeats);
    if (playbackSession_.isLoaded() && !playbackSession_.seekBars(target / kBeatsPerBar)) {
        reportFailure(QStringLiteral("The playhead position is outside the supported range."));
        return;
    }
    pendingPositionBeats_ = target;
    if (std::abs(playheadBeats_ - target) > 1.0e-9) {
        playheadBeats_ = target;
        emit playheadChanged();
    }
}

void MainWindow::record() {
    reportFailure(QStringLiteral("Recording is not part of the 0.1 MVP."));
}

void MainWindow::setTempo(double bpm) {
    if (!std::isfinite(bpm)) {
        return;
    }
    const double oldTempo = project_.tempo();
    project_.setTempo(bpm);
    const double newTempo = project_.tempo();
    if (newTempo == oldTempo) {
        return;
    }

    const bool restartStream = audioEngine_.isRunning();
    if (restartStream) {
        audioEngine_.stop();
        wasAudioOutputRunning_ = false;
        audioOutputRunning_ = false;
    }

    if (const std::string error = playbackSession_.setProjectTempo(newTempo); !error.empty()) {
        project_.setTempo(oldTempo);
        if (playbackSession_.isLoaded()) {
            (void)playbackSession_.setProjectTempo(oldTempo);
        }
        if (restartStream) {
            const std::string restartError = audioEngine_.start();
            audioOutputRunning_ = restartError.empty() && audioEngine_.isRunning();
            wasAudioOutputRunning_ = audioOutputRunning_;
            if (audioOutputRunning_) {
                updateAudioOutputStatusForRunningStream();
            }
        }
        emit audioStatusChanged();
        reportFailure(fromUtf8(error));
        return;
    }

    emit tempoChanged();
    emit dirtyChanged();
    if (restartStream) {
        const std::string restartError = audioEngine_.start();
        if (!restartError.empty()) {
            audioOutputRunning_ = false;
            wasAudioOutputRunning_ = false;
            setAudioOutputStatus(outputDescription_ + QStringLiteral(" · disconnected"));
            reportFailure(QStringLiteral("Tempo changed, but audio could not restart: %1")
                              .arg(fromUtf8(restartError)));
        } else {
            audioOutputRunning_ = audioEngine_.isRunning();
            wasAudioOutputRunning_ = audioOutputRunning_;
            if (audioOutputRunning_) {
                updateAudioOutputStatusForRunningStream();
            }
            setStatus(QStringLiteral("Tempo updated."));
        }
        emit audioStatusChanged();
    } else {
        setStatus(QStringLiteral("Tempo updated."));
    }
}

void MainWindow::newProject(const QString& name) {
    quiesceAudio(true);
    project_.createNew(name.trimmed().isEmpty() ? std::string("Untitled") : toUtf8(name.trimmed()));
    playheadBeats_ = 0.0;
    pendingPositionBeats_ = 0.0;
    emit playheadChanged();
    notifyProjectChanged();
    setStatus(QStringLiteral("New project created."));
}

bool MainWindow::saveProject(const QString& path) {
    if (path.trimmed().isEmpty()) {
        reportFailure(QStringLiteral("Choose a destination before saving the project."));
        return false;
    }
    const std::string error = project_.save(toUtf8(path));
    if (!error.empty()) {
        reportFailure(fromUtf8(error));
        return false;
    }
    emit projectPathChanged();
    emit dirtyChanged();
    setStatus(QStringLiteral("Saved %1").arg(projectPath()));
    return true;
}

bool MainWindow::loadProject(const QString& path) {
    if (path.trimmed().isEmpty()) {
        reportFailure(QStringLiteral("Choose an AURA project file to open."));
        return false;
    }
    Aura::Project::Project candidate;
    const std::string error = candidate.load(toUtf8(path));
    if (!error.empty()) {
        reportFailure(fromUtf8(error));
        return false;
    }

    quiesceAudio(true);
    project_ = std::move(candidate);
    playheadBeats_ = 0.0;
    pendingPositionBeats_ = 0.0;
    emit playheadChanged();
    notifyProjectChanged();
    setStatus(QStringLiteral("Opened %1").arg(projectPath()));
    return true;
}

bool MainWindow::importWav(const QString& path) {
    if (path.trimmed().isEmpty()) {
        reportFailure(QStringLiteral("Choose a WAV file to import."));
        return false;
    }
    const std::string pathUtf8 = toUtf8(QFileInfo(path).absoluteFilePath());
    const auto metadata = Aura::Instrument::inspectWavFile(pathUtf8);
    if (!metadata.ok) {
        reportFailure(fromUtf8(metadata.error));
        return false;
    }

    const QString clipName = QFileInfo(path).completeBaseName();
    const std::string trackId =
        project_.addTrack(toUtf8(clipName), Aura::Project::TrackType::Audio);
    Aura::Project::ClipState clip;
    clip.name = toUtf8(clipName);
    clip.sourcePath = pathUtf8;
    clip.startBar = 0.0;
    // The WAV is decoded synchronously when playback is first started; the
    // real-time callback only reads the prepared PCM buffer.
    clip.lengthBars =
        std::max(0.0625, metadata.metadata.durationSeconds * project_.tempo() / 240.0);

    std::string error;
    if (!project_.addClip(trackId, std::move(clip), error)) {
        project_.removeTrack(trackId);
        reportFailure(fromUtf8(error));
        return false;
    }

    quiesceAudio(true);
    playheadBeats_ = 0.0;
    pendingPositionBeats_ = 0.0;
    emit playheadChanged();
    notifyProjectChanged();
    setStatus(QStringLiteral("Imported WAV clip: %1").arg(clipName));
    return true;
}

void MainWindow::setTrackVolumeDb(const QString& trackId, double volumeDb) {
    auto* track = project_.findTrack(toUtf8(trackId));
    if (track == nullptr || !std::isfinite(volumeDb)) {
        return;
    }
    Aura::Project::TrackMixerState state = track->mixer;
    state.volumeDb = std::clamp(volumeDb, -96.0, 12.0);
    if (project_.setTrackMixerState(track->id, state)) {
        if (playbackSession_.isLoaded() && track->type == Aura::Project::TrackType::Audio &&
            !track->clips.empty()) {
            (void)playbackSession_.setGainPan(track->mixer.volumeDb, track->mixer.pan);
        }
        emit dirtyChanged();
    }
}

void MainWindow::setTrackPan(const QString& trackId, double pan) {
    auto* track = project_.findTrack(toUtf8(trackId));
    if (track == nullptr || !std::isfinite(pan)) {
        return;
    }
    Aura::Project::TrackMixerState state = track->mixer;
    state.pan = std::clamp(pan, -1.0, 1.0);
    if (project_.setTrackMixerState(track->id, state)) {
        if (playbackSession_.isLoaded() && track->type == Aura::Project::TrackType::Audio &&
            !track->clips.empty()) {
            (void)playbackSession_.setGainPan(track->mixer.volumeDb, track->mixer.pan);
        }
        emit dirtyChanged();
    }
}

void MainWindow::refreshPlaybackState() {
    const bool streamRunning = audioEngine_.isRunning();
    if (streamRunning != audioOutputRunning_) {
        audioOutputRunning_ = streamRunning;
        emit audioStatusChanged();
    }

    if (streamRunning) {
        wasAudioOutputRunning_ = true;
        updateAudioOutputStatusForRunningStream();
    } else if (wasAudioOutputRunning_) {
        wasAudioOutputRunning_ = false;
        const std::string error = audioEngine_.lastError();
        setAudioOutputStatus(outputDescription_ + QStringLiteral(" · disconnected"));
        if (!error.empty()) {
            reportFailure(QStringLiteral("Audio output stopped: %1").arg(fromUtf8(error)));
        } else {
            setStatus(QStringLiteral("Audio output stopped."));
        }
    }

    const bool nextPlaying =
        streamRunning && playbackSession_.isLoaded() && playbackSession_.isPlaying();
    if (nextPlaying != playing_) {
        playing_ = nextPlaying;
        emit playbackChanged();
        if (!playing_ && streamRunning) {
            pendingPositionBeats_ = playbackSession_.positionBars() * kBeatsPerBar;
            setStatus(QStringLiteral("Playback reached the end of the clip."));
        }
    }

    if (playbackSession_.isLoaded()) {
        const double nextBeats = playbackSession_.positionBars() * kBeatsPerBar;
        pendingPositionBeats_ = nextBeats;
        if (std::abs(nextBeats - playheadBeats_) > 1.0e-6) {
            playheadBeats_ = nextBeats;
            emit playheadChanged();
        }
    }

    const double nextCpuLoad = streamRunning ? audioEngine_.cpuLoad() : 0.0;
    const double nextMasterPeak = playbackSession_.isLoaded() ? playbackSession_.masterPeak() : 0.0;
    const qulonglong nextUnderrunCount = static_cast<qulonglong>(audioEngine_.underrunCount());
    if (std::abs(nextCpuLoad - cpuLoad_) > 1.0e-4 ||
        std::abs(nextMasterPeak - masterPeak_) > 1.0e-4 || nextUnderrunCount != underrunCount_) {
        cpuLoad_ = nextCpuLoad;
        masterPeak_ = nextMasterPeak;
        underrunCount_ = nextUnderrunCount;
        emit metersChanged();
    }
}

void MainWindow::setStatus(QString message) {
    if (message != statusMessage_) {
        statusMessage_ = std::move(message);
        emit statusMessageChanged();
    }
}

void MainWindow::setAudioOutputStatus(QString message) {
    if (message != audioOutputStatus_) {
        audioOutputStatus_ = std::move(message);
        emit audioStatusChanged();
    }
}

void MainWindow::reportFailure(const QString& message) {
    setStatus(message);
    emit operationFailed(message);
}

void MainWindow::notifyProjectChanged() {
    emit projectNameChanged();
    emit projectPathChanged();
    emit tempoChanged();
    emit projectChanged();
    emit dirtyChanged();
    emit audioStatusChanged();
}

void MainWindow::quiesceAudio(bool clearSession) {
    audioEngine_.stop();
    playbackSession_.stop();
    if (clearSession) {
        playbackSession_.clear();
    }
    if (playing_) {
        playing_ = false;
        emit playbackChanged();
    }
    audioOutputRunning_ = false;
    wasAudioOutputRunning_ = false;
    cpuLoad_ = 0.0;
    masterPeak_ = clearSession ? 0.0 : masterPeak_;
    setAudioOutputStatus(outputDescription_ + QStringLiteral(" · idle"));
    emit metersChanged();
    emit audioStatusChanged();
}

void MainWindow::updateAudioOutputStatusForRunningStream() {
    const double sampleRate = audioEngine_.negotiatedSampleRate();
    const int channels = audioEngine_.negotiatedOutputChannels();
    const int latency = audioEngine_.latencySamples();
    const double latencyMilliseconds =
        sampleRate > 0.0 ? static_cast<double>(latency) * 1000.0 / sampleRate : 0.0;
    const QString message = QStringLiteral("%1 · %2 Hz · %3 ch · ~%4 ms")
                                .arg(outputDescription_)
                                .arg(static_cast<qlonglong>(std::llround(sampleRate)))
                                .arg(channels)
                                .arg(latencyMilliseconds, 0, 'f', 1);
    setAudioOutputStatus(message);
}

void MainWindow::applyPendingPosition() {
    if (playbackSession_.isLoaded()) {
        const double maxBeats = std::max(kBeatsPerBar, timelineLengthBeats());
        pendingPositionBeats_ = std::clamp(pendingPositionBeats_, 0.0, maxBeats);
        (void)playbackSession_.seekBars(pendingPositionBeats_ / kBeatsPerBar);
        playheadBeats_ = pendingPositionBeats_;
        emit playheadChanged();
    }
}

QString MainWindow::defaultOutputDescription() {
    const auto config = audioEngine_.config();
    const QString driverName = QString::fromLatin1(Aura::Audio::toString(config.driver));
    const auto devices = audioEngine_.enumerateDevices();
    const auto match = std::find_if(devices.begin(), devices.end(), [&](const auto& device) {
        return device.driver == config.driver && device.isDefaultOutput;
    });
    if (match != devices.end()) {
        return driverName + QStringLiteral(" — ") + fromUtf8(match->name);
    }
    return driverName + QStringLiteral(" output unavailable");
}

} // namespace Aura::Ui
