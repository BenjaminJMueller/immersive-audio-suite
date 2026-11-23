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

/// Collects sound particles while a view paints, then draws them in one go: a soft glow
/// layer (rendered small and stretched, so it stays cheap) and crisp cores on top.
class ParticleBatch {
public:
    void add(juce::Point<float> pt, float size, float glowRadius, juce::Colour core, juce::Colour glow, float alpha);
    bool empty() const noexcept { return items_.empty(); }
    int size() const noexcept { return (int)items_.size(); }
    /// Draws and clears. `area` is the view's bounds in the graphics context.
    void draw(juce::Graphics& g, juce::Rectangle<int> area, float glowAmount = 1.0f);

private:
    struct Item { juce::Point<float> pt; float size, glowRadius; juce::Colour core, glow; float alpha; };
    std::vector<Item> items_;
    std::vector<float> accum_; // premultiplied RGB of the glow layer
    juce::Image glow_;
};

/// Sound particles ("fireflies") between a source and a speaker, like the energy display in
/// Spat: many small glowing dots whose density shows how much of the source this speaker
/// plays. They do not travel; they sit scattered along the path and twinkle softly.
/// `energy` 0..1 (gain squared times level), `seed` keeps each path's pattern stable,
/// `widthPx` widens the cloud (source width). Returns the number of dots drawn.
int drawSoundParticles(ParticleBatch& g, juce::Point<float> from, juce::Point<float> to, juce::Colour colour,
                       float energy, uint32_t seed, double timeSec, float widthPx = 0.0f, float sizeScale = 1.0f);

/// A cloud of the same particles around a point (a sounding source or a speaker that plays
/// it), starting just outside `innerPx` and thinning out over `radiusPx`.
int drawSoundCloud(ParticleBatch& g, juce::Point<float> centre, juce::Colour colour, float energy, float innerPx,
                   float radiusPx, uint32_t seed, double timeSec, float sizeScale = 1.0f);

} // namespace rkui
