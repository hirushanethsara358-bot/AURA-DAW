/// @file ProjectPlaybackSession.cpp
/// @brief Transactional project/WAV preparation for the first playback slice.

#include "Aura/ProjectPlaybackSession.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Aura::Session {
namespace {

bool barsToFrames(double bars, double samplesPerBar, std::uint64_t& frames) noexcept {
    if (!std::isfinite(bars) || bars < 0.0 || !std::isfinite(samplesPerBar) ||
        samplesPerBar <= 0.0) {
        return false;
    }
    const double exactFrames = bars * samplesPerBar;
    if (!std::isfinite(exactFrames) ||
        exactFrames >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        return false;
    }
    frames = static_cast<std::uint64_t>(std::round(exactFrames));
    return true;
}

} // namespace

ProjectPlaybackSession::ProjectPlaybackSession(std::uint64_t decodedMemoryLimitBytes)
    : decodedMemoryLimitBytes_(decodedMemoryLimitBytes), transport_(48000.0),
      buffers_(std::make_unique<Instrument::AudioBufferManager>(decodedMemoryLimitBytes)),
      player_(std::make_unique<Instrument::WavClipPlayer>()) {}

bool ProjectPlaybackSession::loadProject(const Project::Project& project, double outputSampleRate,
                                         std::string& error) {
    error.clear();
    if (player_->isPlaying()) {
        error = "Stop playback before replacing the project audio session.";
        return false;
    }
    if (!std::isfinite(outputSampleRate) || outputSampleRate < 8000.0 ||
        outputSampleRate > 384000.0 || !std::isfinite(project.tempo()) || project.tempo() <= 0.0) {
        error = "Invalid project tempo or audio sample rate.";
        return false;
    }

    const Project::TrackState* selectedTrack = nullptr;
    const Project::ClipState* selectedClip = nullptr;
    for (const auto& track : project.tracks()) {
        if (track.type != Project::TrackType::Audio) {
            continue;
        }
        for (const auto& clip : track.clips) {
            if (selectedClip != nullptr) {
                error = "The 0.1 playback adapter currently supports one audio clip at a time.";
                return false;
            }
            selectedTrack = &track;
            selectedClip = &clip;
        }
    }
    if (selectedTrack == nullptr || selectedClip == nullptr) {
        error = "The project contains no audio clip to play.";
        return false;
    }
    if (selectedClip->sourcePath.empty()) {
        error = "The audio clip has no WAV source path.";
        return false;
    }

    // The current project schema/timeline uses 4/4 bars. Decode and allocate
    // everything off the audio callback before publishing the candidate session.
    const double candidateSamplesPerBar = outputSampleRate * 240.0 / project.tempo();
    std::uint64_t clipStartFrame = 0;
    std::uint64_t clipLengthFrames = 0;
    if (!barsToFrames(selectedClip->startBar, candidateSamplesPerBar, clipStartFrame) ||
        !barsToFrames(selectedClip->lengthBars, candidateSamplesPerBar, clipLengthFrames)) {
        error = "The audio clip timeline range is too large to render.";
        return false;
    }
    clipLengthFrames = std::max<std::uint64_t>(1, clipLengthFrames);
    if (clipLengthFrames > std::numeric_limits<std::uint64_t>::max() - clipStartFrame) {
        error = "The audio clip timeline end overflows the sample-frame range.";
        return false;
    }
    const std::uint64_t timelineEndFrame = clipStartFrame + clipLengthFrames;

    auto candidateBuffers =
        std::make_unique<Instrument::AudioBufferManager>(decodedMemoryLimitBytes_);
    const Instrument::AudioBufferLoadResult bufferResult =
        candidateBuffers->loadWavFile(selectedClip->sourcePath);
    if (!bufferResult.ok) {
        error = bufferResult.error;
        return false;
    }
    const Instrument::SampleData* const source = candidateBuffers->get(bufferResult.handle);
    if (source == nullptr) {
        error = "Decoded audio buffer could not be retained.";
        return false;
    }

    auto candidatePlayer = std::make_unique<Instrument::WavClipPlayer>();
    candidatePlayer->prepare(outputSampleRate);
    candidatePlayer->setSource(source);
    if (!candidatePlayer->setTimelineRange(clipStartFrame, clipLengthFrames, timelineEndFrame)) {
        error = "The decoded WAV could not be scheduled on the project timeline.";
        return false;
    }
    candidatePlayer->setGainPan(selectedTrack->mixer.volumeDb, selectedTrack->mixer.pan);

    // Caller must have stopped and quiesced the device before swapping callback state.
    player_.swap(candidatePlayer);
    buffers_.swap(candidateBuffers);
    outputSampleRate_ = outputSampleRate;
    samplesPerBar_ = candidateSamplesPerBar;
    projectTempo_ = project.tempo();
    clipStartBar_ = selectedClip->startBar;
    clipLengthBars_ = selectedClip->lengthBars;
    transport_.stop();
    transport_.setSampleRate(outputSampleRate);
    transport_.setTempo(projectTempo_);
    transport_.setTimeSignature(4, 4);
    transport_.seekSamples(0);
    masterPeak_.store(0.0F, std::memory_order_relaxed);
    loaded_ = true;
    return true;
}

