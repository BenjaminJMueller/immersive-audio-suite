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

} // namespace rkui
