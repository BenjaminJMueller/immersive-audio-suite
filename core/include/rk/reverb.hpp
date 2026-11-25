// Raumklang core: multichannel feedback delay network reverb.
#pragma once

#include <algorithm>
#include <array>
#include <vector>

namespace rk {

struct ReverbParams {
    float decaySec = 1.8f;     // RT60 at low/mid frequencies, 0.2..12 s
    float dampingHz = 6000.0f; // high frequencies above this decay faster, 1k..20k
    float preDelayMs = 15.0f;  // 0..250 ms
    float size = 1.0f;         // scales the delay lengths, 0.3..2
    float lowRatio = 1.0f;     // decay below ~250 Hz relative to decaySec, 0.3..3
    float earlyDb = -6.0f;     // early reflections, -80 (off)..+6 dB
    float clusterDb = -9.0f;   // dense reflection cluster, -80..+6 dB
    float tailDb = 0.0f;       // late diffuse tail, -80..+6 dB
};

/// Reverb time (RT60, seconds) the tail has at `hz`, as the network computes it.
/// Used by the UI to draw the decay curve from the real parameters.
float rt60At(const ReverbParams& p, float hz, double sampleRate = 48000.0) noexcept;

/// The damping frequency that gives the tail `rtSec` at `hz` (other parameters fixed).
/// Clamped into the dampingHz range when the target cannot be reached.
float dampingForRt(const ReverbParams& p, float hz, float rtSec, double sampleRate = 48000.0) noexcept;

/// Fixed reflection patterns at size 1, in ms after the pre-delay (scaled by size).
inline constexpr int kEarlyTaps = 8;
inline constexpr float kEarlyMs[kEarlyTaps] = {7.1f, 11.3f, 17.9f, 23.3f, 31.7f, 41.9f, 53.3f, 67.1f};
inline constexpr float kEarlyGain[kEarlyTaps] = {0.80f, 0.72f, 0.66f, 0.58f, 0.50f, 0.42f, 0.35f, 0.28f};
inline constexpr int kClusterTaps = 16;
inline constexpr float kClusterMs[kClusterTaps] = {21.3f, 27.9f, 33.1f, 39.7f, 46.3f, 52.9f, 59.1f, 66.7f,
                                                   73.3f, 81.1f, 89.9f, 97.3f, 106.1f, 115.7f, 127.3f, 139.9f};
inline constexpr float kLowCrossoverHz = 250.0f;

/// Clamps all values into the documented ranges (and replaces NaNs).
ReverbParams sanitize(ReverbParams p);

/// 16-line FDN with Hadamard feedback, input diffusion and per-line damping.
/// Every output channel receives a different, mutually decorrelated mix of the lines.
class FdnReverb {
public:
    static constexpr int kLines = 16;

    void prepare(double sampleRate);
    void reset() noexcept;
    /// Realtime-safe. Size changes are applied without reallocation.
    void setParams(const ReverbParams& p) noexcept;
    const ReverbParams& params() const noexcept { return params_; }

    /// Processes `n` samples of the mono send in `input` and *adds* `gain` times the
    /// reverb to each of the `numOut` channels in `out` (channel pointers may repeat).
    void process(const float* input, float* const* out, int numOut, int n, float gain) noexcept;

private:
    struct Delay {
        std::vector<float> buf;
        int pos = 0;
        int length = 1;
        float cur = -1.0f; // length actually read, glides to `length` (-1: jump there)
        float read() const noexcept { return buf[static_cast<size_t>((pos - length + static_cast<int>(buf.size())) % static_cast<int>(buf.size()))]; }
        /// Reads at a length that glides towards `length` by at most `maxStep` samples per
        /// sample, with interpolation. A size change then bends the pitch briefly instead of
        /// jumping in the buffer, which would click.
        float readGliding(float maxStep, float offset = 0.0f) noexcept {
            const float target = static_cast<float>(length);
            if (cur < 0.0f) cur = target;
            else if (cur != target) cur += std::clamp(target - cur, -maxStep, maxStep);
            const int sz = static_cast<int>(buf.size());
            const float c = std::clamp(cur + offset, 1.0f, static_cast<float>(sz - 2));
            const int i0 = static_cast<int>(c);
            const float f = c - static_cast<float>(i0);
            const float a = buf[static_cast<size_t>((pos - i0 + sz) % sz)];
            const float b = buf[static_cast<size_t>((pos - i0 - 1 + 2 * sz) % sz)];
            return a + (b - a) * f;
        }
        void write(float v) noexcept { buf[static_cast<size_t>(pos)] = v; pos = (pos + 1) % static_cast<int>(buf.size()); }
    };
    struct Allpass {
        std::vector<float> buf;
        int pos = 0;
        float g = 0.6f;
        float process(float x) noexcept;
    };

    double sampleRate_ = 48000.0;
    ReverbParams params_;
    Delay pre_;
    std::array<Allpass, 4> diffusers_;
    std::array<Delay, kLines> lines_;
    /// One allpass inside each feedback loop: every pass through the loop multiplies the
    /// echoes, so the tail gets dense early and stays smooth on quiet, sparse material.
    std::array<Allpass, kLines> lineDiffusers_;
    /// Slow modulation of each line's length (sine oscillators as rotating phasors): the
    /// resonances of the loops wander a little, so the tail does not flutter or beat.
    std::array<float, kLines> modRe_{}, modIm_{}, modCos_{}, modSin_{};
    float modDepth_ = 0.0f; // samples
    std::array<float, kLines> feedback_{};
    std::array<float, kLines> lpState_{};
    std::array<float, kLines> lowState_{};
    std::array<float, kLines> lowFeedback_{};
    float lpCoef_ = 0.0f;
    float lowCoef_ = 0.0f;
    Delay early_;   // pre-delayed dry signal, read at the early reflection taps
    Delay cluster_; // diffused signal, read at the cluster taps
    std::array<int, kEarlyTaps> earlyLen_{};
    std::array<int, kClusterTaps> clusterLen_{};
    std::array<float, kEarlyTaps> earlyCur_{};     // gliding tap positions (see Delay::readGliding)
    std::array<float, kClusterTaps> clusterCur_{};
    std::array<float, kClusterTaps> clusterGainCur_{};
    bool snapTaps_ = true;
    std::array<float, kClusterTaps> clusterGain_{};
    float earlyLevel_ = 0.0f, clusterLevel_ = 0.0f, tailLevel_ = 1.0f;
    bool prepared_ = false;
};

} // namespace rk
