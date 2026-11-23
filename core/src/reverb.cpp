#include "rk/reverb.hpp"

#include "rk/math.hpp"

#include <algorithm>
#include <cmath>

namespace rk {
namespace {

// Mutually prime-ish delay lengths in ms (size = 1).
constexpr float kLineMs[FdnReverb::kLines] = {29.7f, 37.1f, 41.1f, 43.7f, 53.1f, 59.3f, 61.7f, 67.1f,
                                              71.9f, 79.3f, 83.9f, 89.7f, 97.1f, 101.3f, 107.9f, 113.7f};
constexpr float kDiffuserMs[4] = {4.77f, 3.59f, 12.73f, 9.30f};
constexpr float kMaxSize = 2.0f;
constexpr float kMaxPreDelayMs = 250.0f;

inline void hadamard16(float* v) noexcept {
    for (int len = 1; len < 16; len <<= 1)
        for (int i = 0; i < 16; i += len << 1)
            for (int j = i; j < i + len; ++j) {
                const float a = v[j], b = v[j + len];
                v[j] = a + b;
                v[j + len] = a - b;
            }
    for (int i = 0; i < 16; ++i) v[i] *= 0.25f; // 1/sqrt(16): orthonormal
}

constexpr float kH4[4][4] = {{1, 1, 1, 1}, {1, -1, 1, -1}, {1, 1, -1, -1}, {1, -1, -1, 1}};

} // namespace

ReverbParams sanitize(ReverbParams p) {
    p.decaySec = safeClamp(p.decaySec, 0.2f, 12.0f, 1.8f);
    p.dampingHz = safeClamp(p.dampingHz, 1000.0f, 20000.0f, 6000.0f);
    p.preDelayMs = safeClamp(p.preDelayMs, 0.0f, kMaxPreDelayMs, 15.0f);
    p.size = safeClamp(p.size, 0.3f, kMaxSize, 1.0f);
    p.lowRatio = safeClamp(p.lowRatio, 0.3f, 3.0f, 1.0f);
    p.earlyDb = safeClamp(p.earlyDb, -80.0f, 6.0f, -6.0f);
    p.clusterDb = safeClamp(p.clusterDb, -80.0f, 6.0f, -9.0f);
    p.tailDb = safeClamp(p.tailDb, -80.0f, 6.0f, 0.0f);
    return p;
}

namespace {
float levelGain(float db) { return db <= -79.9f ? 0.0f : std::pow(10.0f, db / 20.0f); }

double onePoleCoef(double hz, double sr) { return std::clamp(1.0 - std::exp(-kTwoPi * hz / sr), 0.01, 1.0); }

/// |H| of y += a (x - y) at angular frequency w.
double onePoleMag(double a, double w) {
    const double b = 1.0 - a;
    return a / std::sqrt(std::max(1e-12, 1.0 - 2.0 * b * std::cos(w) + b * b));
}

/// |(1 - H) * gMid + H * gLow| / gMid for the low-decay shelf (complex sum).
double shelfMag(double a, double w, double ratio) {
    const double b = 1.0 - a;
    // H = a / (1 - b e^{-jw})
    const double re = 1.0 - b * std::cos(w), im = b * std::sin(w);
    const double den = re * re + im * im;
    const double hr = a * re / den, hi = -a * im / den;
    const double r = ratio;
    const double outR = (1.0 - hr) + r * hr, outI = -hi + r * hi;
    return std::sqrt(outR * outR + outI * outI);
}
} // namespace

float rt60At(const ReverbParams& raw, float hz, double sr) noexcept {
    const ReverbParams p = sanitize(raw);
    if (!(sr > 1000.0)) sr = 48000.0;
    const double w = kTwoPi * std::clamp(static_cast<double>(hz), 1.0, sr * 0.49) / sr;
    // Mean line length decides how often per second the loop gain is applied.
    double meanMs = 0;
    for (float ms : kLineMs) meanMs += ms;
    meanMs /= FdnReverb::kLines;
    const double len = meanMs * p.size * 0.001 * sr;
    const double gMid = std::pow(10.0, -3.0 * len / (p.decaySec * sr));
    const double gLow = std::pow(10.0, -3.0 * len / (p.decaySec * p.lowRatio * sr));
    const double loop = gMid * onePoleMag(onePoleCoef(p.dampingHz, sr), w) *
                        shelfMag(onePoleCoef(kLowCrossoverHz, sr), w, gLow / gMid);
    if (!(loop > 0.0) || loop >= 1.0) return p.decaySec * p.lowRatio;
    return static_cast<float>(-3.0 * len / (sr * std::log10(loop)));
}

float dampingForRt(const ReverbParams& raw, float hz, float rtSec, double sr) noexcept {
    ReverbParams p = sanitize(raw);
    // RT at hz rises monotonically with the damping frequency: bisect in log space.
    double lo = std::log(1000.0), hi = std::log(20000.0);
    for (int i = 0; i < 40; ++i) {
        const double mid = 0.5 * (lo + hi);
        p.dampingHz = static_cast<float>(std::exp(mid));
        if (rt60At(p, hz, sr) < rtSec) lo = mid;
        else hi = mid;
    }
    return static_cast<float>(std::exp(0.5 * (lo + hi)));
}

float FdnReverb::Allpass::process(float x) noexcept {
    const float d = buf[static_cast<size_t>(pos)];
    const float v = x + g * d;
    buf[static_cast<size_t>(pos)] = v;
    pos = (pos + 1) % static_cast<int>(buf.size());
    return d - g * v;
}

void FdnReverb::prepare(double sampleRate) {
    sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0 && sampleRate <= 768000.0) ? sampleRate : 48000.0;
    const auto samples = [this](float ms) { return static_cast<int>(std::ceil(ms * 0.001 * sampleRate_)) + 2; };
    pre_.buf.assign(static_cast<size_t>(samples(kMaxPreDelayMs)), 0.0f);
    for (int i = 0; i < 4; ++i) diffusers_[static_cast<size_t>(i)].buf.assign(static_cast<size_t>(samples(kDiffuserMs[i])), 0.0f);
    for (int j = 0; j < kLines; ++j) lines_[static_cast<size_t>(j)].buf.assign(static_cast<size_t>(samples(kLineMs[j] * kMaxSize)), 0.0f);
    early_.buf.assign(static_cast<size_t>(samples(kEarlyMs[kEarlyTaps - 1] * kMaxSize)), 0.0f);
    cluster_.buf.assign(static_cast<size_t>(samples(kClusterMs[kClusterTaps - 1] * kMaxSize)), 0.0f);
    prepared_ = true;
    reset();
    setParams(params_);
}

