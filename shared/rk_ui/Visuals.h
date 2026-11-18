// Raumklang UI: sound visualisation. A live spectrum of what the speakers play as a
// waterfall receding into depth, and the reverb time over frequency as a fine curve.
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

class ReverbView : public juce::Component, public juce::SettableTooltipClient {
public:
    ReverbView();
    void setParams(const rk::ReverbParams& p, float wetDb);
    void setBands(const std::array<float, SpectrumAnalyzer::kBands>& db);
    void paint(juce::Graphics& g) override;

private:
    juce::Colour hueAt(float x01) const;
    rk::ReverbParams params_;
    float wetDb_ = -6.0f;
    std::array<float, SpectrumAnalyzer::kBands> bands_{};
    bool hasBands_ = false;
    static constexpr int kHistory = 28; // waterfall depth
    std::vector<std::array<float, SpectrumAnalyzer::kBands>> history_; // newest first
};

} // namespace rkui
