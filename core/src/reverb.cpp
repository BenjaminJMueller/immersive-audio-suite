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
    return p;
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
    lpState_.fill(0.0f);
}

void FdnReverb::setParams(const ReverbParams& raw) noexcept {
    params_ = sanitize(raw);
    if (!prepared_) return;
    const double sr = sampleRate_;
    pre_.length = std::clamp(static_cast<int>(params_.preDelayMs * 0.001 * sr), 0, static_cast<int>(pre_.buf.size()) - 1);
    for (int j = 0; j < kLines; ++j) {
        auto& l = lines_[static_cast<size_t>(j)];
        l.length = std::clamp(static_cast<int>(kLineMs[j] * params_.size * 0.001 * sr), 1, static_cast<int>(l.buf.size()) - 1);
        feedback_[static_cast<size_t>(j)] = static_cast<float>(std::pow(10.0, -3.0 * l.length / (params_.decaySec * sr)));
    }
    lpCoef_ = static_cast<float>(1.0 - std::exp(-kTwoPi * params_.dampingHz / sr));
    lpCoef_ = std::clamp(lpCoef_, 0.01f, 1.0f);
}

void FdnReverb::process(const float* input, float* const* out, int numOut, int n, float gain) noexcept {
    if (!prepared_ || n <= 0) return;
    numOut = std::clamp(numOut, 0, 256);
    int idx[256][4];
    float sgn[256][4];
    for (int c = 0; c < numOut; ++c) {
        const int row = (c / 4) % 4;
        for (int k = 0; k < 4; ++k) {
            idx[c][k] = (c * 3 + k * 4 + c / 16) % kLines;
            sgn[c][k] = kH4[row][k] * 0.5f * gain;
        }
    }
    float y[kLines];
    float d[kLines];
    for (int i = 0; i < n; ++i) {
        const float in = sanitizeSample(input[i]);
        float x;
        if (pre_.length > 0) {
            x = pre_.read();
            pre_.write(in);
        } else {
            x = in;
        }
        for (auto& ap : diffusers_) x = ap.process(x);

        for (int j = 0; j < kLines; ++j) {
            y[j] = lines_[static_cast<size_t>(j)].read();
            float& lp = lpState_[static_cast<size_t>(j)];
            lp += lpCoef_ * (y[j] - lp);
            lp = sanitizeSample(lp);
            d[j] = lp * feedback_[static_cast<size_t>(j)];
        }
        hadamard16(d);
        for (int j = 0; j < kLines; ++j) {
            const float inj = (j & 1) ? -0.25f * x : 0.25f * x;
            lines_[static_cast<size_t>(j)].write(d[j] + inj);
        }
        if (gain != 0.0f)
            for (int c = 0; c < numOut; ++c)
                out[c][i] += sgn[c][0] * y[idx[c][0]] + sgn[c][1] * y[idx[c][1]] + sgn[c][2] * y[idx[c][2]] + sgn[c][3] * y[idx[c][3]];
    }
}

} // namespace rk
