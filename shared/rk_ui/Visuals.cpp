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
    history_.insert(history_.begin(), db);
    if ((int)history_.size() > kHistory) history_.resize((size_t)kHistory);
    repaint();
}

juce::Colour ReverbView::hueAt(float x01) const {
    // A curated spectral ramp: warm lows, clear mids, cool highs (no garish full rainbow).
    static const juce::Colour stops[] = {juce::Colour(0xffff6a3d), juce::Colour(0xffffb547), juce::Colour(0xff9be38a),
                                         juce::Colour(0xff43d1c9), juce::Colour(0xff4aa8ff), juce::Colour(0xff8d7bff),
                                         juce::Colour(0xffc77dff)};
    const float t = juce::jlimit(0.0f, 1.0f, x01) * 6.0f;
    const int i = juce::jmin(5, (int)t);
    return stops[i].interpolatedWith(stops[i + 1], t - (float)i);
}

void ReverbView::paint(juce::Graphics& g) {
    auto card = getLocalBounds().toFloat().reduced(0.5f);
    // Instrument panel: deep background with a hairline frame and a faint top highlight.
    juce::ColourGradient bg(juce::Colour(0xff17171b), 0.0f, card.getY(), juce::Colour(0xff0d0d10), 0.0f, card.getBottom(), false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(card, 10.0f);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawRoundedRectangle(card, 10.0f, 0.8f);

    auto area = card.reduced(12.0f, 10.0f);
    auto caption = area.removeFromTop(16.0f);
    auto axis = area.removeFromBottom(13.0f);
    area.removeFromBottom(3.0f);
    const float depthX = 0.9f, depthY = 2.1f; // each older spectrum moves up and right
    auto plot = area.withTrimmedRight(depthX * kHistory * 0.6f).withTrimmedTop(depthY * kHistory * 0.55f);
    const float x0 = plot.getX(), w = plot.getWidth(), yb = plot.getBottom(), h = plot.getHeight();

    // Grid: decades and octaves, very quiet.
    for (float f : {50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f}) {
        const float x = x0 + freqToX(f) * w;
        const bool decade = f == 100.0f || f == 1000.0f || f == 10000.0f;
        g.setColour(juce::Colours::white.withAlpha(decade ? 0.07f : 0.035f));
        g.drawVerticalLine(juce::roundToInt(x), plot.getY() - depthY * kHistory * 0.55f, yb);
    }
    for (int k = 1; k <= 3; ++k) {
        g.setColour(juce::Colours::white.withAlpha(0.035f));
        g.drawHorizontalLine(juce::roundToInt(yb - h * (float)k / 4.0f), x0, x0 + w);
    }
    g.setColour(juce::Colours::white.withAlpha(0.14f));
    g.drawHorizontalLine(juce::roundToInt(yb), x0, x0 + w);

    juce::ColourGradient ramp;
    ramp.point1 = {x0, 0.0f};
    ramp.point2 = {x0 + w, 0.0f};
    for (int i = 0; i <= 6; ++i) ramp.addColour(i / 6.0, hueAt(i / 6.0f));
    auto rampAlpha = [&ramp](float a) {
        auto r = ramp;
        for (int i = 0; i < r.getNumColours(); ++i) r.setColour(i, r.getColour(i).withAlpha(a));
        return r;
    };

    // Smooth curve through band levels.
    auto spectrumPath = [&](const std::array<float, SpectrumAnalyzer::kBands>& db, float dx, float dy, bool closed) {
        juce::Path path;
        const int n = SpectrumAnalyzer::kBands;
        auto pt = [&](int b) {
            const float lvl = juce::jlimit(0.0f, 1.0f, (db[(size_t)b] + 78.0f) / 78.0f);
            return juce::Point<float>{x0 + (b + 0.5f) / n * w + dx, yb - h * lvl + dy};
        };
        if (closed) path.startNewSubPath(x0 + dx, yb + dy), path.lineTo(pt(0));
        else path.startNewSubPath(pt(0));
        for (int b = 1; b < n; ++b) {
            const auto a = pt(b - 1), c = pt(b);
            path.quadraticTo(a, (a + c) * 0.5f);
        }
        path.lineTo(pt(n - 1));
        if (closed) path.lineTo(x0 + w + dx, yb + dy), path.closeSubPath();
        return path;
    };

    if (hasBands_ && !history_.empty()) {
        // Waterfall: the last second of sound, receding into depth.
        for (int i = (int)history_.size() - 1; i >= 1; --i) {
            const float age = (float)i / (float)kHistory;
            const float dx = depthX * (float)i * 0.6f, dy = -depthY * (float)i * 0.55f;
            // Mask what lies behind this line, then draw it: proper occlusion, like a 3D surface.
            g.setColour(juce::Colour(0xff0f0f12));
            g.fillPath(spectrumPath(history_[(size_t)i], dx, dy, true));
            g.setGradientFill(rampAlpha(0.55f * (1.0f - age) * (1.0f - age) + 0.05f));
            g.strokePath(spectrumPath(history_[(size_t)i], dx, dy, false), juce::PathStrokeType(0.75f));
        }
        // Front: filled with a fading gradient and a crisp edge.
        const auto front = spectrumPath(history_[0], 0.0f, 0.0f, true);
        g.setColour(juce::Colour(0xff0f0f12));
        g.fillPath(front);
        auto fill = rampAlpha(0.30f);
        g.setGradientFill(fill);
        g.fillPath(front);
        juce::ColourGradient fade(juce::Colours::transparentBlack, 0.0f, plot.getY(), juce::Colour(0xff0f0f12).withAlpha(0.85f), 0.0f, yb, false);
        g.setGradientFill(fade);
        g.fillPath(front);
        g.setGradientFill(rampAlpha(1.0f));
        g.strokePath(spectrumPath(history_[0], 0.0f, 0.0f, false), juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Reverb time over frequency: a fine white curve with its own scale on the right.
    const bool dry = wetDb_ <= -79.0f;
    if (!dry) {
        const float maxT = 8.0f;
        auto tY = [&](float t) { return yb - h * juce::jlimit(0.0f, 1.0f, std::log1p(t) / std::log1p(maxT)); };
        juce::Path tail;
        for (int i = 0; i <= 80; ++i) {
            const float x01 = (float)i / 80.0f;
            const float f = kFMin * std::pow(kFMax / kFMin, x01);
            const float t = params_.decaySec / (1.0f + (f / params_.dampingHz) * (f / params_.dampingHz));
            const juce::Point<float> q{x0 + x01 * w, tY(t)};
            if (i == 0) tail.startNewSubPath(q);
            else tail.lineTo(q);
        }
        const float dashes[] = {4.0f, 3.0f};
        juce::Path dashed;
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, tail, dashes, 2);
        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.fillPath(dashed);
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.setFont(uiFont(9.5f));
        for (float t : {0.5f, 1.0f, 2.0f, 4.0f}) {
            const float y = tY(t);
            g.drawText(num(t, t < 1.0f ? 1 : 0) + " s", juce::Rectangle<float>(area.getRight() - 30.0f, y - 6.0f, 30.0f, 12.0f), juce::Justification::centredRight);
        }
    }

    const juce::String dot = juce::String::fromUTF8(" \xc2\xb7 ");
    g.setColour(juce::Colour(0xffebebf5).withAlpha(0.85f));
    g.setFont(uiFont(11.5f, true));
    const juce::String title = dry ? tr("Trocken") : tr("Nachhall") + " " + num(params_.decaySec, 1) + " s";
    g.drawText(title, caption, juce::Justification::centredLeft, true);
    if (!dry) {
        const float tw = juce::GlyphArrangement::getStringWidth(uiFont(11.5f, true), title);
        g.setColour(juce::Colour(0xffebebf5).withAlpha(0.45f));
        g.setFont(uiFont(11.5f));
        g.drawText(dot + tr("Höhen ab") + " " + num(params_.dampingHz / 1000.0, 1) + " kHz", caption.withTrimmedLeft(tw + 2.0f),
                   juce::Justification::centredLeft, true);
    }
    g.setColour(juce::Colour(0xffebebf5).withAlpha(0.4f));
    g.setFont(uiFont(9.5f));
    for (float f : {100.0f, 1000.0f, 10000.0f}) {
        const float x = x0 + freqToX(f) * w;
        g.drawText(f >= 1000.0f ? juce::String((int)(f / 1000.0f)) + " kHz" : juce::String((int)f) + " Hz",
                   juce::Rectangle<float>(x - 30.0f, axis.getY(), 60.0f, axis.getHeight()), juce::Justification::centred);
    }
}

} // namespace rkui
