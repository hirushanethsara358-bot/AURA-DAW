/// @file Mixer.cpp
/// @brief Implementation of the AURA mixer.

#include "Aura/Mixer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Aura::Mixer {

namespace {
constexpr double kPi = 3.14159265358979323846;

double dbToGain(double db) {
    return std::pow(10.0, db / 20.0);
}
} // namespace

ChannelStrip::ChannelStrip(std::string name) : name_(std::move(name)) {}

void ChannelStrip::setVolumeDb(double db) {
    std::lock_guard<std::mutex> lock(mutex_);
    volumeDb_ = std::clamp(db, -96.0, 12.0);
}

double ChannelStrip::volumeDb() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return volumeDb_;
}

void ChannelStrip::setPan(double pan) {
    std::lock_guard<std::mutex> lock(mutex_);
    pan_ = std::clamp(pan, -1.0, 1.0);
}

double ChannelStrip::pan() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pan_;
}

void ChannelStrip::setMute(bool mute) {
    std::lock_guard<std::mutex> lock(mutex_);
    mute_ = mute;
}

bool ChannelStrip::isMuted() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mute_;
}

void ChannelStrip::setSolo(bool solo) {
    std::lock_guard<std::mutex> lock(mutex_);
    solo_ = solo;
}

bool ChannelStrip::isSolo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return solo_;
}

void ChannelStrip::addInsert(std::unique_ptr<IInsertProcessor> insert) {
    std::lock_guard<std::mutex> lock(mutex_);
    inserts_.push_back(std::move(insert));
}

void ChannelStrip::clearInserts() {
    std::lock_guard<std::mutex> lock(mutex_);
    inserts_.clear();
}

std::size_t ChannelStrip::insertCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inserts_.size();
}

void ChannelStrip::setSends(std::vector<Send> sends) {
    std::lock_guard<std::mutex> lock(mutex_);
    sends_ = std::move(sends);
}

std::vector<Send> ChannelStrip::sends() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sends_;
}

void ChannelStrip::prepare(double sampleRate, int maxBlockSize) {
    std::lock_guard<std::mutex> lock(mutex_);
    scratchLeft_.assign(static_cast<std::size_t>(maxBlockSize), 0.0);
    scratchRight_.assign(static_cast<std::size_t>(maxBlockSize), 0.0);
    for (auto& insert : inserts_) {
        insert->prepare(sampleRate, maxBlockSize);
    }
}

void ChannelStrip::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& insert : inserts_) {
        insert->reset();
    }
    peak_.store(0.0, std::memory_order_relaxed);
}

void ChannelStrip::processStrip(const double* const* inputs, double* const* busBuffers,
                                std::size_t numBuses, double* mixLeft, double* mixRight,
                                int numSamples, bool audible) {
    // Snapshot parameters once per block (single lock, no locking in loop).
    double volumeDb;
    double pan;
    std::vector<Send> sends;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        volumeDb = volumeDb_;
        pan = pan_;
        sends = sends_;
    }

    double* stereo[2] = {scratchLeft_.data(), scratchRight_.data()};
    std::memcpy(stereo[0], inputs[0], sizeof(double) * static_cast<std::size_t>(numSamples));
    std::memcpy(stereo[1], inputs[1], sizeof(double) * static_cast<std::size_t>(numSamples));

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& insert : inserts_) {
            insert->process(stereo, 2, numSamples);
        }
    }

    // Sends are post-insert, pre-fader sends (standard default).
    for (const auto& send : sends) {
        if (!send.enabled || send.busIndex >= numBuses) {
            continue;
        }
        const double gain = dbToGain(send.levelDb);
        double* busL = busBuffers[send.busIndex * 2];
        double* busR = busBuffers[send.busIndex * 2 + 1];
        for (int i = 0; i < numSamples; ++i) {
            busL[i] += stereo[0][i] * gain;
            busR[i] += stereo[1][i] * gain;
        }
    }

    double peak = 0.0;
    if (audible) {
        // Equal-power pan law (-3 dB center).
        const double angle = (pan + 1.0) * kPi / 4.0;
        const double gainL = std::cos(angle) * dbToGain(volumeDb);
        const double gainR = std::sin(angle) * dbToGain(volumeDb);
        for (int i = 0; i < numSamples; ++i) {
            const double l = stereo[0][i] * gainL;
            const double r = stereo[1][i] * gainR;
            mixLeft[i] += l;
            mixRight[i] += r;
            peak = std::max({peak, std::abs(l), std::abs(r)});
        }
    }
    peak_.store(peak, std::memory_order_relaxed);
}

Mixer::Mixer() = default;