void ProjectPlaybackSession::clear() noexcept {
    player_->stop();
    player_->setSource(nullptr);
    buffers_->clear();
    transport_.stop();
    transport_.seekSamples(0);
    transport_.setTempo(120.0);
    transport_.setTimeSignature(4, 4);
    loaded_ = false;
    masterPeak_.store(0.0F, std::memory_order_relaxed);
    samplesPerBar_ = outputSampleRate_ * 240.0 / 120.0;
    projectTempo_ = 120.0;
    clipStartBar_ = 0.0;
    clipLengthBars_ = 1.0;
}

void ProjectPlaybackSession::play() noexcept {
    if (loaded_) {
        player_->play();
        transport_.play();
    }
}

void ProjectPlaybackSession::stop() noexcept {
    player_->stop();
    transport_.stop();
}

bool ProjectPlaybackSession::seekBars(double bars) noexcept {
    if (!loaded_ || !std::isfinite(bars) || bars < 0.0) {
        return false;
    }
    std::uint64_t frame = 0;
    if (!barsToFrames(bars, samplesPerBar_, frame) ||
        frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return false;
    }
    player_->seekTimelineFrame(frame);
    transport_.seekSamples(static_cast<std::int64_t>(frame));
    return true;
}

bool ProjectPlaybackSession::setGainPan(double volumeDb, double pan) noexcept {
    if (!loaded_ || !std::isfinite(volumeDb) || !std::isfinite(pan)) {
        return false;
    }
    player_->setGainPan(volumeDb, pan);
    return true;
}

bool ProjectPlaybackSession::setMasterGainDb(double volumeDb) noexcept {
    if (!std::isfinite(volumeDb)) {
        return false;
    }
    volumeDb = std::clamp(volumeDb, -96.0, 12.0);
    masterGain_.store(static_cast<float>(std::pow(10.0, volumeDb / 20.0)),
                      std::memory_order_relaxed);
    return true;
}

std::string ProjectPlaybackSession::setProjectTempo(double bpm) {
    if (!std::isfinite(bpm)) {
        return "Project tempo must be a finite number.";
    }
    const double nextTempo = std::clamp(bpm, 20.0, 999.0);
    if (nextTempo == projectTempo_) {
        return {};
    }

    const double previousTempo = projectTempo_;
    projectTempo_ = nextTempo;
    if (!loaded_) {
        samplesPerBar_ = outputSampleRate_ * 240.0 / projectTempo_;
        transport_.setTempo(projectTempo_);
        transport_.setTimeSignature(4, 4);
        return {};
    }

    if (const std::string error = configureDeviceFormat(outputSampleRate_, 0, 2); !error.empty()) {
        projectTempo_ = previousTempo;
        return error;
    }
    return {};
}

bool ProjectPlaybackSession::isPlaying() const noexcept {
    return loaded_ && player_->isPlaying();
}

