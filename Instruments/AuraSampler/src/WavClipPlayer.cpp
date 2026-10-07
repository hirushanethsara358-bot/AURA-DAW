/// @file WavClipPlayer.cpp
/// @brief Minimal allocation-free one-clip renderer for the 0.1 playback path.

#include "Aura/WavClipPlayer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace Aura::Instrument {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kSqrtHalf = 0.7071067811865475F;

double dbToGain(double db) noexcept {
    return std::pow(10.0, db / 20.0);
}

} // namespace

void WavClipPlayer::prepare(double outputSampleRate) noexcept {
    if (std::isfinite(outputSampleRate) && outputSampleRate > 0.0) {
        outputSampleRate_ = outputSampleRate;
    } else {
        outputSampleRate_ = 48000.0;
    }
    if (source_ != nullptr && source_->sampleRate > 0.0) {
        sourceFramesPerOutputFrame_ = source_->sampleRate / outputSampleRate_;
    }
    if (!customTimelineRange_) {
        updateDefaultTimelineRange();
    }
    sourcePosition_ = 0.0;
    timelinePosition_ = 0;
    requestedSeekFrame_.store(0, std::memory_order_relaxed);
    seekRequested_.store(false, std::memory_order_relaxed);
    playheadSourceFrame_.store(0, std::memory_order_relaxed);
    timelinePositionPublished_.store(0, std::memory_order_relaxed);
}

void WavClipPlayer::setSource(const SampleData* source) noexcept {
    playing_.store(false, std::memory_order_release);
    seekRequested_.store(false, std::memory_order_relaxed);
    customTimelineRange_ = false;
    if (source == nullptr || source->frames() == 0 || !std::isfinite(source->sampleRate) ||
        source->sampleRate <= 0.0 ||
        (!source->right.empty() && source->right.size() != source->left.size())) {
        source_ = nullptr;
        sourceFramesPerOutputFrame_ = 1.0;
        clipStartFrame_ = 0;
        clipEndFrame_ = 0;
        timelineEndFrame_ = 0;
    } else {
        source_ = source;
        sourceFramesPerOutputFrame_ = source->sampleRate / outputSampleRate_;
        updateDefaultTimelineRange();
    }
    sourcePosition_ = 0.0;
    timelinePosition_ = 0;
    requestedSeekFrame_.store(0, std::memory_order_relaxed);
    playheadSourceFrame_.store(0, std::memory_order_relaxed);
    timelinePositionPublished_.store(0, std::memory_order_relaxed);
}

void WavClipPlayer::updateDefaultTimelineRange() noexcept {
    if (source_ == nullptr || source_->frames() == 0 ||
        !std::isfinite(sourceFramesPerOutputFrame_) || sourceFramesPerOutputFrame_ <= 0.0) {
        clipStartFrame_ = 0;
        clipEndFrame_ = 0;
        timelineEndFrame_ = 0;
        return;
    }
    const double outputFrames =
        std::ceil(static_cast<double>(source_->frames()) / sourceFramesPerOutputFrame_);
    if (!std::isfinite(outputFrames) || outputFrames < 1.0 ||
        outputFrames > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        clipStartFrame_ = 0;
        clipEndFrame_ = 0;
        timelineEndFrame_ = 0;
        return;
    }
    clipStartFrame_ = 0;
    clipEndFrame_ = static_cast<std::uint64_t>(outputFrames);
    timelineEndFrame_ = clipEndFrame_;
}

bool WavClipPlayer::setTimelineRange(std::uint64_t clipStartFrame, std::uint64_t clipLengthFrames,
                                     std::uint64_t timelineEndFrame) noexcept {
    if (playing_.load(std::memory_order_acquire) || source_ == nullptr || clipLengthFrames == 0 ||
        clipStartFrame >= timelineEndFrame) {
        return false;
    }
    const std::uint64_t unclampedEnd =
        clipLengthFrames > std::numeric_limits<std::uint64_t>::max() - clipStartFrame
            ? std::numeric_limits<std::uint64_t>::max()
            : clipStartFrame + clipLengthFrames;
    const std::uint64_t clipEndFrame = std::min(unclampedEnd, timelineEndFrame);
    if (clipEndFrame <= clipStartFrame) {
        return false;
    }

    clipStartFrame_ = clipStartFrame;
    clipEndFrame_ = clipEndFrame;
    timelineEndFrame_ = timelineEndFrame;
    customTimelineRange_ = true;
    timelinePosition_ = std::min(timelinePosition_, timelineEndFrame_);
    timelinePositionPublished_.store(timelinePosition_, std::memory_order_relaxed);
    sourcePosition_ = timelinePosition_ <= clipStartFrame_
                          ? 0.0
                          : std::min(static_cast<double>(source_->frames()),
                                     static_cast<double>(timelinePosition_ - clipStartFrame_) *
                                         sourceFramesPerOutputFrame_);
    playheadSourceFrame_.store(static_cast<std::uint64_t>(sourcePosition_),
                               std::memory_order_relaxed);
    return true;
}

