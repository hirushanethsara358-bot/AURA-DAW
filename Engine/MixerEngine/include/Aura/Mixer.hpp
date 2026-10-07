#pragma once

/// @file Mixer.hpp
/// @brief Professional mixer: channel strips, buses, inserts, sends, master.
///
/// Signal flow per block:
///   strip input → inserts → fader/pan → mute/solo → sends → bus sums
///   bus sums + strips → master fader → outputs + peak meters.

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace Aura::Mixer {

/// @brief Insert-effect interface hosted on strips, buses and master.
class IInsertProcessor {
  public:
    virtual ~IInsertProcessor() = default;
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    virtual void process(double* const* channels, int numChannels, int numSamples) = 0;
    virtual void reset() = 0;
};

/// @brief A send routing from a strip to an effect bus.
struct Send {
    std::size_t busIndex = 0;
    double levelDb = 0.0;
    bool enabled = true;
};

/// @brief A single mixer channel strip (audio / instrument / bus return).
class ChannelStrip {
  public:
    explicit ChannelStrip(std::string name = "Channel");

    [[nodiscard]] const std::string& name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }

    void setVolumeDb(double db);
    [[nodiscard]] double volumeDb() const;
    void setPan(double pan); ///< -1 (left) .. +1 (right), equal-power law.
    [[nodiscard]] double pan() const;
    void setMute(bool mute);
    [[nodiscard]] bool isMuted() const;
    void setSolo(bool solo);
    [[nodiscard]] bool isSolo() const;

    void addInsert(std::unique_ptr<IInsertProcessor> insert);
    void clearInserts();
    [[nodiscard]] std::size_t insertCount() const;

    void setSends(std::vector<Send> sends);
    [[nodiscard]] std::vector<Send> sends() const;

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    /// @brief Last-block peak (linear) for metering.
    [[nodiscard]] double peak() const { return peak_.load(std::memory_order_relaxed); }

    // Internal processing entry used by Mixer (locks avoided on audio path
    // by snapshotting parameters under mutex before the block loop).
    void processStrip(const double* const* inputs, double* const* busBuffers, std::size_t numBuses,
                      double* mixLeft, double* mixRight, int numSamples, bool audible);

  private:
    std::string name_;
    mutable std::mutex mutex_;
    double volumeDb_ = 0.0;
    double pan_ = 0.0;
    bool mute_ = false;
    bool solo_ = false;
    std::vector<std::unique_ptr<IInsertProcessor>> inserts_;
    std::vector<Send> sends_;
    std::atomic<double> peak_{0.0};
    std::vector<double> scratchLeft_;
    std::vector<double> scratchRight_;
};

/// @brief The mixer: owns strips, buses and the master chain.
class Mixer {
  public:
    Mixer();

    void prepare(double sampleRate, int maxBlockSize, int numOutputs = 2);
    void reset();

    /// @brief Creates a strip and returns its index.
    std::size_t addStrip(std::string name);
    void removeStrip(std::size_t index);
    [[nodiscard]] std::size_t stripCount() const;
    [[nodiscard]] ChannelStrip& strip(std::size_t index);

    void setNumBuses(std::size_t count);
    [[nodiscard]] std::size_t busCount() const;

    void setMasterVolumeDb(double db);
    [[nodiscard]] double masterVolumeDb() const;
    void addMasterInsert(std::unique_ptr<IInsertProcessor> insert);

    /// @brief Renders one block.
    /// @param stripInputs Per-strip stereo input buffers [strip][0=L,1=R].
    /// @param outputs     Output channel buffers (L, R, ...).
    void processBlock(const double* const* const* stripInputs, double* const* outputs,
                      int numOutputs, int numSamples);

    [[nodiscard]] double masterPeak() const { return masterPeak_.load(std::memory_order_relaxed); }

  private:
    mutable std::mutex mutex_;
    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 512;
    std::vector<std::unique_ptr<ChannelStrip>> strips_;
    std::size_t numBuses_ = 2;
    double masterVolumeDb_ = 0.0;
    std::vector<std::unique_ptr<IInsertProcessor>> masterInserts_;
    std::vector<std::vector<double>> busBuffers_; // [bus][L...,R...] interleaved pairs
    std::vector<double> mixLeft_;
    std::vector<double> mixRight_;
    std::atomic<double> masterPeak_{0.0};
};

} // namespace Aura::Mixer
