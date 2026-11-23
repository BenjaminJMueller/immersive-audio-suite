// Raumklang UI: a live spectrum of what the speakers play (shown inside the reverb curve).
#pragma once

#include "Theme.h"

#include <rk/reverb.hpp>

#include <array>
#include <vector>

namespace rkui {

/// Turns blocks of samples into smoothed band levels (1/6 octave, 40 Hz .. 18 kHz).
/// Message thread only. Uses its own small FFT so the plugins need no extra modules.
class SpectrumAnalyzer {
public:
    static constexpr int kSize = 4096;
    static constexpr int kBands = 52;
    /// `samples` are the newest kSize samples (mono).
    void process(const float* samples, double sampleRate);
    /// Level per band in dB, about -90 .. 0, with a slow fall like an analogue meter.
    const std::array<float, kBands>& bandsDb() const noexcept { return bands_; }
    static float bandFrequency(int band) noexcept;

private:
    std::array<float, kBands> bands_{};
    bool init_ = false;
};

} // namespace rkui