void WavClipPlayer::seekTimelineFrame(std::uint64_t frame) noexcept {
    requestedSeekFrame_.store(frame, std::memory_order_relaxed);
    seekRequested_.store(true, std::memory_order_release);
}

void WavClipPlayer::setGainPan(double volumeDb, double pan) noexcept {
    if (!std::isfinite(volumeDb)) {
        volumeDb = 0.0;
    }
    if (!std::isfinite(pan)) {
        pan = 0.0;
    }
    volumeDb = std::clamp(volumeDb, -96.0, 12.0);
    pan = std::clamp(pan, -1.0, 1.0);

    const double gain = dbToGain(volumeDb);
    const double angle = (pan + 1.0) * kPi / 4.0;
    leftGain_.store(static_cast<float>(gain * std::cos(angle)), std::memory_order_relaxed);
    rightGain_.store(static_cast<float>(gain * std::sin(angle)), std::memory_order_relaxed);
}

void WavClipPlayer::play() noexcept {
    if (source_ == nullptr || source_->frames() == 0 || timelineEndFrame_ == 0 ||
        !std::isfinite(sourceFramesPerOutputFrame_) || sourceFramesPerOutputFrame_ <= 0.0) {
        return;
    }
    if (timelinePositionPublished_.load(std::memory_order_relaxed) >= timelineEndFrame_) {
        seekTimelineFrame(0);
    }
    playing_.store(true, std::memory_order_release);
}

void WavClipPlayer::stop() noexcept {
    playing_.store(false, std::memory_order_release);
}

void WavClipPlayer::processBlock(const double* const* /*inputs*/, double* const* outputs,
                                 int /*numInputs*/, int numOutputs, int numSamples) {
    if (outputs == nullptr || numOutputs <= 0 || numSamples <= 0) {
        return;
    }
    for (int channel = 0; channel < numOutputs; ++channel) {
        if (outputs[channel] != nullptr) {
            std::fill_n(outputs[channel], numSamples, 0.0);
        }
    }

    if (seekRequested_.exchange(false, std::memory_order_acquire)) {
        timelinePosition_ =
            std::min(requestedSeekFrame_.load(std::memory_order_relaxed), timelineEndFrame_);
    }
    if (!playing_.load(std::memory_order_acquire)) {
        timelinePositionPublished_.store(timelinePosition_, std::memory_order_relaxed);
        return;
    }

    const SampleData* const source = source_;
    if (source == nullptr || source->frames() == 0 || timelineEndFrame_ == 0) {
        playing_.store(false, std::memory_order_release);
        return;
    }

    const std::size_t sourceFrames = source->frames();
    const float gainL = leftGain_.load(std::memory_order_relaxed);
    const float gainR = rightGain_.load(std::memory_order_relaxed);
    double* const outputL = outputs[0];
    double* const outputR = numOutputs > 1 ? outputs[1] : nullptr;

    for (int frame = 0; frame < numSamples; ++frame) {
        if (timelinePosition_ >= timelineEndFrame_) {
            playing_.store(false, std::memory_order_release);
            break;
        }
        if (timelinePosition_ >= clipStartFrame_ && timelinePosition_ < clipEndFrame_) {
            const std::uint64_t clipOffset = timelinePosition_ - clipStartFrame_;
            const double sourcePosition =
                static_cast<double>(clipOffset) * sourceFramesPerOutputFrame_;
            if (sourcePosition < static_cast<double>(sourceFrames)) {
                const std::size_t first = static_cast<std::size_t>(sourcePosition);
                const std::size_t second = std::min(first + 1, sourceFrames - 1);
                const double fraction = sourcePosition - static_cast<double>(first);
                const double left =
                    source->left[first] * (1.0 - fraction) + source->left[second] * fraction;
                const double right = source->isStereo() ? source->right[first] * (1.0 - fraction) +
                                                              source->right[second] * fraction
                                                        : left;

                if (numOutputs > 1) {
                    if (outputL != nullptr) {
                        outputL[frame] = left * static_cast<double>(gainL);
                    }
                    if (outputR != nullptr) {
                        outputR[frame] = right * static_cast<double>(gainR);
                    }
                } else if (outputL != nullptr) {
                    outputL[frame] =
                        (left * static_cast<double>(gainL) + right * static_cast<double>(gainR)) *
                        kSqrtHalf;
                }
            }
        }
        ++timelinePosition_;
    }

    if (timelinePosition_ >= timelineEndFrame_) {
        timelinePosition_ = timelineEndFrame_;
        playing_.store(false, std::memory_order_release);
    }
    timelinePositionPublished_.store(timelinePosition_, std::memory_order_relaxed);
    if (timelinePosition_ <= clipStartFrame_) {
        sourcePosition_ = 0.0;
    } else {
        const double clipOffset = static_cast<double>(
            std::min(timelinePosition_ - clipStartFrame_, clipEndFrame_ - clipStartFrame_));
        sourcePosition_ =
            std::min(static_cast<double>(sourceFrames), clipOffset * sourceFramesPerOutputFrame_);
    }
    playheadSourceFrame_.store(static_cast<std::uint64_t>(sourcePosition_),
                               std::memory_order_relaxed);
}

} // namespace Aura::Instrument
