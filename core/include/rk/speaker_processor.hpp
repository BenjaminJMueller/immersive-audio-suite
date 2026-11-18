// Raumklang core: the loudspeaker output stage on its own (used by the "Raum" plugin
// on the DAW master bus): per-speaker calibration (gain, delay), speaker test noise,
// headphone preview, master gain and the protective limiter.
//
// Threading contract as for rk::Engine: prepare() with audio stopped, setLayout()
// and collectGarbage() from a non-audio thread, process() on the audio thread.
#pragma once

#include "rk/binaural.hpp"
#include "rk/dsp.hpp"
#include "rk/layout.hpp"
#include "rk/rt_swap.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace rk {

class SpeakerProcessor {
public:
    SpeakerProcessor();
    ~SpeakerProcessor();

    void prepare(double sampleRate, int maxBlockSize);
    void setLayout(SpeakerLayout layout, bool headphones);
    void collectGarbage();

    std::atomic<float> masterGainDb{0.0f};
    std::atomic<bool> limiterEnabled{true};
    std::atomic<int> speakerTest{-1};

    /// In place: channel m carries speaker m. With headphones on, channels 0/1 receive
    /// the binaural preview and all other channels are cleared.
    void process(float* const* io, int numChannels, int numFrames) noexcept;

    float takePeak(int channel) noexcept;
    uint32_t takeLimiterEvents() noexcept { return limiterEvents_.exchange(0); }

private:
    struct Runtime;
    std::unique_ptr<Runtime> build(const SpeakerLayout& layout, bool headphones) const;
    void processChunk(float* const* io, int numChannels, int offset, int n) noexcept;

    double sampleRate_ = 48000.0;
    int maxBlock_ = 512;
    std::mutex mutex_;
    SpeakerLayout layout_;
    bool headphones_ = false;
    RtSwap<Runtime> runtime_;
    std::vector<float> scratch_; // kMaxSpeakers x maxBlock + 2 x maxBlock
    std::array<PeakLimiter, kMaxSpeakers> limiter_{};
    PinkNoise noise_;
    float masterCur_ = 1.0f;
    float fade_ = 0.0f;
    bool fadingOut_ = false;
    std::array<std::atomic<float>, kMaxSpeakers> peaks_;
    std::atomic<uint32_t> limiterEvents_{0};
};

} // namespace rk
