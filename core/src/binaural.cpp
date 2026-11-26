#include "rk/binaural.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <string>

namespace rk {
namespace {
constexpr float kHeadRadius = 0.0875f; // metres
// Pinna echoes (Brown & Duda, table 1): reflection gain, and delay = A cos(az/2) sin(D (90 - el)) + B
// samples at 44.1 kHz. Gains are scaled down so the colouring stays mild with any ear shape.
constexpr float kPinnaRho[5] = {0.5f, -1.0f, 0.5f, -0.25f, 0.25f};
constexpr float kPinnaA[5] = {1.0f, 5.0f, 5.0f, 5.0f, 5.0f};
constexpr float kPinnaB[5] = {2.0f, 4.0f, 7.0f, 11.0f, 13.0f};
constexpr float kPinnaD[5] = {1.0f, 0.5f, 0.5f, 0.5f, 0.5f};
constexpr float kPinnaAmount = 0.6f;
constexpr float kRearCutDb = -5.0f;     // treble directly behind
constexpr float kRearCornerHz = 4000.0f;
} // namespace

SpeakerLayout binauralVirtualLayout() {
    SpeakerLayout l;
    l.name = "Binaural";
    auto add = [&l](float az, float el) {
        Speaker s;
        s.name = std::to_string(l.speakers.size() + 1);
        s.position = toCartesian({az, el, 2.0f});
        l.speakers.push_back(s);
    };
    for (int i = 0; i < 8; ++i) add(wrapDegrees(45.0f * static_cast<float>(i)), 0.0f);
    for (int i = 0; i < 6; ++i) add(wrapDegrees(30.0f + 60.0f * static_cast<float>(i)), 40.0f);
    add(0.0f, 90.0f);
    for (int i = 0; i < 4; ++i) add(wrapDegrees(45.0f + 90.0f * static_cast<float>(i)), -35.0f);
    return l;
}

void BinauralFold::prepare(const SpeakerLayout& layout, double sampleRate, const HrirSet* hrirs) {
    measured_ = false;
    conv_.clear();
    fdl_.clear();
    if (hrirs != nullptr && hrirs->valid()) {
        prepareMeasured(layout, sampleRate, *hrirs);
        ch_.clear();
        return;
    }
    const double sr = (std::isfinite(sampleRate) && sampleRate >= 8000.0) ? sampleRate : 48000.0;
    const int n = std::min(static_cast<int>(layout.speakers.size()), kMaxSpeakers);
    ch_.assign(static_cast<size_t>(n), Channel{});
    const float maxItd = 2.0f * kHeadRadius / kSpeedOfSound * 1.6f; // generous upper bound
    const float pinnaScale = static_cast<float>(sr / 44100.0);
    const float maxPinna = (kPinnaA[1] + kPinnaB[4]) * pinnaScale;
    historyLen_ = static_cast<int>(std::ceil(maxItd * sr + maxPinna)) + 4;
    const float w0 = kSpeedOfSound / kHeadRadius;
    const float K = static_cast<float>(2.0 * sr);
    for (int m = 0; m < n; ++m) {
        const Speaker& s = layout.speakers[static_cast<size_t>(m)];
        Channel& c = ch_[static_cast<size_t>(m)];
        c.history.assign(static_cast<size_t>(historyLen_), 0.0f);
        c.active = s.enabled;
        const Vec3 dir = normalized(s.position);
        const Polar pol = toPolar(s.position);
        const float azRad = pol.azimuthDeg * kDegToRad, elDeg = std::clamp(pol.elevationDeg, -90.0f, 90.0f);
        const float front = std::cos(azRad); // 1 ahead, -1 behind
        const float rear = std::max(0.0f, -front) * std::cos(elDeg * kDegToRad);
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
            for (int k = 0; k < 5; ++k) {
                const float d = kPinnaA[k] * std::cos(azRad * 0.5f) * std::sin(kPinnaD[k] * (90.0f - elDeg) * kDegToRad) + kPinnaB[k];
                ear.pinnaDelay[k] = std::max(1.0f, d * pinnaScale);
            }
            ear.rearA = 1.0f - std::exp(-2.0f * kPi * kRearCornerHz / static_cast<float>(sr));
            ear.rearG = s.isSubwoofer ? 0.0f : dbToGain(kRearCutDb * rear) - 1.0f;
        }
    }
}

