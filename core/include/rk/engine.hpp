// Raumklang core: the realtime spatialisation engine.
//
// Threading contract
//   * prepare() and the destructor: only while no audio callback is running.
//   * Every set*/source() call: any non-audio thread (typically the UI thread).
//     They never block the audio thread; values are picked up at the next block.
//   * process(): the audio thread. Never allocates, locks, throws or blocks.
//   * collectGarbage(): call regularly from the UI thread (e.g. 10x per second).
#pragma once

#include "rk/binaural.hpp"
#include "rk/dsp.hpp"
#include "rk/layout.hpp"
#include "rk/panners.hpp"
#include "rk/reverb.hpp"
#include "rk/rt_swap.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace rk {

enum class OutputMode : int { Speakers = 0, Headphones = 1 };

/// Per-source parameters, written by the UI, read lock-free by the audio thread.
struct SourceControl {
    std::atomic<bool> active{false};
    std::atomic<int> inputChannel{-1}; // -1: use the source index
    std::atomic<float> x{0.0f}, y{2.0f}, z{0.0f};
    std::atomic<float> gainDb{0.0f};
    std::atomic<float> spread{0.0f};         // 0..1
    std::atomic<float> reverbSendDb{-12.0f}; // -80..+6
    std::atomic<float> distanceAmount{1.0f}; // 0 = no distance cues, 1 = natural
    /// Where the source faces, relative to the listener: 0 = towards the listener, 180 = away.
    std::atomic<float> yawDeg{0.0f};
    /// 0 = radiates evenly in all directions; 1 = strongly directed (facing away: about
    /// 12 dB quieter and much darker in the direct sound; the room part stays).
    std::atomic<float> directivity{0.0f};
    /// Delays the source by its distance (about 3 ms per metre), so movement shifts the pitch.
    std::atomic<bool> doppler{false};
    std::atomic<bool> mute{false};
    std::atomic<bool> solo{false};
    std::atomic<uint32_t> version{0};

    void setPosition(Vec3 p) noexcept;
    Vec3 position() const noexcept;
    /// Call after changing any value so that the audio thread notices.
    void touch() noexcept { version.fetch_add(1, std::memory_order_release); }
};

struct EngineConfig {
    SpeakerLayout layout;
    PannerKind panner = PannerKind::VBAP;
    DbapPanner::Params dbap;
    int ambiOrder = 3;
    OutputMode outputMode = OutputMode::Speakers;
};

class Engine {
public:
    static constexpr int kDefaultMaxBlock = 512;

    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // ---- lifecycle (audio stopped) -----------------------------------------
    void prepare(double sampleRate, int maxBlockSize);
    double sampleRate() const noexcept { return sampleRate_; }

    // ---- control (any non-audio thread) ------------------------------------
    /// Builds the new layout/panner off the audio thread and hands it over with a short
    /// fade, so changing the setup while playing never clicks or crashes.
    void setConfig(EngineConfig config);
    EngineConfig config() const;
    SourceControl& source(int index) noexcept;
    const SourceControl& source(int index) const noexcept;

    std::atomic<float> masterGainDb{0.0f};
    std::atomic<float> reverbWetDb{-6.0f};
    std::atomic<bool> limiterEnabled{true};
    std::atomic<float> limiterCeilingDb{-0.1f};  // dBFS
    std::atomic<float> limiterReleaseMs{80.0f};
    std::atomic<float> subLevelDb{-6.0f};
    /// Plays pink noise on one speaker (index into the layout) for setup checks. -1 = off.
    std::atomic<int> speakerTest{-1};
    void setReverb(const ReverbParams& p) noexcept;
    ReverbParams reverb() const noexcept;

    void collectGarbage();

    // ---- metering (read by the UI, reset on read) --------------------------
    float takeOutputPeak(int channel) noexcept;
    /// Largest limiter gain reduction on output `channel` since the last call, in dB (>= 0).
    float takeLimiterReduction(int channel) noexcept;
    float takeSourcePeak(int source) noexcept;
    /// Number of times the protective limiter had to act since the last call.
    uint32_t takeLimiterEvents() noexcept { return limiterEvents_.exchange(0); }
    /// Number of non-finite input samples that were replaced by silence.
    uint32_t takeInvalidInputEvents() noexcept { return invalidInputs_.exchange(0); }