void FdnReverb::reset() noexcept {
    if (!prepared_) return;
    auto clear = [](std::vector<float>& v) { std::fill(v.begin(), v.end(), 0.0f); };
    clear(pre_.buf);
    pre_.pos = 0;
    for (auto& d : diffusers_) { clear(d.buf); d.pos = 0; }
    for (auto& l : lines_) { clear(l.buf); l.pos = 0; }
    clear(early_.buf);
    early_.pos = 0;
    clear(cluster_.buf);
    cluster_.pos = 0;
    lpState_.fill(0.0f);
    lowState_.fill(0.0f);
    pre_.cur = -1.0f;
    for (auto& l : lines_) l.cur = -1.0f;
    snapTaps_ = true;
}

void FdnReverb::setParams(const ReverbParams& raw) noexcept {
    params_ = sanitize(raw);
    if (!prepared_) return;
    const double sr = sampleRate_;
    pre_.length = std::clamp(static_cast<int>(params_.preDelayMs * 0.001 * sr), 0, static_cast<int>(pre_.buf.size()) - 2);
    for (int j = 0; j < kLines; ++j) {
        auto& l = lines_[static_cast<size_t>(j)];
        l.length = std::clamp(static_cast<int>(kLineMs[j] * params_.size * 0.001 * sr), 1, static_cast<int>(l.buf.size()) - 2);
        feedback_[static_cast<size_t>(j)] = static_cast<float>(std::pow(10.0, -3.0 * l.length / (params_.decaySec * sr)));
        lowFeedback_[static_cast<size_t>(j)] =
            static_cast<float>(std::pow(10.0, -3.0 * l.length / (params_.decaySec * params_.lowRatio * sr)));
    }
    lpCoef_ = static_cast<float>(onePoleCoef(params_.dampingHz, sr));
    lowCoef_ = static_cast<float>(onePoleCoef(kLowCrossoverHz, sr));
    const int earlyMax = static_cast<int>(early_.buf.size()) - 1;
    for (int k = 0; k < kEarlyTaps; ++k)
        earlyLen_[static_cast<size_t>(k)] = std::clamp(static_cast<int>(kEarlyMs[k] * params_.size * 0.001 * sr), 1, earlyMax);
    const int clusterMax = static_cast<int>(cluster_.buf.size()) - 1;
    for (int k = 0; k < kClusterTaps; ++k) {
        const float ms = kClusterMs[k] * params_.size;
        clusterLen_[static_cast<size_t>(k)] = std::clamp(static_cast<int>(ms * 0.001 * sr), 1, clusterMax);
        // The cluster fades like the room: -60 dB after decaySec.
        clusterGain_[static_cast<size_t>(k)] = 0.45f * std::pow(10.0f, -3.0f * ms * 0.001f / params_.decaySec);
    }
    earlyLevel_ = levelGain(params_.earlyDb);
    clusterLevel_ = levelGain(params_.clusterDb);
    tailLevel_ = levelGain(params_.tailDb);
}