void BinauralFold::reset() noexcept {
    for (auto& c : conv_) std::fill(c.time.begin(), c.time.end(), 0.0f);
    std::fill(fdl_.begin(), fdl_.end(), cpx{});
    for (int e = 0; e < 2; ++e) std::fill(out_[e].begin(), out_[e].end(), 0.0f);
    fill_ = 0;
    for (auto& c : ch_) {
        std::fill(c.history.begin(), c.history.end(), 0.0f);
        c.pos = 0;
        c.ear[0].z = c.ear[1].z = 0.0f;
        c.ear[0].rearLp = c.ear[1].rearLp = 0.0f;
    }
}

float BinauralFold::tap(const Channel& c, float delay) const noexcept {
    float rp = static_cast<float>(c.pos) - delay;
    while (rp < 0.0f) rp += static_cast<float>(historyLen_);
    const int i0 = static_cast<int>(rp) % historyLen_;
    const float frac = rp - std::floor(rp);
    const int i1 = (i0 + 1) % historyLen_;
    // Linear interpolation between the sample at i0 and the newer one at i1.
    return c.history[static_cast<size_t>(i0)] * (1.0f - frac) + c.history[static_cast<size_t>(i1)] * frac;
}

void BinauralFold::process(const float* const* speakers, int numSpeakers, float* left, float* right, int n) noexcept {
    if (measured_) {
        processMeasured(speakers, numSpeakers, left, right, n);
        return;
    }
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
                float x = tap(c, ear.delay);
                for (int k = 0; k < 5; ++k) x += kPinnaAmount * kPinnaRho[k] * tap(c, ear.delay + ear.pinnaDelay[k]);
                if (ear.rearG < -1.0e-4f) {
                    ear.rearLp = sanitizeSample(ear.rearLp + ear.rearA * (x - ear.rearLp));
                    x += ear.rearG * (x - ear.rearLp);
                }
                // Direct form II transposed, first order.
                const float y = ear.b0 * x + ear.z;
                ear.z = sanitizeSample(ear.b1 * x - ear.a1 * y);
                outs[e][i] += y * ear.gain;
            }
            c.pos = (c.pos + 1) % historyLen_;
        }
    }
}

// ---------------------------------------------------------------------------
// Measured HRIRs
// ---------------------------------------------------------------------------

bool HrirSet::valid() const noexcept {
    const size_t n = directions.size() * static_cast<size_t>(std::max(0, length));
    return length > 0 && !directions.empty() && left.size() == n && right.size() == n &&
           std::isfinite(sampleRate) && sampleRate >= 8000.0 && sampleRate <= 768000.0;
}

int HrirSet::nearest(Vec3 direction) const noexcept {
    const float len = rk::length(direction);
    const Vec3 d = len > 1.0e-6f ? direction * (1.0f / len) : Vec3{0, 1, 0};
    int best = 0;
    float bestDot = -2.0f;
    for (size_t i = 0; i < directions.size(); ++i) {
        const float k = dot(d, directions[i]);
        if (k > bestDot) {
            bestDot = k;
            best = static_cast<int>(i);
        }
    }
    return best;
}

bool HrirSet::finalize() {
    const size_t m = directions.size();
    if (length <= 0 || m == 0 || left.size() != m * (size_t)length || right.size() != m * (size_t)length) return false;
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 768000.0) return false;
    for (auto& d : directions) {
        const float len = rk::length(d);
        d = (std::isfinite(len) && len > 1.0e-6f) ? d * (1.0f / len) : Vec3{0, 1, 0};
    }
    for (auto* v : {&left, &right})
        for (float& x : *v)
            if (!std::isfinite(x)) return false;
    // Earliest onset over all responses: the first sample above 5 % of that response's peak.
    int onset = length;
    for (auto* v : {&left, &right})
        for (size_t i = 0; i < m; ++i) {
            const float* h = v->data() + i * (size_t)length;
            float peak = 0.0f;
            for (int k = 0; k < length; ++k) peak = std::max(peak, std::abs(h[k]));
            if (peak <= 0.0f) continue;
            for (int k = 0; k < length; ++k)
                if (std::abs(h[k]) > 0.05f * peak) {
                    onset = std::min(onset, k);
                    break;
                }
        }
    if (onset >= length) return false; // all silent
    const int trim = std::max(0, onset - 4);
    const int keep = std::min(length - trim, std::max(16, static_cast<int>(std::lround(0.02 * sampleRate))));
    std::vector<float> nl(m * (size_t)keep), nr(m * (size_t)keep);
    double energy = 0.0;
    for (size_t i = 0; i < m; ++i)
        for (int k = 0; k < keep; ++k) {
            const float a = left[i * (size_t)length + (size_t)(trim + k)];
            const float b = right[i * (size_t)length + (size_t)(trim + k)];
            nl[i * (size_t)keep + (size_t)k] = a;
            nr[i * (size_t)keep + (size_t)k] = b;
            energy += 0.5 * ((double)a * a + (double)b * b);
        }
    energy /= (double)m;
    if (!(energy > 1.0e-12)) return false;
    const float g = static_cast<float>(1.0 / std::sqrt(energy));
    for (float& x : nl) x *= g;
    for (float& x : nr) x *= g;
    left = std::move(nl);
    right = std::move(nr);
    length = keep;
    return true;
}

