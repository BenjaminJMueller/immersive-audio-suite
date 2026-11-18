// Raumklang core: small realtime-safe DSP building blocks shared by the engines.
#pragma once

#include "rk/math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace rk {

/// Pink noise (Paul Kellet's refined filter on a xorshift white noise source).
class PinkNoise {
public:
    explicit PinkNoise(uint32_t seed = 0x1234567u) : state_(seed ? seed : 1u) {}
    float next() noexcept {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        const float white = static_cast<float>(static_cast<int32_t>(state_)) * (1.0f / 2147483648.0f);
        b0_ = 0.99886f * b0_ + white * 0.0555179f;
        b1_ = 0.99332f * b1_ + white * 0.0750759f;
        b2_ = 0.96900f * b2_ + white * 0.1538520f;
        b3_ = 0.86650f * b3_ + white * 0.3104856f;
        b4_ = 0.55000f * b4_ + white * 0.5329522f;
        b5_ = -0.7616f * b5_ - white * 0.0168980f;
        const float pink = b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + white * 0.5362f;
        b6_ = white * 0.115926f;
        return pink * 0.11f; // roughly -12 dBFS RMS
    }

private:
    uint32_t state_;
    float b0_ = 0, b1_ = 0, b2_ = 0, b3_ = 0, b4_ = 0, b5_ = 0, b6_ = 0;
};

/// Integer delay. Memory is allocated by prepare() (off the audio thread).
class DelayLine {
public:
    void prepare(int delaySamples) {
        delay_ = std::max(0, delaySamples);
        buf_.assign(static_cast<size_t>(delay_ + 1), 0.0f);
        pos_ = 0;
    }
    int delay() const noexcept { return delay_; }
    void process(float* io, int n) noexcept {
        if (delay_ == 0) return;
        const int size = delay_ + 1;
        for (int i = 0; i < n; ++i) {
            buf_[static_cast<size_t>(pos_)] = io[i];
            pos_ = (pos_ + 1) % size; // oldest sample == written `delay_` samples ago
            io[i] = buf_[static_cast<size_t>(pos_)];
        }
    }

private:
    std::vector<float> buf_;
    int pos_ = 0;
    int delay_ = 0;
};

/// Protective peak limiter: instant attack, smooth release, hard ceiling.
/// It guarantees |output| <= ceiling and turns NaN/Inf into silence.
class PeakLimiter {
public:
    static constexpr float kCeiling = 0.989f; // -0.1 dBFS

    void prepare(double sampleRate) noexcept {
        release_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.08 * sampleRate));
        gain_ = 1.0f;
    }
    void reset() noexcept { gain_ = 1.0f; }
    /// Returns true if the limiter reduced the gain in this block.
    bool process(float* io, int n, bool enabled, float& peakOut) noexcept {
        bool acted = false;
        float peak = 0.0f;
        for (int i = 0; i < n; ++i) {
            float v = sanitizeSample(io[i]);
            if (enabled) {
                const float a = std::fabs(v);
                if (a * gain_ > kCeiling) {
                    gain_ = kCeiling / a;
                    acted = true;
                } else {
                    gain_ += (1.0f - gain_) * release_;
                }
                v = std::clamp(v * gain_, -kCeiling, kCeiling);
            }
            io[i] = v;
            peak = std::max(peak, std::fabs(v));
        }
        peakOut = peak;
        return acted;
    }

private:
    float gain_ = 1.0f;
    float release_ = 0.001f;
};

} // namespace rk
