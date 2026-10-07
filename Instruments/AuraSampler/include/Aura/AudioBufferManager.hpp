#pragma once

/// @file AudioBufferManager.hpp
/// @brief Control-thread WAV decode/cache and bounded decoded PCM ownership.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Aura/Sampler.hpp"

namespace Aura::Instrument {

struct AudioBufferLoadResult {
    bool ok = false;
    std::uint64_t handle = 0;
    std::uint64_t decodedBytes = 0;
    std::string error;
};

/// @brief Owns decoded WAV buffers under a decoded-PCM memory budget.
///
/// All methods are control-thread operations, never audio-callback operations.
/// A caller must stop playback before releasing or clearing a buffer whose
/// pointer may currently be used by an audio callback. Repeated loads of the
/// same path reuse the cached buffer until it is explicitly released.
class AudioBufferManager {
  public:
    explicit AudioBufferManager(std::uint64_t memoryLimitBytes = kDefaultMaxDecodedWavBytes);

    /// @brief Decode a supported WAV and retain it, or return a cached handle.
    [[nodiscard]] AudioBufferLoadResult loadWavFile(const std::string& path);
    /// @brief Retrieve a stable pointer until that handle is released or cleared.
    [[nodiscard]] const SampleData* get(std::uint64_t handle) const noexcept;
    /// @brief Release one retained buffer. Returns false for an unknown handle.
    bool release(std::uint64_t handle);
    /// @brief Release all retained buffers. Stop playback before calling.
    void clear();

    /// @brief Change the decoded-PCM budget; cannot shrink below current use.
    bool setMemoryLimit(std::uint64_t memoryLimitBytes) noexcept;
    [[nodiscard]] std::uint64_t memoryLimitBytes() const noexcept { return memoryLimitBytes_; }
    [[nodiscard]] std::uint64_t decodedBytesInUse() const noexcept { return decodedBytesInUse_; }
    [[nodiscard]] std::size_t bufferCount() const noexcept { return buffers_.size(); }

  private:
    struct Entry {
        std::uint64_t handle = 0;
        std::uint64_t decodedBytes = 0;
        std::string sourcePath;
        SampleData data;
    };

    std::vector<std::unique_ptr<Entry>> buffers_;
    std::uint64_t memoryLimitBytes_;
    std::uint64_t decodedBytesInUse_ = 0;
    std::uint64_t nextHandle_ = 1;
};

} // namespace Aura::Instrument