namespace {
/// Band-limited resampling of a short response (windowed sinc, 16 zero crossings).
std::vector<float> resampleResponse(const float* h, int len, double from, double to) {
    if (std::abs(from - to) < 0.5) return std::vector<float>(h, h + len);
    const double ratio = from / to; // input samples per output sample
    const int outLen = std::max(1, static_cast<int>(std::ceil(len / ratio)));
    const double cutoff = std::min(1.0, to / from) * 0.97;
    constexpr int kZeros = 16;
    const double half = kZeros / cutoff;
    std::vector<float> out(static_cast<size_t>(outLen), 0.0f);
    for (int j = 0; j < outLen; ++j) {
        const double t = j * ratio;
        const int k0 = std::max(0, static_cast<int>(std::ceil(t - half)));
        const int k1 = std::min(len - 1, static_cast<int>(std::floor(t + half)));
        double acc = 0.0;
        for (int k = k0; k <= k1; ++k) {
            const double x = t - k;
            const double arg = kPi * cutoff * x;
            const double sinc = std::abs(arg) < 1.0e-9 ? 1.0 : std::sin(arg) / arg;
            const double w = 0.5 + 0.5 * std::cos(kPi * x / half); // Hann
            acc += h[k] * cutoff * sinc * w;
        }
        out[static_cast<size_t>(j)] = static_cast<float>(acc);
    }
    return out;
}
} // namespace

void BinauralFold::prepareMeasured(const SpeakerLayout& layout, double sampleRate, const HrirSet& set) {
    const double sr = (std::isfinite(sampleRate) && sampleRate >= 8000.0) ? sampleRate : 48000.0;
    const int n = std::min(static_cast<int>(layout.speakers.size()), kMaxSpeakers);
    // Up to about 10 ms at the engine rate: the part of a head response that matters.
    const int maxLen = std::clamp(static_cast<int>(std::lround(0.0107 * sr)), kBlock, 16 * kBlock);
    std::vector<std::vector<float>> resp[2];
    int len = 1;
    for (int e = 0; e < 2; ++e) resp[e].resize(static_cast<size_t>(n));
    for (int m = 0; m < n; ++m) {
        const Speaker& s = layout.speakers[static_cast<size_t>(m)];
        const int d = set.nearest(s.position);
        for (int e = 0; e < 2; ++e) {
            const float* h = (e == 0 ? set.left : set.right).data() + (size_t)d * (size_t)set.length;
            auto r = resampleResponse(h, set.length, set.sampleRate, sr);
            if ((int)r.size() > maxLen) {
                r.resize(static_cast<size_t>(maxLen));
                constexpr int kFade = 16; // half Hann, so the cut does not click
                for (int k = 0; k < kFade; ++k)
                    r[static_cast<size_t>(maxLen - kFade + k)] *= 0.5f + 0.5f * std::cos(kPi * (k + 1) / kFade);
            }
            const float g = s.isSubwoofer ? 0.5f : 1.0f;
            for (float& x : r) x *= g;
            len = std::max(len, (int)r.size());
            resp[e][static_cast<size_t>(m)] = std::move(r);
        }
    }
    partitions_ = (len + kBlock - 1) / kBlock;

    // FFT tables.
    twiddle_.resize(kFft / 2);
    for (int k = 0; k < kFft / 2; ++k) twiddle_[(size_t)k] = std::polar(1.0f, -2.0f * kPi * (float)k / (float)kFft);
    bitrev_.resize(kFft);
    int bits = 0;
    while ((1 << bits) < kFft) ++bits;
    for (int i = 0; i < kFft; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b) r |= ((i >> b) & 1) << (bits - 1 - b);
        bitrev_[(size_t)i] = r;
    }
    work_.assign(kFft, cpx{});
    for (int e = 0; e < 2; ++e) {
        acc_[e].assign(kBins, cpx{});
        out_[e].assign(kBlock, 0.0f);
    }

    conv_.assign(static_cast<size_t>(n), Conv{});
    for (int m = 0; m < n; ++m) {
        Conv& c = conv_[static_cast<size_t>(m)];
        c.active = layout.speakers[static_cast<size_t>(m)].enabled;
        c.time.assign(kFft, 0.0f);
        for (int e = 0; e < 2; ++e) {
            c.h[e].assign(static_cast<size_t>(partitions_) * kBins, cpx{});
            const auto& r = resp[e][static_cast<size_t>(m)];
            for (int p = 0; p < partitions_; ++p) {
                std::fill(work_.begin(), work_.end(), cpx{});
                for (int k = 0; k < kBlock; ++k) {
                    const size_t idx = (size_t)(p * kBlock + k);
                    if (idx < r.size()) work_[(size_t)k] = r[idx];
                }
                fft(work_.data(), false);
                std::copy(work_.begin(), work_.begin() + kBins, c.h[e].begin() + (ptrdiff_t)p * kBins);
            }
        }
    }
    fdl_.assign(static_cast<size_t>(n) * (size_t)partitions_ * kBins, cpx{});
    fdlPos_ = 0;
    fill_ = 0;
    measured_ = true;
}

