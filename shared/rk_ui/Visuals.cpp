#include "Visuals.h"
#include <array>

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

int drawSoundParticles(ParticleBatch& g, juce::Point<float> from, juce::Point<float> to, juce::Colour colour,
                       float energy, uint32_t seed, double timeSec, float widthPx, float sizeScale) {
    energy = juce::jlimit(0.0f, 1.0f, energy);
    const auto d = to - from;
    const float len = d.getDistanceFromOrigin();
    if (energy < 0.004f || len < 4.0f) return 0;
    // Density per 100 px of path, so long and short paths look alike.
    const int count = juce::jlimit(0, 360, juce::roundToInt(std::sqrt(energy) * 240.0f * std::max(0.7f, len / 100.0f)));
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
        // Every particle glows; the bigger ones carry a wider halo.
        g.add(pt, size, size * (3.0f + 4.0f * w * w), (w > 0.6f ? core : colour), colour, alpha);
    }
    return count;
}

int drawSoundCloud(ParticleBatch& g, juce::Point<float> centre, juce::Colour colour, float energy, float innerPx,
                   float radiusPx, uint32_t seed, double timeSec, float sizeScale) {
    energy = juce::jlimit(0.0f, 1.0f, energy);
    if (energy < 0.004f) return 0;
    const int count = juce::jlimit(0, 360, juce::roundToInt(std::sqrt(energy) * radiusPx * 8.0f));
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
        // Every particle glows; the bigger ones carry a wider halo.
        g.add(pt, size, size * (3.0f + 4.0f * v * v), (v > 0.6f ? core : colour), colour, alpha);
    }
    return count;
}

void ParticleBatch::add(juce::Point<float> pt, float size, float glowRadius, juce::Colour core, juce::Colour glow, float alpha) {
    if (alpha <= 0.003f) return;
    items_.push_back({pt, size, glowRadius, core, glow, alpha});
}

