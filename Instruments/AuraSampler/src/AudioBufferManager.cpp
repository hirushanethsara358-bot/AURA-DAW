/// @file AudioBufferManager.cpp
/// @brief Bounded decoded WAV cache for control/session operations.

#include "Aura/AudioBufferManager.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace Aura::Instrument {

AudioBufferManager::AudioBufferManager(std::uint64_t memoryLimitBytes)
    : memoryLimitBytes_(memoryLimitBytes) {}

AudioBufferLoadResult AudioBufferManager::loadWavFile(const std::string& path) {
    AudioBufferLoadResult result;
    if (path.empty()) {
        result.error = "WAV path cannot be empty.";
        return result;
    }

    const auto cached = std::find_if(buffers_.begin(), buffers_.end(), [&path](const auto& entry) {
        return entry->sourcePath == path;
    });
    if (cached != buffers_.end()) {
        result.ok = true;
        result.handle = (*cached)->handle;
        result.decodedBytes = (*cached)->decodedBytes;
        return result;
    }

    if (nextHandle_ == 0) {
        result.error = "Audio buffer handle space is exhausted.";
        return result;
    }
    const std::uint64_t availableBytes = memoryLimitBytes_ - decodedBytesInUse_;
    SampleData decoded;
    SampleLoadResult loadResult;
    try {
        loadResult = parseWavFile(path, decoded, availableBytes);
    } catch (const std::bad_alloc&) {
        result.error = "Insufficient memory while decoding WAV: " + path;
        return result;
    }
    if (!loadResult.ok) {
        result.error = std::move(loadResult.error);
        return result;
    }

    const std::uint64_t channelCount = decoded.isStereo() ? 2U : 1U;
    const std::uint64_t frames = decoded.frames();
    if (frames > std::numeric_limits<std::uint64_t>::max() / (channelCount * sizeof(double))) {
        result.error = "Decoded WAV size overflow: " + path;
        return result;
    }
    const std::uint64_t decodedBytes = frames * channelCount * sizeof(double);
    if (decodedBytes > availableBytes) {
        result.error = "Decoded WAV exceeds the remaining audio-buffer memory budget: " + path;
        return result;
    }

    try {
        auto entry = std::make_unique<Entry>();
        entry->handle = nextHandle_;
        entry->decodedBytes = decodedBytes;
        entry->sourcePath = path;
        entry->data = std::move(decoded);
        buffers_.push_back(std::move(entry));
    } catch (const std::bad_alloc&) {
        result.error = "Insufficient memory while retaining WAV buffer: " + path;
        return result;
    }

    result.ok = true;
    result.handle = nextHandle_;
    result.decodedBytes = decodedBytes;
    ++nextHandle_;
    decodedBytesInUse_ += decodedBytes;
    return result;
}

const SampleData* AudioBufferManager::get(std::uint64_t handle) const noexcept {
    const auto found = std::find_if(buffers_.begin(), buffers_.end(), [handle](const auto& entry) {
        return entry->handle == handle;
    });
    return found == buffers_.end() ? nullptr : &(*found)->data;
}

bool AudioBufferManager::release(std::uint64_t handle) {
    const auto found = std::find_if(buffers_.begin(), buffers_.end(), [handle](const auto& entry) {
        return entry->handle == handle;
    });
    if (found == buffers_.end()) {
        return false;
    }
    decodedBytesInUse_ -= (*found)->decodedBytes;
    buffers_.erase(found);
    return true;
}

void AudioBufferManager::clear() {
    buffers_.clear();
    decodedBytesInUse_ = 0;
}

bool AudioBufferManager::setMemoryLimit(std::uint64_t memoryLimitBytes) noexcept {
    if (memoryLimitBytes < decodedBytesInUse_) {
        return false;
    }
    memoryLimitBytes_ = memoryLimitBytes;
    return true;
}

} // namespace Aura::Instrument