void BinauralFold::fft(cpx* x, bool inverse) const noexcept {
    for (int i = 0; i < kFft; ++i) {
        const int j = bitrev_[(size_t)i];
        if (j > i) std::swap(x[i], x[j]);
    }
    for (int size = 2; size <= kFft; size *= 2) {
        const int half = size / 2, step = kFft / size;
        for (int start = 0; start < kFft; start += size)
            for (int k = 0; k < half; ++k) {
                cpx w = twiddle_[(size_t)(k * step)];
                if (inverse) w = std::conj(w);
                const cpx t = w * x[start + k + half];
                x[start + k + half] = x[start + k] - t;
                x[start + k] += t;
            }
    }
}

void BinauralFold::runBlock() noexcept {
    const int n = static_cast<int>(conv_.size());
    fdlPos_ = (fdlPos_ + 1) % partitions_;
    for (int e = 0; e < 2; ++e) std::fill(acc_[e].begin(), acc_[e].end(), cpx{});
    for (int m = 0; m < n; ++m) {
        Conv& c = conv_[static_cast<size_t>(m)];
        if (!c.active) continue;
        cpx* fdl = fdl_.data() + (size_t)m * (size_t)partitions_ * kBins;
        for (int k = 0; k < kFft; ++k) work_[(size_t)k] = c.time[(size_t)k];
        fft(work_.data(), false);
        std::copy(work_.begin(), work_.begin() + kBins, fdl + (ptrdiff_t)fdlPos_ * kBins);
        for (int p = 0; p < partitions_; ++p) {
            const cpx* x = fdl + (ptrdiff_t)((fdlPos_ - p + partitions_) % partitions_) * kBins;
            for (int e = 0; e < 2; ++e) {
                const cpx* h = c.h[e].data() + (ptrdiff_t)p * kBins;
                cpx* a = acc_[e].data();
                for (int k = 0; k < kBins; ++k) a[k] += x[k] * h[k];
            }
        }
        std::copy(c.time.begin() + kBlock, c.time.end(), c.time.begin()); // slide by one block
    }
    for (int e = 0; e < 2; ++e) {
        work_[0] = acc_[e][0];
        work_[kBlock] = acc_[e][kBlock];
        for (int k = 1; k < kBlock; ++k) {
            work_[(size_t)k] = acc_[e][(size_t)k];
            work_[(size_t)(kFft - k)] = std::conj(acc_[e][(size_t)k]);
        }
        fft(work_.data(), true);
        const float scale = 1.0f / (float)kFft;
        for (int k = 0; k < kBlock; ++k) out_[e][(size_t)k] = sanitizeSample(work_[(size_t)(kBlock + k)].real() * scale);
    }
}

void BinauralFold::processMeasured(const float* const* speakers, int numSpeakers, float* left, float* right, int n) noexcept {
    numSpeakers = std::min(numSpeakers, static_cast<int>(conv_.size()));
    int i = 0;
    while (i < n) {
        const int chunk = std::min(n - i, kBlock - fill_);
        for (int m = 0; m < static_cast<int>(conv_.size()); ++m) {
            Conv& c = conv_[static_cast<size_t>(m)];
            if (!c.active) continue;
            float* dst = c.time.data() + kBlock + fill_;
            if (m < numSpeakers && speakers[m] != nullptr) std::copy(speakers[m] + i, speakers[m] + i + chunk, dst);
            else std::fill(dst, dst + chunk, 0.0f);
        }
        std::copy(out_[0].begin() + fill_, out_[0].begin() + fill_ + chunk, left + i);
        std::copy(out_[1].begin() + fill_, out_[1].begin() + fill_ + chunk, right + i);
        fill_ += chunk;
        i += chunk;
        if (fill_ == kBlock) {
            runBlock();
            fill_ = 0;
        }
    }
}

} // namespace rk