std::uint64_t ProjectPlaybackSession::positionFrames() const noexcept {
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, transport_.positionSamples()));
}

double ProjectPlaybackSession::positionBars() const noexcept {
    return transport_.positionBars();
}

std::string ProjectPlaybackSession::configureDeviceFormat(double sampleRate, int /*numInputs*/,
                                                          int /*numOutputs*/) {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 384000.0) {
        return "The audio driver negotiated an unsupported sample rate.";
    }
    if (!loaded_) {
        outputSampleRate_ = sampleRate;
        samplesPerBar_ = sampleRate * 240.0 / projectTempo_;
        transport_.setSampleRate(sampleRate);
        transport_.setTempo(projectTempo_);
        transport_.setTimeSignature(4, 4);
        return {};
    }

    const bool resumePlayback = player_->isPlaying();
    const double currentBar = transport_.positionBars();
    const double candidateSamplesPerBar = sampleRate * 240.0 / projectTempo_;
    std::uint64_t clipStartFrame = 0;
    std::uint64_t clipLengthFrames = 0;
    std::uint64_t resumeFrame = 0;
    if (!barsToFrames(clipStartBar_, candidateSamplesPerBar, clipStartFrame) ||
        !barsToFrames(clipLengthBars_, candidateSamplesPerBar, clipLengthFrames) ||
        !barsToFrames(currentBar, candidateSamplesPerBar, resumeFrame)) {
        return "The project timeline cannot be represented at the negotiated sample rate.";
    }
    clipLengthFrames = std::max<std::uint64_t>(1, clipLengthFrames);
    if (clipLengthFrames > std::numeric_limits<std::uint64_t>::max() - clipStartFrame ||
        resumeFrame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return "The project timeline exceeds the negotiated sample-frame range.";
    }
    const std::uint64_t timelineEndFrame = clipStartFrame + clipLengthFrames;

    player_->stop();
    transport_.stop();
    player_->prepare(sampleRate);
    if (!player_->setTimelineRange(clipStartFrame, clipLengthFrames, timelineEndFrame)) {
        return "The clip could not be rescheduled at the negotiated sample rate.";
    }
    outputSampleRate_ = sampleRate;
    samplesPerBar_ = candidateSamplesPerBar;
    transport_.setSampleRate(sampleRate);
    transport_.setTempo(projectTempo_);
    transport_.setTimeSignature(4, 4);
    player_->seekTimelineFrame(resumeFrame);
    transport_.seekSamples(static_cast<std::int64_t>(resumeFrame));
    if (resumePlayback) {
        player_->play();
        transport_.play();
    }
    return {};
}

void ProjectPlaybackSession::processBlock(const double* const* inputs, double* const* outputs,
                                          int numInputs, int numOutputs, int numSamples) {
    player_->processBlock(inputs, outputs, numInputs, numOutputs, numSamples);
    const std::uint64_t renderedPosition = player_->timelinePositionFrames();
    const auto maxPosition = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    transport_.publishAudioThreadPositionSamples(
        static_cast<std::int64_t>(std::min(renderedPosition, maxPosition)));
    transport_.publishAudioThreadState(player_->isPlaying()
                                           ? Aura::Transport::TransportState::Playing
                                           : Aura::Transport::TransportState::Stopped);
    const float masterGain = masterGain_.load(std::memory_order_relaxed);
    float blockPeak = 0.0F;
    if (outputs != nullptr && numOutputs > 0 && numSamples > 0) {
        for (int channel = 0; channel < numOutputs; ++channel) {
            if (outputs[channel] == nullptr) {
                continue;
            }
            for (int frame = 0; frame < numSamples; ++frame) {
                double sample = outputs[channel][frame];
                if (masterGain != 1.0F) {
                    sample *= static_cast<double>(masterGain);
                    outputs[channel][frame] = sample;
                }
                if (std::isfinite(sample)) {
                    blockPeak = std::max(blockPeak, static_cast<float>(std::abs(sample)));
                }
            }
        }
    }
    masterPeak_.store(blockPeak, std::memory_order_relaxed);
}

} // namespace Aura::Session