    // ---- audio thread ------------------------------------------------------
    /// Inputs and outputs may alias (in-place processing is supported).
    /// Unused outputs are cleared. Any numFrames is accepted.
    void process(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                 int numFrames) noexcept;

    /// Number of channels the current configuration produces (speakers or 2 for headphones).
    int numOutputChannels() const noexcept { return numOutputChannels_.load(std::memory_order_relaxed); }

private:
    struct Runtime;
    std::unique_ptr<Runtime> buildRuntime(const EngineConfig& cfg) const;
    void processChunk(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                      int offset, int n) noexcept;
    void resetSourceState() noexcept;

    double sampleRate_ = 48000.0;
    int maxBlock_ = kDefaultMaxBlock;

    mutable std::mutex configMutex_; // never touched by the audio thread
    EngineConfig config_;
    RtSwap<Runtime> runtime_;
    std::atomic<int> numOutputChannels_{0};

    std::array<SourceControl, kMaxSources> sources_;

    // Reverb parameters are a small POD guarded by a seqlock-like version counter.
    std::atomic<uint32_t> reverbVersion_{0};
    std::array<std::atomic<float>, 8> reverbParams_;
    uint32_t reverbSeen_ = 0;
    FdnReverb reverb_;

    // Per-source audio-thread state.
    struct SourceState {
        uint32_t seenVersion = 0xffffffffu;
        std::array<float, kMaxSpeakers> pan{};    // panner output (energy normalised)
        std::array<float, kMaxSpeakers> cur{};    // current applied gain
        std::array<float, kMaxSpeakers> target{}; // gain we ramp to
        std::array<float, kMaxSpeakers> step{};
        int rampLeft = 0;
        float distanceGain = 1.0f;
        float airCoef = 1.0f;
        float airState = 0.0f;
        float dirGain = 1.0f;    // directivity, part of the direct gain
        float dirCoef = 1.0f;    // directivity low-pass (1 = off)
        float dirState = 0.0f;
        int dopplerSlot = -1;    // index into the Doppler delay pool, -1 = none
        float dopplerCur = 0.0f; // current delay in samples
        float dopplerTarget = 0.0f;
        float sendCur = 0.0f;
        float subCur = 0.0f;
        bool hadSignal = false;
    };
    std::vector<SourceState> state_;

    // Scratch buffers (allocated in prepare).
    std::vector<float> inputCopy_;   // kMaxInputs x maxBlock
    std::vector<float> mix_;         // kMaxSpeakers x maxBlock
    std::vector<float> reverbBus_;   // maxBlock
    std::vector<float> subBus_;      // maxBlock
    std::vector<float> srcTmp_;      // maxBlock
    std::vector<float> outTmp_;      // 2 x maxBlock (headphones)
    std::vector<float> directTmp_;   // maxBlock: direct sound after directivity
    // Doppler: a small pool of delay lines shared by the sources that use it.
    static constexpr int kDopplerSlots = 16;
    std::vector<float> dopplerLines_; // kDopplerSlots x dopplerLen_
    int dopplerLen_ = 0;
    std::array<int, kDopplerSlots> dopplerPos_{};
    std::array<bool, kDopplerSlots> dopplerUsed_{};
    std::array<float*, kMaxSpeakers> mixPtr_{};

    // Output stage state.
    std::array<PeakLimiter, kMaxSpeakers> limiter_{};
    PinkNoise testNoise_;
    float masterCur_ = 1.0f;
    float wetCur_ = 0.0f;
    enum class Fade { Running, Out, In } fade_ = Fade::Running;
    float fadeGain_ = 1.0f;
    float subLp_[2][2] = {};

    std::array<std::atomic<float>, kMaxSpeakers> outPeak_;
    std::array<std::atomic<float>, kMaxSpeakers> limiterGr_; // 1 - smallest gain since last take
    std::array<std::atomic<float>, kMaxSources> srcPeak_;
    std::atomic<uint32_t> limiterEvents_{0};
    std::atomic<uint32_t> invalidInputs_{0};
};

} // namespace rk