void Mixer::prepare(double sampleRate, int maxBlockSize, int numOutputs) {
    (void)numOutputs;
    std::lock_guard<std::mutex> lock(mutex_);
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    mixLeft_.assign(static_cast<std::size_t>(maxBlockSize), 0.0);
    mixRight_.assign(static_cast<std::size_t>(maxBlockSize), 0.0);
    busBuffers_.assign(numBuses_ * 2, std::vector<double>(static_cast<std::size_t>(maxBlockSize_)));
    for (auto& strip : strips_) {
        strip->prepare(sampleRate, maxBlockSize);
    }
    for (auto& insert : masterInserts_) {
        insert->prepare(sampleRate, maxBlockSize);
    }
}

void Mixer::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& strip : strips_) {
        strip->reset();
    }
    for (auto& insert : masterInserts_) {
        insert->reset();
    }
}

std::size_t Mixer::addStrip(std::string name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto strip = std::make_unique<ChannelStrip>(std::move(name));
    strip->prepare(sampleRate_, maxBlockSize_);
    strips_.push_back(std::move(strip));
    return strips_.size() - 1;
}

void Mixer::removeStrip(std::size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < strips_.size()) {
        strips_.erase(strips_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

std::size_t Mixer::stripCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return strips_.size();
}

ChannelStrip& Mixer::strip(std::size_t index) {
    return *strips_.at(index);
}

void Mixer::setNumBuses(std::size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    numBuses_ = std::min<std::size_t>(count, 16);
    busBuffers_.assign(numBuses_ * 2, std::vector<double>(static_cast<std::size_t>(maxBlockSize_)));
}

std::size_t Mixer::busCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return numBuses_;
}

void Mixer::setMasterVolumeDb(double db) {
    std::lock_guard<std::mutex> lock(mutex_);
    masterVolumeDb_ = std::clamp(db, -96.0, 12.0);
}

double Mixer::masterVolumeDb() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return masterVolumeDb_;
}

void Mixer::addMasterInsert(std::unique_ptr<IInsertProcessor> insert) {
    std::lock_guard<std::mutex> lock(mutex_);
    insert->prepare(sampleRate_, maxBlockSize_);
    masterInserts_.push_back(std::move(insert));
}

void Mixer::processBlock(const double* const* const* stripInputs, double* const* outputs,
                         int numOutputs, int numSamples) {
    std::fill(mixLeft_.begin(), mixLeft_.end(), 0.0);
    std::fill(mixRight_.begin(), mixRight_.end(), 0.0);
    for (auto& bus : busBuffers_) {
        std::fill(bus.begin(), bus.end(), 0.0);
    }

    bool anySolo = false;
    for (const auto& strip : strips_) {
        if (strip->isSolo()) {
            anySolo = true;
            break;
        }
    }

    std::vector<double*> busPtrs(busBuffers_.size());
    for (std::size_t i = 0; i < busBuffers_.size(); ++i) {
        busPtrs[i] = busBuffers_[i].data();
    }

    for (std::size_t s = 0; s < strips_.size(); ++s) {
        const bool audible =
            anySolo ? strips_[s]->isSolo() : !strips_[s]->isMuted();
        strips_[s]->processStrip(stripInputs[s], busPtrs.data(), numBuses_, mixLeft_.data(),
                                 mixRight_.data(), numSamples, audible);
    }

    // Sum buses into the mix (post-fader returns at unity).
    for (std::size_t b = 0; b < numBuses_; ++b) {
        const double* busL = busBuffers_[b * 2].data();
        const double* busR = busBuffers_[b * 2 + 1].data();
        for (int i = 0; i < numSamples; ++i) {
            mixLeft_[i] += busL[i];
            mixRight_[i] += busR[i];
        }
    }

    double* masterStereo[2] = {mixLeft_.data(), mixRight_.data()};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& insert : masterInserts_) {
            insert->process(masterStereo, 2, numSamples);
        }
    }

    const double masterGain = dbToGain(masterVolumeDb());
    double peak = 0.0;
    double* outL = numOutputs > 0 ? outputs[0] : nullptr;
    double* outR = numOutputs > 1 ? outputs[1] : nullptr;
    for (int i = 0; i < numSamples; ++i) {
        const double l = mixLeft_[i] * masterGain;
        const double r = mixRight_[i] * masterGain;
        if (outL != nullptr) {
            outL[i] = l;
        }
        if (outR != nullptr) {
            outR[i] = r;
        }
        peak = std::max({peak, std::abs(l), std::abs(r)});
    }
    // Clear any extra outputs.
    for (int ch = 2; ch < numOutputs; ++ch) {
        std::memset(outputs[ch], 0, sizeof(double) * static_cast<std::size_t>(numSamples));
    }
    masterPeak_.store(peak, std::memory_order_relaxed);
}

} // namespace Aura::Mixer
