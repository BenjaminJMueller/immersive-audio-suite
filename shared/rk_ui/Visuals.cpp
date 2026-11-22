#include "Visuals.h"

#include "Strings.h"

#include <cmath>
#include <complex>

namespace rkui {
namespace {

constexpr float kFMin = 40.0f, kFMax = 18000.0f;


void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * juce::MathConstants<float>::pi / (float)len;
        const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

} // namespace

float SpectrumAnalyzer::bandFrequency(int band) noexcept {
    return kFMin * std::pow(kFMax / kFMin, (band + 0.5f) / (float)kBands);
}

void SpectrumAnalyzer::process(const float* samples, double sampleRate) {
    if (!init_) {
        bands_.fill(-90.0f);
        init_ = true;
    }
    std::vector<std::complex<float>> a(kSize);
    for (int i = 0; i < kSize; ++i) {
        const float w = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * (float)i / (float)(kSize - 1));
        const float v = samples[i];
        a[(size_t)i] = {std::isfinite(v) ? v * w : 0.0f, 0.0f};
    }
    fft(a);
    const float binHz = (float)sampleRate / (float)kSize;
    for (int b = 0; b < kBands; ++b) {
        const float lo = kFMin * std::pow(kFMax / kFMin, (float)b / (float)kBands);
        const float hi = kFMin * std::pow(kFMax / kFMin, (float)(b + 1) / (float)kBands);
        int i0 = juce::jlimit(1, kSize / 2 - 1, (int)std::floor(lo / binHz));
        int i1 = juce::jlimit(i0, kSize / 2 - 1, (int)std::ceil(hi / binHz));
        float e = 0.0f;
        for (int i = i0; i <= i1; ++i) e = std::max(e, std::norm(a[(size_t)i]));
        // Hann window and FFT scaling: a full-scale sine reads about 0 dB.
        const float db = 10.0f * std::log10(e * 16.0f / ((float)kSize * (float)kSize) + 1e-12f);
        const float target = juce::jlimit(-90.0f, 6.0f, db);
        bands_[(size_t)b] = target > bands_[(size_t)b] ? target : std::max(target, bands_[(size_t)b] - 1.6f);
    }
}

int drawSoundParticles(juce::Graphics& g, juce::Point<float> from, juce::Point<float> to, juce::Colour colour,
                       float energy, uint32_t seed, double timeSec, float widthPx, float sizeScale) {
    energy = juce::jlimit(0.0f, 1.0f, energy);
    const auto d = to - from;
    const float len = d.getDistanceFromOrigin();
    if (energy < 0.004f || len < 4.0f) return 0;
    // Density per 100 px of path, so long and short paths look alike.
    const int count = juce::jlimit(0, 160, juce::roundToInt(std::sqrt(energy) * 110.0f * std::max(0.7f, len / 100.0f)));
    const juce::Point<float> dir = d / len, nrm{-dir.y, dir.x};
    uint32_t r = seed * 2654435761u + 0x9e3779b9u;
    auto rnd = [&r] {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        return (float)(r & 0xffffff) / 16777216.0f;
    };
    const juce::Colour core = colour.interpolatedWith(juce::Colours::white, 0.7f);
    for (int k = 0; k < count; ++k) {
        // Along the path: a little denser near the source, sparse at the very ends.
        const float u = rnd(), v = rnd(), w = rnd(), ph = rnd() * juce::MathConstants<float>::twoPi;
        const float t = 0.06f + 0.9f * std::pow(u, 1.15f);
        // Across: a soft bell, widest in the middle of the path.
        const float gauss = (v + w + rnd() - 1.5f) * 1.6f;
        const float spread = (3.0f + 0.10f * len * t * (1.0f - t) + widthPx * (0.3f + t)) * gauss;
        // A slight shimmer in place instead of travelling.
        const float wob = 1.2f * (float)std::sin(timeSec * (0.6 + 0.9 * u) + ph);
        const auto pt = from + dir * (t * len + wob) + nrm * (spread + 0.6f * (float)std::cos(timeSec * 0.8 + ph));
        const float tw = 0.55f + 0.45f * (float)std::sin(timeSec * (1.3 + 2.2 * v) + ph * 3.0f);
        const float alpha = juce::jlimit(0.0f, 1.0f, (0.45f + 0.55f * std::sqrt(energy)) * tw * (1.0f - 0.3f * t));
        const float size = sizeScale * (1.1f + 1.6f * w * w);
        if (w > 0.82f) { // a few bigger ones with a halo
            g.setColour(colour.withAlpha(alpha * 0.12f));
            g.fillEllipse(pt.x - size * 1.6f, pt.y - size * 1.6f, size * 3.2f, size * 3.2f);
        }
        g.setColour((w > 0.6f ? core : colour).withAlpha(alpha));
        // Tiny dots as rectangles: they look the same and cost a fraction of an ellipse.
        if (size < 1.9f) g.fillRect(pt.x - size * 0.5f, pt.y - size * 0.5f, size, size);
        else g.fillEllipse(pt.x - size * 0.5f, pt.y - size * 0.5f, size, size);
    }
    return count;
}

int drawSoundCloud(juce::Graphics& g, juce::Point<float> centre, juce::Colour colour, float energy, float innerPx,
                   float radiusPx, uint32_t seed, double timeSec, float sizeScale) {
    energy = juce::jlimit(0.0f, 1.0f, energy);
    if (energy < 0.004f) return 0;
    const int count = juce::jlimit(0, 180, juce::roundToInt(std::sqrt(energy) * radiusPx * 4.0f));
    uint32_t r = seed * 2246822519u + 0x85ebca6bu;
    auto rnd = [&r] {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        return (float)(r & 0xffffff) / 16777216.0f;
    };
    const juce::Colour core = colour.interpolatedWith(juce::Colours::white, 0.7f);
    for (int k = 0; k < count; ++k) {
        const float a = rnd() * juce::MathConstants<float>::twoPi, u = rnd(), v = rnd(), ph = rnd() * juce::MathConstants<float>::twoPi;
        const float dist = innerPx + radiusPx * u * u + 1.5f * (float)std::sin(timeSec * (0.5 + v) + ph);
        const auto pt = centre + juce::Point<float>(std::cos(a + 0.05f * (float)std::sin(timeSec * 0.4 + ph)), std::sin(a)) * dist;
        const float tw = 0.55f + 0.45f * (float)std::sin(timeSec * (1.1 + 2.0 * v) + ph * 3.0f);
        const float alpha = juce::jlimit(0.0f, 1.0f, (0.45f + 0.55f * std::sqrt(energy)) * tw * (1.0f - 0.55f * u));
        const float size = sizeScale * (1.1f + 1.5f * v * v);
        if (v > 0.85f) {
            g.setColour(colour.withAlpha(alpha * 0.12f));
            g.fillEllipse(pt.x - size * 1.6f, pt.y - size * 1.6f, size * 3.2f, size * 3.2f);
        }
        g.setColour((v > 0.6f ? core : colour).withAlpha(alpha));
        // Tiny dots as rectangles: they look the same and cost a fraction of an ellipse.
        if (size < 1.9f) g.fillRect(pt.x - size * 0.5f, pt.y - size * 0.5f, size, size);
        else g.fillEllipse(pt.x - size * 0.5f, pt.y - size * 0.5f, size, size);
    }
    return count;
}

} // namespace rkui
