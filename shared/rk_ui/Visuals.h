// Raumklang UI: a live spectrum of what the speakers play (shown inside the reverb curve).
#pragma once

#include "Scene.h"
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

/// Sound particles between a source and a speaker: many small glowing dots whose density
/// shows how much of the source this speaker plays. Each dot lives for a fraction of a second,
/// fades in and out and is born again somewhere else, so the cloud never forms a fixed
/// pattern. How many are alive at once follows the level directly.
/// `energy` 0..1 (gain squared times level), `seed` separates the paths,
/// `widthPx` widens the cloud (source width). Returns the number of dots drawn.
int drawSoundParticles(ParticleBatch& g, juce::Point<float> from, juce::Point<float> to, juce::Colour colour,
                       float energy, uint32_t seed, double timeSec, float widthPx = 0.0f, float sizeScale = 1.0f);

/// A cloud of the same particles around a point (a sounding source or a speaker that plays
/// it), starting just outside `innerPx` and thinning out over `radiusPx`.
int drawSoundCloud(ParticleBatch& g, juce::Point<float> centre, juce::Colour colour, float energy, float innerPx,
                   float radiusPx, uint32_t seed, double timeSec, float sizeScale = 1.0f);

/// The reverb share of a source: particles drifting outwards from `from` on bent paths
/// towards the speakers in `to`, as a haze rather than straight rays.
int drawReverbFlow(ParticleBatch& g, juce::Point<float> from, const std::vector<juce::Point<float>>& to, juce::Colour colour,
                   float energy, uint32_t seed, double timeSec, float sizeScale = 1.0f);

/// How strongly a sounding source flows into the reverb (0..1): its level and reverb send.
/// Zero when the send is at its minimum or the source is silent.
float reverbFlowEnergy(const SourceInfo& source, float level, float reverbWetDb);

/// Whether a view's window can be seen at all. The app installs a check that knows about
/// macOS Spaces and covered windows; without one, every shown view counts as visible.
/// Animated views skip their redraws while this is false.
void setOnScreenCheck(bool (*check)(const juce::Component&));
bool isOnScreen(const juce::Component& c);

} // namespace rkui
