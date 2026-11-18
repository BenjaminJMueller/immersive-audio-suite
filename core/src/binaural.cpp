#include "rk/binaural.hpp"

#include <algorithm>
#include <cmath>

namespace rk {
namespace {
constexpr float kHeadRadius = 0.0875f; // metres
} // namespace

void BinauralFold::prepare(const SpeakerLayout& layout, double sampleRate) {
    const double sr = (std::isfinite(sampleRate) && sampleRate >= 8000.0) ? sampleRate : 48000.0;
    const int n = std::min(static_cast<int>(layout.speakers.size()), kMaxSpeakers);
    ch_.assign(static_cast<size_t>(n), Channel{});
    const float maxItd = 2.0f * kHeadRadius / kSpeedOfSound * 1.6f; // generous upper bound
    historyLen_ = static_cast<int>(std::ceil(maxItd * sr)) + 4;
    const float w0 = kSpeedOfSound / kHeadRadius;
    const float K = static_cast<float>(2.0 * sr);
    for (int m = 0; m < n; ++m) {
        const Speaker& s = layout.speakers[static_cast<size_t>(m)];
        Channel& c = ch_[static_cast<size_t>(m)];
        c.history.assign(static_cast<size_t>(historyLen_), 0.0f);
        c.active = s.enabled;
        const Vec3 dir = normalized(s.position);
        for (int e = 0; e < 2; ++e) {
            // x points to the right, so the left ear looks towards -x.
            const Vec3 earAxis = e == 0 ? Vec3{-1, 0, 0} : Vec3{1, 0, 0};
            const float cosInc = std::clamp(dot(dir, earAxis), -1.0f, 1.0f);
            const float theta = std::acos(cosInc) * kRadToDeg; // 0 = ear facing the speaker
            // Head shadow (Brown & Duda): alpha 2 at the facing ear, ~0.1 behind the head.
            constexpr float aMin = 0.1f, thetaMin = 150.0f;
            const float alpha = (1.0f + aMin / 2.0f) + (1.0f - aMin / 2.0f) * std::cos(theta / thetaMin * 180.0f * kDegToRad);
            const float t = K / (2.0f * w0);
            const float a0 = 1.0f + t;
            Ear& ear = c.ear[e];
            ear.b0 = (1.0f + alpha * t) / a0;
            ear.b1 = (1.0f - alpha * t) / a0;
            ear.a1 = (1.0f - t) / a0;
            // Woodworth ITD relative to the head centre, made positive.
            const float thetaRad = theta * kDegToRad;
            float tau = thetaRad < kPi / 2 ? -kHeadRadius / kSpeedOfSound * std::cos(thetaRad)
                                           : kHeadRadius / kSpeedOfSound * (thetaRad - kPi / 2);
            tau += kHeadRadius / kSpeedOfSound;
            ear.delay = std::clamp(static_cast<float>(tau * sr), 0.0f, static_cast<float>(historyLen_ - 3));
            ear.gain = s.isSubwoofer ? 0.5f : 1.0f;
        }
    }
}

void BinauralFold::reset() noexcept {
    for (auto& c : ch_) {
        std::fill(c.history.begin(), c.history.end(), 0.0f);
        c.pos = 0;
        c.ear[0].z = c.ear[1].z = 0.0f;
    }
}

void BinauralFold::process(const float* const* speakers, int numSpeakers, float* left, float* right, int n) noexcept {
    std::fill(left, left + n, 0.0f);
    std::fill(right, right + n, 0.0f);
    numSpeakers = std::min(numSpeakers, static_cast<int>(ch_.size()));
    for (int m = 0; m < numSpeakers; ++m) {
        Channel& c = ch_[static_cast<size_t>(m)];
        if (!c.active || speakers[m] == nullptr) continue;
        const float* in = speakers[m];
        float* outs[2] = {left, right};
        for (int i = 0; i < n; ++i) {
            c.history[static_cast<size_t>(c.pos)] = in[i];
            for (int e = 0; e < 2; ++e) {
                Ear& ear = c.ear[e];
                float rp = static_cast<float>(c.pos) - ear.delay;
                if (rp < 0.0f) rp += static_cast<float>(historyLen_);
                const int i0 = static_cast<int>(rp);
                const float frac = rp - static_cast<float>(i0);
                const int i1 = (i0 + 1) % historyLen_;
                // Linear interpolation between the sample at i0 and the newer one at i1.
                const float x = c.history[static_cast<size_t>(i0 % historyLen_)] * (1.0f - frac) + c.history[static_cast<size_t>(i1)] * frac;
                // Direct form II transposed, first order.
                const float y = ear.b0 * x + ear.z;
                ear.z = sanitizeSample(ear.b1 * x - ear.a1 * y);
                outs[e][i] += y * ear.gain;
            }
            c.pos = (c.pos + 1) % historyLen_;
        }
    }
}

} // namespace rk