void FdnReverb::process(const float* input, float* const* out, int numOut, int n, float gain) noexcept {
    if (!prepared_ || n <= 0) return;
    numOut = std::clamp(numOut, 0, 256);
    // Size and pre-delay glide: at most 3 % faster or slower reading, so a change (e.g.
    // while a scene fades) bends the pitch a little instead of clicking.
    constexpr float kGlide = 0.03f;
    // Tap positions glide per sample from where the last block ended (start) by `step` per sample.
    std::array<float, kEarlyTaps> eStart{}, eStep{};
    std::array<float, kClusterTaps> cStart{}, cStep{};
    std::array<float, kClusterTaps> cgStart{}, cgStep{}; // cluster tap gains, ramped over the block
    {
        const float maxMove = kGlide * static_cast<float>(n);
        auto glide = [&](float& cur, int target, float& start, float& step) {
            if (snapTaps_) cur = static_cast<float>(target);
            start = cur;
            cur += std::clamp(static_cast<float>(target) - cur, -maxMove, maxMove);
            step = (cur - start) / static_cast<float>(n);
        };
        for (int k = 0; k < kEarlyTaps; ++k)
            glide(earlyCur_[static_cast<size_t>(k)], earlyLen_[static_cast<size_t>(k)], eStart[static_cast<size_t>(k)], eStep[static_cast<size_t>(k)]);
        for (int k = 0; k < kClusterTaps; ++k)
            glide(clusterCur_[static_cast<size_t>(k)], clusterLen_[static_cast<size_t>(k)], cStart[static_cast<size_t>(k)], cStep[static_cast<size_t>(k)]);
        for (int k = 0; k < kClusterTaps; ++k) {
            float& g = clusterGainCur_[static_cast<size_t>(k)];
            const float target = clusterGain_[static_cast<size_t>(k)];
            if (snapTaps_) g = target;
            cgStart[static_cast<size_t>(k)] = g;
            cgStep[static_cast<size_t>(k)] = (target - g) / static_cast<float>(n);
            g = target;
        }
        snapTaps_ = false;
    }
    int idx[256][4];
    float sgn[256][4];
    // Each output hears its own four early taps and four cluster taps, so reflections
    // arrive from different directions, like walls at different distances.
    int eTap[256][4], cTap[256][4]; // which tap
    float eG[256][4], cG[256][4];
    for (int c = 0; c < numOut; ++c) {
        const int row = (c / 4) % 4;
        for (int k = 0; k < 4; ++k) {
            idx[c][k] = (c * 3 + k * 4 + c / 16) % kLines;
            sgn[c][k] = kH4[row][k] * 0.5f * gain * tailLevel_;
            const int e = (c * 5 + k * 2 + (k > 1 ? 1 : 0)) % kEarlyTaps;
            eTap[c][k] = e;
            eG[c][k] = kH4[(row + c) % 4][k] * kEarlyGain[e] * 0.5f * gain * earlyLevel_;
            const int q = (c * 7 + k * 4 + c / 4) % kClusterTaps;
            cTap[c][k] = q;
            cG[c][k] = kH4[(row + 1) % 4][k] * gain * clusterLevel_; // the tap's own gain glides below
        }
    }
    const bool doEarly = earlyLevel_ > 0.0f && gain != 0.0f;
    const bool doCluster = clusterLevel_ > 0.0f && gain != 0.0f;
    auto tap = [](const Delay& dl, float len) noexcept { // interpolated, len >= 1
        const int sz = static_cast<int>(dl.buf.size());
        const float c = std::clamp(len, 1.0f, static_cast<float>(sz - 2));
        const int i0 = static_cast<int>(c);
        const float f = c - static_cast<float>(i0);
        const float a = dl.buf[static_cast<size_t>((dl.pos - i0 + sz) % sz)];
        const float b = dl.buf[static_cast<size_t>((dl.pos - i0 - 1 + 2 * sz) % sz)];
        return a + (b - a) * f;
    };
    float eVal[kEarlyTaps];
    float cVal[kClusterTaps];
    float y[kLines];
    float d[kLines];
    for (int i = 0; i < n; ++i) {
        const float in = sanitizeSample(input[i]);
        float x;
        if (pre_.length > 0 || pre_.cur > 1.0f) {
            x = pre_.readGliding(kGlide);
            pre_.write(in);
        } else {
            pre_.cur = 0.0f;
            x = in;
            pre_.write(in);
        }
        early_.write(x);
        if (doEarly) {
            for (int k = 0; k < kEarlyTaps; ++k)
                eVal[k] = tap(early_, eStart[static_cast<size_t>(k)] + eStep[static_cast<size_t>(k)] * static_cast<float>(i + 1));
            for (int c = 0; c < numOut; ++c)
                out[c][i] += eG[c][0] * eVal[eTap[c][0]] + eG[c][1] * eVal[eTap[c][1]] + eG[c][2] * eVal[eTap[c][2]] + eG[c][3] * eVal[eTap[c][3]];
        }
        for (auto& ap : diffusers_) x = ap.process(x);
        cluster_.write(x);
        if (doCluster) {
            for (int k = 0; k < kClusterTaps; ++k)
                cVal[k] = tap(cluster_, cStart[static_cast<size_t>(k)] + cStep[static_cast<size_t>(k)] * static_cast<float>(i + 1)) *
                          (cgStart[static_cast<size_t>(k)] + cgStep[static_cast<size_t>(k)] * static_cast<float>(i + 1));
            for (int c = 0; c < numOut; ++c)
                out[c][i] += cG[c][0] * cVal[cTap[c][0]] + cG[c][1] * cVal[cTap[c][1]] + cG[c][2] * cVal[cTap[c][2]] + cG[c][3] * cVal[cTap[c][3]];
        }

        for (int j = 0; j < kLines; ++j) {
            y[j] = lines_[static_cast<size_t>(j)].readGliding(kGlide);
            float& lp = lpState_[static_cast<size_t>(j)];
            lp += lpCoef_ * (y[j] - lp);
            lp = sanitizeSample(lp);
            // Two-band decay: below the crossover the line uses its own (longer or shorter) feedback.
            float& lo = lowState_[static_cast<size_t>(j)];
            lo += lowCoef_ * (lp - lo);
            lo = sanitizeSample(lo);
            d[j] = (lp - lo) * feedback_[static_cast<size_t>(j)] + lo * lowFeedback_[static_cast<size_t>(j)];
        }
        hadamard16(d);
        for (int j = 0; j < kLines; ++j) {
            const float inj = (j & 1) ? -0.25f * x : 0.25f * x;
            lines_[static_cast<size_t>(j)].write(d[j] + inj);
        }
        if (gain != 0.0f && tailLevel_ > 0.0f)
            for (int c = 0; c < numOut; ++c)
                out[c][i] += sgn[c][0] * y[idx[c][0]] + sgn[c][1] * y[idx[c][1]] + sgn[c][2] * y[idx[c][2]] + sgn[c][3] * y[idx[c][3]];
    }
}

} // namespace rk
