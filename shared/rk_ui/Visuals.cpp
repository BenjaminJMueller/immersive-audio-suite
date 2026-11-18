#include "Visuals.h"

#include "Strings.h"

#include <cmath>
#include <complex>

namespace rkui {
namespace {

constexpr float kFMin = 40.0f, kFMax = 18000.0f;

float freqToX(float f) { return std::log(f / kFMin) / std::log(kFMax / kFMin); }

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

ReverbView::ReverbView() { setOpaque(false); }

void ReverbView::setParams(const rk::ReverbParams& p, float wetDb) {
    params_ = p;
    wetDb_ = wetDb;
    repaint();
}

void ReverbView::setBands(const std::array<float, SpectrumAnalyzer::kBands>& db) {
    bands_ = db;
    hasBands_ = true;
    repaint();
}

juce::Colour ReverbView::hueAt(float x01) const {
    // Red for the lows through yellow and green to blue and violet for the highs.
    return juce::Colour::fromHSV(juce::jlimit(0.0f, 0.78f, x01 * 0.78f), 0.78f, 1.0f, 1.0f);
}

void ReverbView::paint(juce::Graphics& g) {
    const Palette& p = palette();
    auto card = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(juce::Colour(0xff111114));
    g.fillRoundedRectangle(card, 10.0f);
    g.setColour(p.line);
    g.drawRoundedRectangle(card, 10.0f, 0.75f);
    auto area = card.reduced(12.0f, 10.0f);
    auto caption = area.removeFromTop(16.0f);
    auto axis = area.removeFromBottom(14.0f);
    const float x0 = area.getX(), w = area.getWidth(), yb = area.getBottom(), h = area.getHeight();
    g.saveState();
    g.reduceClipRegion(area.toNearestInt());

    juce::ColourGradient rainbow;
    rainbow.point1 = {x0, 0.0f};
    rainbow.point2 = {x0 + w, 0.0f};
    for (int i = 0; i <= 8; ++i) rainbow.addColour(i / 8.0, hueAt(i / 8.0f));

    // Reverb tail: decay time over frequency (high frequencies die away above the damping).
    const bool dry = wetDb_ <= -79.0f;
    const float maxT = 8.0f;
    auto tY = [&](float t) { return yb - h * juce::jlimit(0.0f, 1.0f, std::log1p(t) / std::log1p(maxT)); };
    juce::Path tail;
    tail.startNewSubPath(x0, yb);
    for (int i = 0; i <= 64; ++i) {
        const float x01 = (float)i / 64.0f;
        const float f = kFMin * std::pow(kFMax / kFMin, x01);
        const float t = params_.decaySec / (1.0f + (f / params_.dampingHz) * (f / params_.dampingHz));
        tail.lineTo(x0 + x01 * w, tY(t));
    }
    tail.lineTo(x0 + w, yb);
    tail.closeSubPath();
    if (!dry) {
        const float strength = juce::jlimit(0.2f, 1.0f, (wetDb_ + 40.0f) / 40.0f);
        auto fill = rainbow;
        for (int i = 0; i < fill.getNumColours(); ++i) fill.setColour(i, fill.getColour(i).withAlpha(0.22f * strength));
        g.setGradientFill(fill);
        g.fillPath(tail);
        auto edge = rainbow;
        for (int i = 0; i < edge.getNumColours(); ++i) edge.setColour(i, edge.getColour(i).withAlpha(0.35f * strength));
        g.setGradientFill(edge);
        g.strokePath(tail, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved));
        for (int i = 0; i < edge.getNumColours(); ++i) edge.setColour(i, rainbow.getColour(i).withAlpha(0.95f));
        g.setGradientFill(edge);
        g.strokePath(tail, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved));
    }

    // Live spectrum: glowing bars.
    if (hasBands_) {
        const float bw = w / (float)SpectrumAnalyzer::kBands;
        for (int b = 0; b < SpectrumAnalyzer::kBands; ++b) {
            const float lvl = juce::jlimit(0.0f, 1.0f, (bands_[(size_t)b] + 78.0f) / 78.0f);
            if (lvl <= 0.01f) continue;
            const float x = x0 + b * bw;
            const float top = yb - h * lvl;
            const juce::Colour c = hueAt((b + 0.5f) / (float)SpectrumAnalyzer::kBands);
            juce::ColourGradient bar(c.withAlpha(0.9f), x, top, c.withAlpha(0.15f), x, yb, false);
            g.setGradientFill(bar);
            g.fillRoundedRectangle(x + 1.0f, top, std::max(1.0f, bw - 2.0f), yb - top, 1.5f);
            g.setColour(c.withAlpha(0.25f));
            g.fillRoundedRectangle(x - 1.0f, top - 3.0f, bw + 2.0f, 6.0f, 3.0f);
        }
    }
    g.restoreState();

    g.setColour(juce::Colour(0xffebebf5).withAlpha(0.75f));
    g.setFont(uiFont(11.5f, true));
    const juce::String dot = juce::String::fromUTF8(" \xc2\xb7 ");
    g.drawText(dry ? tr("Trocken") : tr("Nachhall") + " " + num(params_.decaySec, 1) + " s" + dot + tr("Höhen ab") + " " +
                                         num(params_.dampingHz / 1000.0, 1) + " kHz",
               caption, juce::Justification::centredLeft, true);
    g.setColour(juce::Colour(0xffebebf5).withAlpha(0.45f));
    g.setFont(uiFont(10.5f));
    for (float f : {100.0f, 1000.0f, 10000.0f}) {
        const float x = x0 + freqToX(f) * w;
        g.drawText(f >= 1000.0f ? juce::String((int)(f / 1000.0f)) + " kHz" : juce::String((int)f) + " Hz",
                   juce::Rectangle<float>(x - 30.0f, axis.getY(), 60.0f, axis.getHeight()), juce::Justification::centred);
    }
}

} // namespace rkui