void ParticleBatch::draw(juce::Graphics& g, juce::Rectangle<int> area, float glowAmount) {
    if (items_.empty()) return;
    // Glow: every particle as a soft blob on a layer at a third of the resolution, then
    // stretched over the view. The bilinear scaling turns the blobs into a smooth glow,
    // and overlapping particles add up to a brighter haze where the energy is dense.
    constexpr float kDown = 3.0f;
    const int w = juce::jmax(1, (int)std::ceil(area.getWidth() / kDown)), h = juce::jmax(1, (int)std::ceil(area.getHeight() / kDown));
    if (glow_.getWidth() != w || glow_.getHeight() != h) glow_ = juce::Image(juce::Image::ARGB, w, h, true);
    accum_.assign((size_t)w * (size_t)h * 3, 0.0f);
    // Gaussian falloff from a table: d2 * inv stays below 1 / 0.35 because of the d2 < r*r test.
    static constexpr int kLut = 256;
    static constexpr float kLutScale = (float)(kLut - 1) * 0.35f;
    static const auto kGauss = [] {
        std::array<float, kLut> t {};
        for (int i = 0; i < kLut; ++i) t[(size_t)i] = std::exp(-(float)i / kLutScale);
        return t;
    }();
    // Additive splats: overlapping particles really add up to a brighter glow.
    int bx0 = w, by0 = h, bx1 = -1, by1 = -1; // touched rows, the others are simply cleared
    // Touched tiles, so only those get scaled up (the expensive part of the glow).
    constexpr int kTile = 12;
    const int tw = (w + kTile - 1) / kTile, th = (h + kTile - 1) / kTile;
    std::vector<char> tiles((size_t)tw * (size_t)th, 0);
    for (const auto& p : items_) {
        const float x = (p.pt.x - (float)area.getX()) / kDown, y = (p.pt.y - (float)area.getY()) / kDown;
        const float r = juce::jlimit(0.8f, 8.0f, p.glowRadius / kDown);
        const float inv = 1.0f / (r * r * 0.35f);
        const float a = p.alpha * 0.7f * glowAmount;
        const float cr = p.glow.getFloatRed() * a, cg = p.glow.getFloatGreen() * a, cb = p.glow.getFloatBlue() * a;
        const int x0 = juce::jmax(0, (int)(x - r)), x1 = juce::jmin(w - 1, (int)(x + r) + 1);
        const int y0 = juce::jmax(0, (int)(y - r)), y1 = juce::jmin(h - 1, (int)(y + r) + 1);
        if (x0 > x1 || y0 > y1) continue;
        bx0 = juce::jmin(bx0, x0); by0 = juce::jmin(by0, y0); bx1 = juce::jmax(bx1, x1); by1 = juce::jmax(by1, y1);
        for (int ty = y0 / kTile; ty <= y1 / kTile; ++ty)
            for (int tx = x0 / kTile; tx <= x1 / kTile; ++tx) tiles[(size_t)(ty * tw + tx)] = 1;
        for (int yy = y0; yy <= y1; ++yy) {
            const float dy = (float)yy + 0.5f - y;
            float* row = &accum_[((size_t)yy * (size_t)w + (size_t)x0) * 3];
            for (int xx = x0; xx <= x1; ++xx, row += 3) {
                const float dx = (float)xx + 0.5f - x;
                const float d2 = dx * dx + dy * dy;
                if (d2 >= r * r) continue;
                const float k = kGauss[(int)(d2 * inv * kLutScale)];
                row[0] += cr * k;
                row[1] += cg * k;
                row[2] += cb * k;
            }
        }
    }
    {
        juce::Image::BitmapData bd(glow_, juce::Image::BitmapData::writeOnly);
        for (int yy = 0; yy < h; ++yy) {
            const float* row = &accum_[(size_t)yy * (size_t)w * 3];
            if (yy < by0 || yy > by1) { // untouched rows stay transparent
                for (int xx = 0; xx < w; ++xx) ((juce::PixelARGB*)bd.getPixelPointer(xx, yy))->setARGB(0, 0, 0, 0);
                continue;
            }
            for (int xx = 0; xx < w; ++xx, row += 3) {
                // Soft knee so dense spots bloom instead of clipping hard.
                const float r = row[0] / (1.0f + row[0]), gg = row[1] / (1.0f + row[1]), b = row[2] / (1.0f + row[2]);
                const float al = std::max(r, std::max(gg, b));
                auto* px = (juce::PixelARGB*)bd.getPixelPointer(xx, yy);
                px->setARGB((juce::uint8)(al * 255.0f), (juce::uint8)(r * 255.0f), (juce::uint8)(gg * 255.0f), (juce::uint8)(b * 255.0f));
            }
        }
    }
    {
        juce::Graphics::ScopedSaveState st(g);
        g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
        g.setOpacity(1.0f);
        juce::RectangleList<int> clip;
        const int ts = (int)((float)kTile * kDown);
        for (int ty = 0; ty < th; ++ty)
            for (int tx = 0; tx < tw; ++tx)
                if (tiles[(size_t)(ty * tw + tx)]) clip.addWithoutMerging({ area.getX() + tx * ts, area.getY() + ty * ts, ts, ts });
        clip.consolidate();
        if (!clip.isEmpty() && g.reduceClipRegion(clip))
            g.drawImage(glow_, area.toFloat(), juce::RectanglePlacement::stretchToFit);
    }
    // Crisp cores on top. Tiny dots as rectangles: they look the same and cost far less.
    for (const auto& p : items_) {
        g.setColour(p.core.withAlpha(p.alpha));
        if (p.size < 2.4f) g.fillRect(p.pt.x - p.size * 0.5f, p.pt.y - p.size * 0.5f, p.size, p.size);
        else g.fillEllipse(p.pt.x - p.size * 0.5f, p.pt.y - p.size * 0.5f, p.size, p.size);
    }
    items_.clear();
}

} // namespace rkui
