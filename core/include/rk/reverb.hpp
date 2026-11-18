// Raumklang core: multichannel feedback delay network reverb.
#pragma once

#include <array>
#include <vector>

namespace rk {

struct ReverbParams {
    float decaySec = 1.8f;     // RT60 at low/mid frequencies, 0.2..12 s
    float dampingHz = 6000.0f; // high frequencies above this decay faster, 1k..20k
    float preDelayMs = 15.0f;  // 0..250 ms
    float size = 1.0f;         // scales the delay lengths, 0.3..2
};

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
        float read() const noexcept { return buf[static_cast<size_t>((pos - length + static_cast<int>(buf.size())) % static_cast<int>(buf.size()))]; }
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
    std::array<float, kLines> feedback_{};
    std::array<float, kLines> lpState_{};
    float lpCoef_ = 0.0f;
    bool prepared_ = false;
};

} // namespace rk
