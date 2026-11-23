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

/// Peak limiter: instant attack, smooth release, hard ceiling.
/// It guarantees |output| <= ceiling and turns NaN/Inf into silence.
/// Compressor for all outputs together ("3D compressor"): one detector over every channel
/// (the loudest sample of all channels), one gain for all. The spatial image stays where it
/// is because every speaker is turned down by the same amount. Feed-forward, peak detection
/// in the log domain with soft knee, separate attack and release.
class LinkedCompressor {
public:
    struct Params {
        bool enabled = false;
        float thresholdDb = -18.0f; // -60..0 dBFS
        float ratio = 3.0f;         // 1..20
        float attackMs = 10.0f;     // 0.1..200
        float releaseMs = 150.0f;   // 5..2000
        float kneeDb = 6.0f;        // 0..24
        float makeupDb = 0.0f;      // 0..24
    };

    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        grDb_ = 0.0f;
        makeup_ = 1.0f;
        attackMs_ = releaseMs_ = -1.0f;
    }
    void reset() noexcept { grDb_ = 0.0f; }

    /// Gain reduction computed by the static curve for an input level in dB (>= 0).
    static float curveReductionDb(float levelDb, float thresholdDb, float ratio, float kneeDb) noexcept {
        const float over = levelDb - thresholdDb;
        const float slope = 1.0f - 1.0f / std::max(1.0f, ratio);
        if (kneeDb > 0.0f && 2.0f * std::fabs(over) <= kneeDb) {
            const float x = over + 0.5f * kneeDb;
            return slope * x * x / (2.0f * kneeDb);
        }
        return over > 0.0f ? slope * over : 0.0f;
    }

    /// Processes `numCh` channels in place with one common gain. Returns the largest gain
    /// reduction of the block in dB (>= 0). Disabling releases smoothly instead of jumping.
    float process(float* const* ch, int numCh, int n, const Params& p) noexcept {
        if (numCh <= 0 || n <= 0) return 0.0f;
        const float thr = std::clamp(std::isfinite(p.thresholdDb) ? p.thresholdDb : -18.0f, -60.0f, 0.0f);
        const float ratio = std::clamp(std::isfinite(p.ratio) ? p.ratio : 3.0f, 1.0f, 20.0f);
        const float knee = std::clamp(std::isfinite(p.kneeDb) ? p.kneeDb : 6.0f, 0.0f, 24.0f);
        setTimes(p.attackMs, p.releaseMs);
        const float makeupTarget =
            p.enabled ? std::pow(10.0f, std::clamp(std::isfinite(p.makeupDb) ? p.makeupDb : 0.0f, 0.0f, 24.0f) / 20.0f) : 1.0f;
        if (!p.enabled && grDb_ < 1.0e-4f && std::fabs(makeup_ - 1.0f) < 1.0e-6f) return 0.0f;
        const float mStep = (makeupTarget - makeup_) / static_cast<float>(n);
        float most = 0.0f;
        for (int i = 0; i < n; ++i) {
            float peak = 0.0f;
            for (int c = 0; c < numCh; ++c)
                if (ch[c] != nullptr) peak = std::max(peak, std::fabs(ch[c][i]));
            float target = 0.0f;
            if (p.enabled && peak > 1.0e-6f) target = curveReductionDb(20.0f * std::log10(peak), thr, ratio, knee);
            grDb_ += (target - grDb_) * (target > grDb_ ? attack_ : release_);
            if (!std::isfinite(grDb_)) grDb_ = 0.0f;
            most = std::max(most, grDb_);
            makeup_ += mStep;
            const float g = makeup_ * std::pow(10.0f, -grDb_ / 20.0f);
            for (int c = 0; c < numCh; ++c)
                if (ch[c] != nullptr) ch[c][i] *= g;
        }
        makeup_ = makeupTarget;
        return most;
    }

private:
    void setTimes(float attackMs, float releaseMs) noexcept {
        attackMs = std::clamp(std::isfinite(attackMs) ? attackMs : 10.0f, 0.1f, 200.0f);
        releaseMs = std::clamp(std::isfinite(releaseMs) ? releaseMs : 150.0f, 5.0f, 2000.0f);
        if (std::fabs(attackMs - attackMs_) > 1.0e-6f) {
            attackMs_ = attackMs;
            attack_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.001 * attackMs * sampleRate_));
        }
        if (std::fabs(releaseMs - releaseMs_) > 1.0e-6f) {
            releaseMs_ = releaseMs;
            release_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.001 * releaseMs * sampleRate_));
        }
    }
    double sampleRate_ = 48000.0;
    float grDb_ = 0.0f, makeup_ = 1.0f;
    float attack_ = 0.01f, release_ = 0.001f, attackMs_ = -1.0f, releaseMs_ = -1.0f;
};

class PeakLimiter {
public:
    static constexpr float kCeiling = 0.989f; // -0.1 dBFS, the default

    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        releaseMs_ = -1.0f;
        setRelease(80.0f);
        gain_ = 1.0f;
    }
    void reset() noexcept { gain_ = 1.0f; }
    /// Ceiling in dBFS (-20..0). Cheap, may be called every block.
    void setCeilingDb(float db) noexcept {
        const float d = std::clamp(std::isfinite(db) ? db : -0.1f, -20.0f, 0.0f);
        ceiling_ = std::min(kCeiling, std::pow(10.0f, d / 20.0f));
    }
    /// Release time constant in ms (1..2000). Cheap when unchanged.
    void setRelease(float ms) noexcept {
        ms = std::clamp(std::isfinite(ms) ? ms : 80.0f, 1.0f, 2000.0f);
        if (ms == releaseMs_) return;
        releaseMs_ = ms;
        release_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.001 * ms * sampleRate_));
    }
    /// Smallest gain applied in the last block (1 = no gain reduction).
    float minGain() const noexcept { return minGain_; }
    /// Returns true if the limiter reduced the gain in this block.
    bool process(float* io, int n, bool enabled, float& peakOut) noexcept {
        bool acted = false;
        float peak = 0.0f;
        float lowest = 1.0f;
        const float ceiling = ceiling_;
        for (int i = 0; i < n; ++i) {
            float v = sanitizeSample(io[i]);
            if (enabled) {
                const float a = std::fabs(v);
                if (a * gain_ > ceiling) {
                    gain_ = ceiling / a;
                    acted = true;
                } else {
                    gain_ += (1.0f - gain_) * release_;
                }
                lowest = std::min(lowest, gain_);
                v = std::clamp(v * gain_, -ceiling, ceiling);
            }
            io[i] = v;
            peak = std::max(peak, std::fabs(v));
        }
        if (!enabled) gain_ = 1.0f;
        minGain_ = lowest;
        peakOut = peak;
        return acted;
    }

private:
    double sampleRate_ = 48000.0;
    float gain_ = 1.0f;
    float release_ = 0.001f;
    float releaseMs_ = -1.0f;
    float ceiling_ = kCeiling;
    float minGain_ = 1.0f;
};

} // namespace rk
