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

namespace {

// Stateless hash: the same inputs always give the same number, so particles need no storage.
inline uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) noexcept {
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u) * 0x85ebca6bu ^ (c + 0x165667b1u) * 0xc2b2ae35u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}

// Random numbers for one particle in one life: a new position every time it is born again.
struct Rng {
    uint32_t r;
    float next() noexcept {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        return (float)(r & 0xffffff) / 16777216.0f;
    }
};

// One particle slot: it lives for a short time, fades in and out, and is then born
// somewhere else. `life` is 0..1 within the current life, `fade` its brightness.
struct Life {
    Rng rng;
    float life, fade, size;
};

inline bool lifeOf(uint32_t seed, int k, double timeSec, Life& out) noexcept {
    Rng slot{hash3(seed, (uint32_t)k, 0x51ed27u) | 1u};
    const float period = 0.35f + 0.75f * slot.next();          // seconds per life
    const float phase = slot.next();
    const double g = timeSec / period + phase;
    const double gen = std::floor(g);
    out.life = (float)(g - gen);
    out.fade = std::sin(out.life * juce::MathConstants<float>::pi); // soft in and out
    out.size = slot.next();
    out.rng = Rng{hash3(seed, (uint32_t)k, (uint32_t)(int64_t)gen) | 1u};
    return out.fade > 0.02f;
}

// How many particles a given energy shows; the last one fades in so the count changes smoothly.
inline float slotsFor(float energy, float perUnit) noexcept { return std::sqrt(energy) * perUnit; }

inline void emit(ParticleBatch& g, juce::Point<float> pt, juce::Colour colour, float alpha, float sizeT, float sizeScale) {
    const float size = sizeScale * (0.9f + 1.3f * sizeT * sizeT);
    // A faint, tinted core inside a wide halo: soft dots rather than hard points.
    g.add(pt, size, size * (4.5f + 4.0f * sizeT), colour.interpolatedWith(juce::Colours::white, 0.25f + 0.25f * sizeT), colour, alpha);
}

} // namespace

int drawSoundParticles(ParticleBatch& g, juce::Point<float> from, juce::Point<float> to, juce::Colour colour,
                       float energy, uint32_t seed, double timeSec, float widthPx, float sizeScale) {
    energy = juce::jlimit(0.0f, 1.0f, energy);
    const auto d = to - from;
    const float len = d.getDistanceFromOrigin();
    if (energy < 0.004f || len < 4.0f) return 0;
    // Density per 100 px of path, so long and short paths look alike. The count follows the
    // level directly: louder means more particles at once, silence means none.
    const float slots = std::min(360.0f, slotsFor(energy, 240.0f * std::max(0.7f, len / 100.0f)));
    const juce::Point<float> dir = d / len, nrm{-dir.y, dir.x};
    const float bright = 0.35f + 0.65f * std::sqrt(energy);
    int n = 0;
    for (int k = 0; k < (int)std::ceil(slots); ++k) {
        Life L;
        if (!lifeOf(seed, k, timeSec, L)) continue;
        const float edge = juce::jlimit(0.0f, 1.0f, slots - (float)k);
        const float u = L.rng.next(), v = L.rng.next(), w = L.rng.next(), z = L.rng.next();
        const float t = 0.06f + 0.9f * std::pow(u, 1.15f);
        const float gauss = (v + w + z - 1.5f) * 1.6f;
        const float spread = (3.0f + 0.10f * len * t * (1.0f - t) + widthPx * (0.3f + t)) * gauss;
        // A small random drift during its short life, no travelling along the path.
        const float ang = L.rng.next() * juce::MathConstants<float>::twoPi;
        const juce::Point<float> drift(std::cos(ang), std::sin(ang));
        const auto pt = from + dir * (t * len) + nrm * spread + drift * (3.0f * L.life);
        emit(g, pt, colour, juce::jlimit(0.0f, 1.0f, bright * L.fade * edge * (1.0f - 0.3f * t)), L.size, sizeScale);
        ++n;
    }
    return n;
}

int drawSoundCloud(ParticleBatch& g, juce::Point<float> centre, juce::Colour colour, float energy, float innerPx,
                   float radiusPx, uint32_t seed, double timeSec, float sizeScale) {
    energy = juce::jlimit(0.0f, 1.0f, energy);
    if (energy < 0.004f) return 0;
    const float slots = std::min(360.0f, slotsFor(energy, radiusPx * 8.0f));
    // Louder sources push their cloud a little further out.
    const float reach = radiusPx * (0.6f + 0.4f * std::sqrt(energy));
    const float bright = 0.35f + 0.65f * std::sqrt(energy);
    int n = 0;
    for (int k = 0; k < (int)std::ceil(slots); ++k) {
        Life L;
        if (!lifeOf(seed, k, timeSec, L)) continue;
        const float edge = juce::jlimit(0.0f, 1.0f, slots - (float)k);
        const float a = L.rng.next() * juce::MathConstants<float>::twoPi, u = L.rng.next();
        // Born near its spot, drifting slightly outwards while it fades.
        const float dist = innerPx + reach * u * u + 4.0f * L.life;
        const auto pt = centre + juce::Point<float>(std::cos(a), std::sin(a)) * dist;
        emit(g, pt, colour, juce::jlimit(0.0f, 1.0f, bright * L.fade * edge * (1.0f - 0.55f * u)), L.size, sizeScale);
        ++n;
    }
    return n;
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
        const float r = juce::jlimit(2.2f, 9.0f, p.glowRadius / kDown);
        const float inv = 1.0f / (r * r * 0.35f);
        const float a = p.alpha * 0.6f * glowAmount;
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
                const float k = kGauss[(size_t)(d2 * inv * kLutScale)];
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
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
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
    // Cores on top of the glow.
    for (const auto& p : items_) {
        // Soft cores: round, and only about half as bright as the glow around them.
        g.setColour(p.core.withAlpha(p.alpha * 0.55f));
        g.fillEllipse(p.pt.x - p.size * 0.5f, p.pt.y - p.size * 0.5f, p.size, p.size);
    }
    items_.clear();
}

float reverbFlowEnergy(const SourceInfo& source, float level, float reverbWetDb) {
    if (source.mute || level < 0.02f || reverbWetDb <= -79.0f) return 0.0f;
    // -40 dB send and below shows nothing, 0 dB the full flow; the wet level scales it as well.
    const float send = juce::jlimit(0.0f, 1.0f, (source.reverbSendDb + 40.0f) / 40.0f);
    const float wet = juce::jlimit(0.0f, 1.0f, (reverbWetDb + 40.0f) / 40.0f);
    return 0.12f * std::sqrt(level) * send * wet;
}

} // namespace rkui
