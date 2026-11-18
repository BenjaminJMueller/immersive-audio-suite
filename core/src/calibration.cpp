#include "rk/calibration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace rk::calib {

namespace {

constexpr double kPiD = 3.14159265358979323846;

using cd = std::complex<double>;

int nextPow2(int n) {
    int p = 1;
    while (p < n && p < (1 << 26)) p <<= 1;
    return p;
}

/// In-place iterative radix-2 FFT. `inverse` includes the 1/N scaling.
void fft(std::vector<cd>& a, bool inverse) {
    const size_t n = a.size();
    if (n < 2) return;
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * kPiD / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
        const cd wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cd w(1.0, 0.0);
            const size_t half = len / 2;
            for (size_t k = 0; k < half; ++k) {
                const cd u = a[i + k];
                const cd v = a[i + k + half] * w;
                a[i + k] = u + v;
                a[i + k + half] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse) {
        const double s = 1.0 / static_cast<double>(n);
        for (auto& x : a) x *= s;
    }
}

bool finite(double v) { return std::isfinite(v); }

float median(std::vector<float> v) {
    if (v.empty()) return 0.0f;
    const size_t m = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(m), v.end());
    float r = v[m];
    if (v.size() % 2 == 0) {
        const float lo = *std::max_element(v.begin(), v.begin() + static_cast<long>(m));
        r = 0.5f * (r + lo);
    }
    return r;
}

double medianD(std::vector<double> v) {
    if (v.empty()) return 0.0;
    const size_t m = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(m), v.end());
    return v[m];
}

} // namespace

float speedOfSound(float celsius) {
    const float t = safeClamp(celsius, -20.0f, 50.0f, 20.0f);
    return 331.3f * std::sqrt(1.0f + t / 273.15f);
}

// ---------------------------------------------------------------------------------------------
// Sweep

SweepParams sanitize(SweepParams p) {
    auto clampD = [](double v, double lo, double hi, double fb) {
        if (!finite(v)) return fb;
        return std::clamp(v, lo, hi);
    };
    p.sampleRate = clampD(p.sampleRate, 8000.0, 384000.0, 48000.0);
    const double nyq = 0.45 * p.sampleRate;
    p.f2 = clampD(p.f2, 200.0, nyq, std::min(20000.0, nyq));
    p.f1 = clampD(p.f1, 10.0, p.f2 / 4.0, 50.0);
    p.seconds = clampD(p.seconds, 0.2, 10.0, 1.5);
    p.fadeSeconds = clampD(p.fadeSeconds, 0.001, p.seconds * 0.25, 0.03);
    p.tailSeconds = clampD(p.tailSeconds, 0.2, 5.0, 0.9);
    p.levelDb = safeClamp(p.levelDb, -60.0f, -3.0f, -18.0f);
    return p;
}

std::vector<float> makeSweep(const SweepParams& in) {
    const SweepParams p = sanitize(in);
    const int n = static_cast<int>(std::lround(p.seconds * p.sampleRate));
    std::vector<float> s(static_cast<size_t>(n));
    const double T = p.seconds;
    const double L = std::log(p.f2 / p.f1);
    const double k = 2.0 * kPiD * p.f1 * T / L;
    const double amp = dbToGain(p.levelDb);
    const int fade = std::max(1, static_cast<int>(p.fadeSeconds * p.sampleRate));
    for (int i = 0; i < n; ++i) {
        const double t = i / p.sampleRate;
        double v = std::sin(k * (std::exp(t / T * L) - 1.0));
        if (i < fade) v *= 0.5 - 0.5 * std::cos(kPiD * i / fade);
        if (i >= n - fade) v *= 0.5 - 0.5 * std::cos(kPiD * (n - 1 - i) / fade);
        s[static_cast<size_t>(i)] = static_cast<float>(v * amp);
    }
    return s;
}

int recordLength(const SweepParams& in) {
    const SweepParams p = sanitize(in);
    return static_cast<int>(std::lround((p.seconds + p.tailSeconds) * p.sampleRate));
}

// ---------------------------------------------------------------------------------------------
// SweepRunner

void SweepRunner::prepare(const std::vector<float>& sweep, int recordSamples) {
    if (busy()) return;
    sweep_ = sweep;
    for (auto& v : sweep_)
        if (!std::isfinite(v)) v = 0.0f;
    recLen_ = std::clamp(recordSamples, 1, 384000 * 20);
    rec_.assign(static_cast<size_t>(recLen_), 0.0f);
    loop_.assign(static_cast<size_t>(recLen_), 0.0f);
    state_.store(static_cast<int>(State::Idle), std::memory_order_release);
}

bool SweepRunner::start(int outChannel, int inChannel, int loopbackChannel) {
    if (busy() || rec_.empty() || outChannel < 0 || inChannel < 0) return false;
    out_ = outChannel;
    in_ = inChannel;
    loopIn_ = loopbackChannel;
    cancel_.store(false, std::memory_order_relaxed);
    posPublic_.store(0, std::memory_order_relaxed);
    state_.store(static_cast<int>(State::Armed), std::memory_order_release);
    return true;
}

void SweepRunner::cancel() { cancel_.store(true, std::memory_order_release); }

float SweepRunner::progress() const noexcept {
    if (recLen_ <= 0) return 0.0f;
    if (state() == State::Done) return 1.0f;
    return std::clamp(static_cast<float>(posPublic_.load(std::memory_order_relaxed)) / static_cast<float>(recLen_), 0.0f, 1.0f);
}

bool SweepRunner::process(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                          int numSamples) noexcept {
    int st = state_.load(std::memory_order_acquire);
    if (st != static_cast<int>(State::Armed) && st != static_cast<int>(State::Running)) return false;
    if (cancel_.load(std::memory_order_acquire)) {
        cancel_.store(false, std::memory_order_relaxed);
        state_.store(static_cast<int>(State::Idle), std::memory_order_release);
        return false;
    }
    if (st == static_cast<int>(State::Armed)) {
        pos_ = 0;
        inputPeak_.store(0.0f, std::memory_order_relaxed);
        state_.store(static_cast<int>(State::Running), std::memory_order_release);
    }
    if (numSamples < 0) numSamples = 0;
    for (int c = 0; c < numOutputs; ++c)
        if (outputs != nullptr && outputs[c] != nullptr) std::fill(outputs[c], outputs[c] + numSamples, 0.0f);

    float* out = (outputs != nullptr && out_ < numOutputs) ? outputs[out_] : nullptr;
    const float* in = (inputs != nullptr && in_ < numInputs) ? inputs[in_] : nullptr;
    const float* lin = (inputs != nullptr && loopIn_ >= 0 && loopIn_ < numInputs) ? inputs[loopIn_] : nullptr;
    const int sweepLen = static_cast<int>(sweep_.size());
    float peak = inputPeak_.load(std::memory_order_relaxed);
    for (int i = 0; i < numSamples && pos_ < recLen_; ++i, ++pos_) {
        if (out != nullptr) out[i] = pos_ < sweepLen ? sweep_[static_cast<size_t>(pos_)] : 0.0f;
        float x = in != nullptr ? in[i] : 0.0f;
        if (!std::isfinite(x)) x = 0.0f;
        rec_[static_cast<size_t>(pos_)] = x;
        peak = std::max(peak, std::fabs(x));
        float l = lin != nullptr ? lin[i] : 0.0f;
        if (!std::isfinite(l)) l = 0.0f;
        loop_[static_cast<size_t>(pos_)] = l;
    }
    inputPeak_.store(peak, std::memory_order_relaxed);
    posPublic_.store(pos_, std::memory_order_relaxed);
    if (pos_ >= recLen_) state_.store(static_cast<int>(State::Done), std::memory_order_release);
    return true;
}

bool SweepRunner::take(std::vector<float>& recording, std::vector<float>& loopback) {
    if (state() != State::Done) return false;
    recording = rec_;
    loopback = loop_;
    state_.store(static_cast<int>(State::Idle), std::memory_order_release);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Analysis

std::vector<float> deconvolve(const std::vector<float>& recording, const std::vector<float>& sweep,
                              const SweepParams& in, int length) {
    const SweepParams p = sanitize(in);
    length = std::max(0, length);
    std::vector<float> out(static_cast<size_t>(length), 0.0f);
    if (recording.empty() || sweep.empty() || length == 0) return out;
    const int n = nextPow2(static_cast<int>(recording.size() + sweep.size()));
    std::vector<cd> X(static_cast<size_t>(n)), Y(static_cast<size_t>(n));
    for (size_t i = 0; i < sweep.size(); ++i) X[i] = std::isfinite(sweep[i]) ? sweep[i] : 0.0f;
    for (size_t i = 0; i < recording.size(); ++i) Y[i] = std::isfinite(recording[i]) ? recording[i] : 0.0f;
    fft(X, false);
    fft(Y, false);
    double maxMag = 0.0;
    for (const auto& x : X) maxMag = std::max(maxMag, std::norm(x));
    if (!(maxMag > 0.0)) return out;
    const double eps = 1.0e-4 * maxMag;
    // Smooth band limit: half an octave cosine taper outside f1..f2.
    const double df = p.sampleRate / n;
    auto band = [&](double f) {
        const double lo = p.f1, hi = p.f2;
        if (f < lo) {
            const double o = std::log2(lo / std::max(f, 1.0e-3));
            return o >= 0.5 ? 0.0 : 0.5 + 0.5 * std::cos(kPiD * o / 0.5);
        }
        if (f > hi) {
            const double o = std::log2(f / hi);
            return o >= 0.5 ? 0.0 : 0.5 + 0.5 * std::cos(kPiD * o / 0.5);
        }
        return 1.0;
    };
    for (int k = 0; k < n; ++k) {
        const int kk = k <= n / 2 ? k : n - k;
        const double w = band(kk * df);
        const cd x = X[static_cast<size_t>(k)];
        Y[static_cast<size_t>(k)] = w == 0.0 ? cd(0.0, 0.0) : Y[static_cast<size_t>(k)] * std::conj(x) * (w / (std::norm(x) + eps));
    }
    fft(Y, true);
    for (int i = 0; i < length && i < n; ++i) {
        const double v = Y[static_cast<size_t>(i)].real();
        out[static_cast<size_t>(i)] = std::isfinite(v) ? static_cast<float>(v) : 0.0f;
    }
    return out;
}

Arrival findArrival(const std::vector<float>& ir, double sampleRate, double maxSeconds) {
    Arrival a;
    if (!(sampleRate >= 1000.0) || !finite(sampleRate) || ir.size() < 64) return a;
    const int total = static_cast<int>(std::min<size_t>(ir.size(), 1u << 22));
    // Envelope via the analytic signal (Hilbert transform in the frequency domain).
    const int n = nextPow2(total);
    std::vector<cd> s(static_cast<size_t>(n));
    for (int i = 0; i < total; ++i) s[static_cast<size_t>(i)] = std::isfinite(ir[static_cast<size_t>(i)]) ? ir[static_cast<size_t>(i)] : 0.0f;
    fft(s, false);
    for (int k = 1; k < n / 2; ++k) s[static_cast<size_t>(k)] *= 2.0;
    for (int k = n / 2 + 1; k < n; ++k) s[static_cast<size_t>(k)] = 0.0;
    fft(s, true);
    std::vector<double> env(static_cast<size_t>(total));
    for (int i = 0; i < total; ++i) env[static_cast<size_t>(i)] = std::abs(s[static_cast<size_t>(i)]);

    const int win = std::clamp(static_cast<int>((finite(maxSeconds) ? maxSeconds : 1.0) * sampleRate), 16, total);
    // Noise floor: the end of the search window, where the room has decayed but the recording
    // still fully overlaps the sweep (later samples would underestimate the noise).
    int n0 = static_cast<int>(0.75 * win), n1 = static_cast<int>(0.98 * win);
    if (n1 - n0 < 32) { n0 = 0; n1 = win; }
    double noise = 0.0;
    for (int i = n0; i < n1; ++i) noise += env[static_cast<size_t>(i)] * env[static_cast<size_t>(i)];
    noise = std::sqrt(noise / std::max(1, n1 - n0));

    int peakIdx = 0;
    for (int i = 1; i < win; ++i)
        if (env[static_cast<size_t>(i)] > env[static_cast<size_t>(peakIdx)]) peakIdx = i;
    const double peak = env[static_cast<size_t>(peakIdx)];
    if (!(peak > 0.0) || !finite(peak)) return a;
    const double thr = std::max(0.2 * peak, 6.0 * noise);
    int first = peakIdx;
    for (int i = 0; i <= peakIdx; ++i)
        if (env[static_cast<size_t>(i)] >= thr) { first = i; break; }
    // The direct sound is the strongest envelope sample shortly (0.5 ms) after the threshold
    // crossing; a small ripple before the main peak must not end the search.
    int k = first;
    const int reach = std::min(win - 1, first + std::max(2, static_cast<int>(0.0005 * sampleRate)));
    for (int i = first; i <= reach; ++i)
        if (env[static_cast<size_t>(i)] > env[static_cast<size_t>(k)]) k = i;
    double frac = 0.0;
    if (k > 0 && k + 1 < total) {
        const double y0 = env[static_cast<size_t>(k - 1)], y1 = env[static_cast<size_t>(k)], y2 = env[static_cast<size_t>(k + 1)];
        const double den = y0 - 2.0 * y1 + y2;
        if (std::fabs(den) > 1.0e-30) frac = std::clamp(0.5 * (y0 - y2) / den, -0.5, 0.5);
    }
    a.sample = k;
    a.seconds = (k + frac) / sampleRate;
    a.directness = static_cast<float>(env[static_cast<size_t>(k)] / peak);
    a.snrDb = noise > 0.0 ? static_cast<float>(20.0 * std::log10(env[static_cast<size_t>(k)] / noise)) : 120.0f;
    const int e0 = std::max(0, k - static_cast<int>(0.0005 * sampleRate));
    const int e1 = std::min(total, k + static_cast<int>(0.005 * sampleRate));
    double e = 0.0;
    for (int i = e0; i < e1; ++i) e += static_cast<double>(ir[static_cast<size_t>(i)]) * ir[static_cast<size_t>(i)];
    a.levelDb = e > 1.0e-30 ? static_cast<float>(10.0 * std::log10(e)) : -160.0f;
    a.valid = finite(a.seconds) && a.snrDb >= 15.0f && k > 0 && k < win - 1;
    return a;
}

Measurement analyse(const std::vector<float>& recording, const std::vector<float>& sweep, const SweepParams& in) {
    const SweepParams p = sanitize(in);
    Measurement m;
    float peak = 0.0f;
    for (float v : recording)
        if (std::isfinite(v)) peak = std::max(peak, std::fabs(v));
    m.clipped = peak > 0.99f;
    const int len = std::max(64, static_cast<int>(recording.size()));
    const auto ir = deconvolve(recording, sweep, p, len);
    m.arrival = findArrival(ir, p.sampleRate, p.tailSeconds);
    m.tooQuiet = !m.arrival.valid;
    return m;
}

// ---------------------------------------------------------------------------------------------
// Geometry: robust multilateration
//
// Parameters: every speaker position (3 each), every measuring point (3 each) and the latency.
// The normal equations have an "arrow" structure (speakers only couple to the shared
// parameters), so they are solved with the Schur complement: cheap even for 128 speakers.

namespace {

struct Mat3 {
    double m[3][3]{};
};

bool invert3(const Mat3& a, Mat3& out) {
    const double (&m)[3][3] = a.m;
    const double c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
    const double c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
    const double c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    const double det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
    if (!(std::fabs(det) > 1.0e-300) || !finite(det)) return false;
    const double id = 1.0 / det;
    out.m[0][0] = c00 * id;
    out.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * id;
    out.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * id;
    out.m[1][0] = c01 * id;
    out.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * id;
    out.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * id;
    out.m[2][0] = c02 * id;
    out.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * id;
    out.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * id;
    return true;
}

/// Dense Cholesky solve of a symmetric positive definite matrix (row-major, n x n). In place.
bool cholesky(std::vector<double>& a, int n) {
    for (int j = 0; j < n; ++j) {
        double d = a[static_cast<size_t>(j * n + j)];
        for (int k = 0; k < j; ++k) d -= a[static_cast<size_t>(j * n + k)] * a[static_cast<size_t>(j * n + k)];
        if (!(d > 1.0e-300) || !finite(d)) return false;
        d = std::sqrt(d);
        a[static_cast<size_t>(j * n + j)] = d;
        for (int i = j + 1; i < n; ++i) {
            double s = a[static_cast<size_t>(i * n + j)];
            for (int k = 0; k < j; ++k) s -= a[static_cast<size_t>(i * n + k)] * a[static_cast<size_t>(j * n + k)];
            a[static_cast<size_t>(i * n + j)] = s / d;
        }
    }
    return true;
}

void choleskySolve(const std::vector<double>& l, int n, std::vector<double>& b) {
    for (int i = 0; i < n; ++i) {
        double s = b[static_cast<size_t>(i)];
        for (int k = 0; k < i; ++k) s -= l[static_cast<size_t>(i * n + k)] * b[static_cast<size_t>(k)];
        b[static_cast<size_t>(i)] = s / l[static_cast<size_t>(i * n + i)];
    }
    for (int i = n - 1; i >= 0; --i) {
        double s = b[static_cast<size_t>(i)];
        for (int k = i + 1; k < n; ++k) s -= l[static_cast<size_t>(k * n + i)] * b[static_cast<size_t>(k)];
        b[static_cast<size_t>(i)] = s / l[static_cast<size_t>(i * n + i)];
    }
}

struct D3 {
    double x = 0, y = 0, z = 0;
};
D3 d3(Vec3 v) { return {v.x, v.y, v.z}; }
Vec3 v3(const D3& d) { return {static_cast<float>(d.x), static_cast<float>(d.y), static_cast<float>(d.z)}; }

double huberRho(double r, double k) {
    const double a = std::fabs(r);
    return a <= k ? 0.5 * r * r : k * a - 0.5 * k * k;
}
double huberW(double r, double k) {
    const double a = std::fabs(r);
    return a <= k ? 1.0 : k / a;
}

class Problem {
public:
    Problem(const SolveInput& in, int numSpeakers, int numPoints)
        : in_(in), ns_(numSpeakers), np_(numPoints), ng_(3 * numPoints + 1) {}

    const SolveInput& in_;
    std::vector<Observation> obs_;
    int ns_, np_ = 0, ng_ = 0;
    std::vector<D3> s, m; // current speakers and points
    double tau = 0.0;
    double huberK = std::numeric_limits<double>::infinity();
    double tauPrior = 0.0, tauSigma = 0.02;

    double obsResidual(const Observation& o, const std::vector<D3>& S, const std::vector<D3>& M, double t,
                       D3* grad = nullptr) const {
        const D3& a = S[static_cast<size_t>(o.speaker)];
        const D3& b = M[static_cast<size_t>(o.point)];
        const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double c = in_.speedOfSound;
        const double sig = c * in_.timeSigma;
        const double sw = std::sqrt(std::clamp(static_cast<double>(o.weight), 0.0, 1.0));
        if (grad != nullptr) {
            if (d > 1.0e-9) *grad = {dx / d / sig * sw, dy / d / sig * sw, dz / d / sig * sw};
            else *grad = {};
        }
        return (d - c * (o.seconds - t)) / sig * sw;
    }

    double cost(const std::vector<D3>& S, const std::vector<D3>& M, double t) const {
        double c = 0.0;
        for (const auto& o : obs_) c += huberRho(obsResidual(o, S, M, t), huberK);
        for (const auto& h : in_.distances) {
            const D3& a = S[static_cast<size_t>(h.speaker)];
            const D3& b = M[static_cast<size_t>(h.point)];
            const double d = std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
            const double r = (d - h.metres) / 0.01;
            c += 0.5 * r * r;
        }
        auto prior = [](const D3& v, Vec3 p0, double sig) {
            const double ax = (v.x - p0.x) / sig, ay = (v.y - p0.y) / sig, az = (v.z - p0.z) / sig;
            return 0.5 * (ax * ax + ay * ay + az * az);
        };
        for (int i = 0; i < ns_; ++i) c += prior(S[static_cast<size_t>(i)], in_.speakerGuess[static_cast<size_t>(i)], in_.speakerSigma);
        for (int j = 0; j < np_; ++j) c += prior(M[static_cast<size_t>(j)], in_.pointGuess[static_cast<size_t>(j)], in_.pointSigma);
        const double rt = (t - tauPrior) / tauSigma;
        c += 0.5 * rt * rt;
        return c;
    }

    /// Normal equations: per-speaker 3x3 blocks A, couplings B (3 x ng), shared block G, gradients.
    void build(std::vector<Mat3>& A, std::vector<double>& B, std::vector<double>& G, std::vector<double>& gs,
               std::vector<double>& gg) const {
        A.assign(static_cast<size_t>(ns_), Mat3{});
        B.assign(static_cast<size_t>(ns_) * 3 * ng_, 0.0);
        G.assign(static_cast<size_t>(ng_) * ng_, 0.0);
        gs.assign(static_cast<size_t>(ns_) * 3, 0.0);
        gg.assign(static_cast<size_t>(ng_), 0.0);
        const int tauIdx = ng_ - 1;
        auto addTerm = [&](int sp, const double js[3], int pt, const double jm[3], double jt, double r, double w) {
            // Residual r with Jacobian js (speaker), jm (point), jt (latency), IRLS weight w.
            for (int a = 0; a < 3; ++a) {
                gs[static_cast<size_t>(sp * 3 + a)] += w * js[a] * r;
                for (int b = 0; b < 3; ++b) A[static_cast<size_t>(sp)].m[a][b] += w * js[a] * js[b];
            }
            int gi[4];
            double gj[4];
            int ngj = 0;
            if (pt >= 0)
                for (int a = 0; a < 3; ++a) { gi[ngj] = pt * 3 + a; gj[ngj++] = jm[a]; }
            if (jt != 0.0) { gi[ngj] = tauIdx; gj[ngj++] = jt; }
            for (int u = 0; u < ngj; ++u) {
                gg[static_cast<size_t>(gi[u])] += w * gj[u] * r;
                for (int v = 0; v < ngj; ++v) G[static_cast<size_t>(gi[u] * ng_ + gi[v])] += w * gj[u] * gj[v];
                for (int a = 0; a < 3; ++a) B[(static_cast<size_t>(sp) * 3 + a) * ng_ + gi[u]] += w * js[a] * gj[u];
            }
        };
        for (const auto& o : obs_) {
            D3 g;
            const double r = obsResidual(o, s, m, tau, &g);
            const double js[3] = {g.x, g.y, g.z};
            const double jm[3] = {-g.x, -g.y, -g.z};
            const double sw = std::sqrt(std::clamp(static_cast<double>(o.weight), 0.0, 1.0));
            const double jt = in_.speedOfSound / (in_.speedOfSound * in_.timeSigma) * sw;
            addTerm(o.speaker, js, o.point, jm, jt, r, huberW(r, huberK));
        }
        for (const auto& h : in_.distances) {
            const D3& a = s[static_cast<size_t>(h.speaker)];
            const D3& b = m[static_cast<size_t>(h.point)];
            const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d < 1.0e-9) continue;
            const double js[3] = {dx / d / 0.01, dy / d / 0.01, dz / d / 0.01};
            const double jm[3] = {-js[0], -js[1], -js[2]};
            addTerm(h.speaker, js, h.point, jm, 0.0, (d - h.metres) / 0.01, 1.0);
        }
        for (int i = 0; i < ns_; ++i) {
            const double sig = in_.speakerSigma;
            const Vec3 p0 = in_.speakerGuess[static_cast<size_t>(i)];
            const double r[3] = {(s[static_cast<size_t>(i)].x - p0.x) / sig, (s[static_cast<size_t>(i)].y - p0.y) / sig,
                                 (s[static_cast<size_t>(i)].z - p0.z) / sig};
            for (int a = 0; a < 3; ++a) {
                A[static_cast<size_t>(i)].m[a][a] += 1.0 / (sig * sig);
                gs[static_cast<size_t>(i * 3 + a)] += r[a] / sig;
            }
        }
        for (int j = 0; j < np_; ++j) {
            const double sig = in_.pointSigma;
            const Vec3 p0 = in_.pointGuess[static_cast<size_t>(j)];
            const double r[3] = {(m[static_cast<size_t>(j)].x - p0.x) / sig, (m[static_cast<size_t>(j)].y - p0.y) / sig,
                                 (m[static_cast<size_t>(j)].z - p0.z) / sig};
            for (int a = 0; a < 3; ++a) {
                G[static_cast<size_t>((j * 3 + a) * ng_ + j * 3 + a)] += 1.0 / (sig * sig);
                gg[static_cast<size_t>(j * 3 + a)] += r[a] / sig;
            }
        }
        G[static_cast<size_t>(tauIdx * ng_ + tauIdx)] += 1.0 / (tauSigma * tauSigma);
        gg[static_cast<size_t>(tauIdx)] += (tau - tauPrior) / tauSigma / tauSigma;
    }

    /// Solves the damped system. Returns false if it is singular.
    bool step(double lambda, std::vector<D3>& ds, std::vector<double>& dg) const {
        std::vector<Mat3> A;
        std::vector<double> B, G, gs, gg;
        build(A, B, G, gs, gg);
        return solveSystem(A, B, G, gs, gg, lambda, ds, dg, nullptr);
    }

    /// Schur complement solve. If `covOut` is set, it receives the 3x3 position covariance per
    /// speaker relative to the measuring points (undamped, lambda should be 0).
    bool solveSystem(std::vector<Mat3>& A, const std::vector<double>& B, std::vector<double> G, const std::vector<double>& gs,
                     const std::vector<double>& gg, double lambda, std::vector<D3>& ds, std::vector<double>& dg,
                     std::vector<Mat3>* covOut) const {
        std::vector<Mat3> Ainv(static_cast<size_t>(ns_));
        for (int i = 0; i < ns_; ++i) {
            Mat3 a = A[static_cast<size_t>(i)];
            for (int k = 0; k < 3; ++k) a.m[k][k] += lambda * a.m[k][k] + 1.0e-12;
            if (!invert3(a, Ainv[static_cast<size_t>(i)])) return false;
        }
        for (int k = 0; k < ng_; ++k) G[static_cast<size_t>(k * ng_ + k)] += lambda * G[static_cast<size_t>(k * ng_ + k)] + 1.0e-12;
        std::vector<double> S = G;
        std::vector<double> rhs(static_cast<size_t>(ng_));
        for (int k = 0; k < ng_; ++k) rhs[static_cast<size_t>(k)] = -gg[static_cast<size_t>(k)];
        std::vector<double> AiB(static_cast<size_t>(3 * ng_)); // Ainv_i * B_i (3 x ng)
        for (int i = 0; i < ns_; ++i) {
            const Mat3& ai = Ainv[static_cast<size_t>(i)];
            const double* bi = &B[static_cast<size_t>(i) * 3 * ng_];
            bool any = false;
            for (int k = 0; k < 3 * ng_ && !any; ++k) any = bi[k] != 0.0;
            if (!any) continue;
            for (int a = 0; a < 3; ++a)
                for (int k = 0; k < ng_; ++k)
                    AiB[static_cast<size_t>(a * ng_ + k)] = ai.m[a][0] * bi[k] + ai.m[a][1] * bi[ng_ + k] + ai.m[a][2] * bi[2 * ng_ + k];
            for (int u = 0; u < ng_; ++u) {
                for (int v = 0; v < ng_; ++v) {
                    double sum = 0.0;
                    for (int a = 0; a < 3; ++a) sum += bi[a * ng_ + u] * AiB[static_cast<size_t>(a * ng_ + v)];
                    S[static_cast<size_t>(u * ng_ + v)] -= sum;
                }
                // rhs += B^T Ainv gs
                double sum = 0.0;
                for (int a = 0; a < 3; ++a) sum += AiB[static_cast<size_t>(a * ng_ + u)] * gs[static_cast<size_t>(i * 3 + a)];
                rhs[static_cast<size_t>(u)] += sum;
            }
        }
        std::vector<double> L = S;
        if (!cholesky(L, ng_)) return false;
        dg = rhs;
        choleskySolve(L, ng_, dg);
        ds.assign(static_cast<size_t>(ns_), D3{});
        for (int i = 0; i < ns_; ++i) {
            const double* bi = &B[static_cast<size_t>(i) * 3 * ng_];
            double t[3];
            for (int a = 0; a < 3; ++a) {
                double sum = -gs[static_cast<size_t>(i * 3 + a)];
                for (int k = 0; k < ng_; ++k) sum -= bi[a * ng_ + k] * dg[static_cast<size_t>(k)];
                t[a] = sum;
            }
            const Mat3& ai = Ainv[static_cast<size_t>(i)];
            ds[static_cast<size_t>(i)] = {ai.m[0][0] * t[0] + ai.m[0][1] * t[1] + ai.m[0][2] * t[2],
                                          ai.m[1][0] * t[0] + ai.m[1][1] * t[1] + ai.m[1][2] * t[2],
                                          ai.m[2][0] * t[0] + ai.m[2][1] * t[1] + ai.m[2][2] * t[2]};
        }
        if (covOut != nullptr) {
            // Cov_i = Ainv + Ainv B S^-1 B^T Ainv
            std::vector<double> Sinv(static_cast<size_t>(ng_) * ng_);
            for (int c = 0; c < ng_; ++c) {
                std::vector<double> e(static_cast<size_t>(ng_), 0.0);
                e[static_cast<size_t>(c)] = 1.0;
                choleskySolve(L, ng_, e);
                for (int r = 0; r < ng_; ++r) Sinv[static_cast<size_t>(r * ng_ + c)] = e[static_cast<size_t>(r)];
            }
            covOut->assign(static_cast<size_t>(ns_), Mat3{});
            for (int i = 0; i < ns_; ++i) {
                const Mat3& ai = Ainv[static_cast<size_t>(i)];
                const double* bi = &B[static_cast<size_t>(i) * 3 * ng_];
                for (int a = 0; a < 3; ++a)
                    for (int k = 0; k < ng_; ++k)
                        AiB[static_cast<size_t>(a * ng_ + k)] = ai.m[a][0] * bi[k] + ai.m[a][1] * bi[ng_ + k] + ai.m[a][2] * bi[2 * ng_ + k];
                Mat3& cv = (*covOut)[static_cast<size_t>(i)];
                for (int a = 0; a < 3; ++a)
                    for (int b = 0; b < 3; ++b) {
                        // Relative to the measuring points (a common shift or turn of the
                        // whole layout is not counted), but including the latency uncertainty.
                        const int t = ng_ - 1;
                        cv.m[a][b] = ai.m[a][b] + AiB[static_cast<size_t>(a * ng_ + t)] * Sinv[static_cast<size_t>(t * ng_ + t)] *
                                                      AiB[static_cast<size_t>(b * ng_ + t)];
                    }
            }
        }
        for (const auto& d : ds)
            if (!finite(d.x) || !finite(d.y) || !finite(d.z)) return false;
        for (double d : dg)
            if (!finite(d)) return false;
        return true;
    }

};

/// Fits one speaker with everything else fixed (used to pick a good starting point).
D3 fitSingle(const Problem& pr, int sp, D3 start, double& costOut) {
    std::vector<const Observation*> mine;
    for (const auto& o : pr.obs_)
        if (o.speaker == sp) mine.push_back(&o);
    const Vec3 p0 = pr.in_.speakerGuess[static_cast<size_t>(sp)];
    const double sigP = pr.in_.speakerSigma;
    auto costAt = [&](const D3& x) {
        std::vector<D3> S = pr.s;
        S[static_cast<size_t>(sp)] = x;
        double c = 0.0;
        for (const auto* o : mine) {
            const double r = pr.obsResidual(*o, S, pr.m, pr.tau);
            c += 0.5 * r * r;
        }
        const double ax = (x.x - p0.x) / sigP, ay = (x.y - p0.y) / sigP, az = (x.z - p0.z) / sigP;
        return c + 0.5 * (ax * ax + ay * ay + az * az);
    };
    D3 x = start;
    double cost = costAt(x), lambda = 1.0e-3;
    std::vector<D3> S = pr.s;
    for (int it = 0; it < 60; ++it) {
        S[static_cast<size_t>(sp)] = x;
        Mat3 A;
        double g[3] = {0, 0, 0};
        for (const auto* o : mine) {
            D3 jg;
            const double r = pr.obsResidual(*o, S, pr.m, pr.tau, &jg);
            const double j[3] = {jg.x, jg.y, jg.z};
            for (int a = 0; a < 3; ++a) {
                g[a] += j[a] * r;
                for (int b = 0; b < 3; ++b) A.m[a][b] += j[a] * j[b];
            }
        }
        const double rp[3] = {(x.x - p0.x) / sigP, (x.y - p0.y) / sigP, (x.z - p0.z) / sigP};
        for (int a = 0; a < 3; ++a) {
            A.m[a][a] += 1.0 / (sigP * sigP);
            g[a] += rp[a] / sigP;
        }
        for (int k = 0; k < 3; ++k) A.m[k][k] += lambda * A.m[k][k] + 1.0e-12;
        Mat3 Ai;
        if (!invert3(A, Ai)) break;
        const D3 dx{-(Ai.m[0][0] * g[0] + Ai.m[0][1] * g[1] + Ai.m[0][2] * g[2]),
                    -(Ai.m[1][0] * g[0] + Ai.m[1][1] * g[1] + Ai.m[1][2] * g[2]),
                    -(Ai.m[2][0] * g[0] + Ai.m[2][1] * g[1] + Ai.m[2][2] * g[2])};
        const D3 xn{x.x + dx.x, x.y + dx.y, x.z + dx.z};
        const double cn = costAt(xn);
        if (cn < cost) {
            const double gain = cost - cn;
            x = xn;
            cost = cn;
            lambda = std::max(1.0e-9, lambda * 0.3);
            if (gain < 1.0e-10 * (1.0 + cost)) break;
        } else {
            lambda *= 10.0;
            if (lambda > 1.0e9) break;
        }
    }
    costOut = cost;
    return x;
}

} // namespace

bool pointsSpanVolume(const std::vector<Vec3>& points, float minExtent) {
    if (points.size() < 4) return false;
    // Smallest singular direction of the centred point cloud via the covariance determinant.
    D3 c;
    for (const auto& p : points) { c.x += p.x; c.y += p.y; c.z += p.z; }
    const double n = static_cast<double>(points.size());
    c = {c.x / n, c.y / n, c.z / n};
    Mat3 cov;
    for (const auto& p : points) {
        const double v[3] = {p.x - c.x, p.y - c.y, p.z - c.z};
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b) cov.m[a][b] += v[a] * v[b] / n;
    }
    // Smallest eigenvalue by inverse iteration on (cov + tiny).
    Mat3 inv;
    Mat3 reg = cov;
    for (int k = 0; k < 3; ++k) reg.m[k][k] += 1.0e-12;
    if (!invert3(reg, inv)) return false;
    double v[3] = {0.577, 0.577, 0.577};
    double ev = 0.0;
    for (int it = 0; it < 50; ++it) {
        double w[3];
        for (int a = 0; a < 3; ++a) w[a] = inv.m[a][0] * v[0] + inv.m[a][1] * v[1] + inv.m[a][2] * v[2];
        const double len = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
        if (!(len > 0.0) || !finite(len)) return false;
        for (int a = 0; a < 3; ++a) v[a] = w[a] / len;
        ev = 1.0 / len;
    }
    // ev = variance along the thinnest direction. Extent ~ 2 standard deviations.
    return 2.0 * std::sqrt(std::max(0.0, ev)) >= minExtent;
}

SolveResult solvePositions(const SolveInput& in) {
    SolveResult res;
    const int ns = static_cast<int>(std::min<size_t>(in.speakerGuess.size(), kMaxSpeakers));
    const int np = static_cast<int>(std::min<size_t>(in.pointGuess.size(), kMaxPoints));
    res.speakers.resize(static_cast<size_t>(ns));
    for (int i = 0; i < ns; ++i) res.speakers[static_cast<size_t>(i)].position = in.speakerGuess[static_cast<size_t>(i)];
    res.points.assign(in.pointGuess.begin(), in.pointGuess.begin() + np);
    if (ns == 0 || np == 0) return res;

    SolveInput safe = in;
    safe.speakerGuess.resize(static_cast<size_t>(ns));
    safe.pointGuess.resize(static_cast<size_t>(np));
    for (auto& p : safe.speakerGuess) p = safeClamp(p, 500.0f);
    for (auto& p : safe.pointGuess) p = safeClamp(p, 500.0f);
    safe.speedOfSound = safeClamp(in.speedOfSound, 300.0f, 380.0f, 343.0f);
    safe.pointSigma = safeClamp(in.pointSigma, 0.001f, 10.0f, 0.1f);
    safe.speakerSigma = safeClamp(in.speakerSigma, 0.01f, 100.0f, 4.0f);
    safe.timeSigma = safeClamp(in.timeSigma, 1.0e-6f, 0.01f, 50.0e-6f);
    safe.latencySigma = finite(in.latencySigma) ? std::clamp(in.latencySigma, 1.0e-6, 1.0) : 0.02;
    safe.distances.clear();
    for (const auto& h : in.distances)
        if (h.speaker >= 0 && h.speaker < ns && h.point >= 0 && h.point < np && std::isfinite(h.metres) && h.metres > 0.05f &&
            h.metres < 500.0f)
            safe.distances.push_back(h);

    Problem pr(safe, ns, np);
    for (const auto& o : in.observations) {
        if (o.speaker < 0 || o.speaker >= ns || o.point < 0 || o.point >= np) continue;
        if (!finite(o.seconds) || o.seconds < -1.0 || o.seconds > 10.0) continue;
        if (!(o.weight > 0.0f) || !std::isfinite(o.weight)) continue;
        pr.obs_.push_back(o);
    }
    std::vector<int> count(static_cast<size_t>(ns), 0);
    for (const auto& o : pr.obs_) ++count[static_cast<size_t>(o.speaker)];

    pr.s.resize(static_cast<size_t>(ns));
    pr.m.resize(static_cast<size_t>(np));
    for (int i = 0; i < ns; ++i) pr.s[static_cast<size_t>(i)] = d3(safe.speakerGuess[static_cast<size_t>(i)]);
    for (int j = 0; j < np; ++j) pr.m[static_cast<size_t>(j)] = d3(safe.pointGuess[static_cast<size_t>(j)]);

    // Latency: known (loopback) or a robust first guess from the rough layout.
    if (in.knownLatency && finite(*in.knownLatency)) {
        pr.tau = std::clamp(*in.knownLatency, -1.0, 2.0);
        pr.tauPrior = pr.tau;
        pr.tauSigma = 1.0e-7;
    } else {
        std::vector<double> est;
        for (const auto& o : pr.obs_) {
            const D3& a = pr.s[static_cast<size_t>(o.speaker)];
            const D3& b = pr.m[static_cast<size_t>(o.point)];
            const double d = std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
            est.push_back(o.seconds - d / safe.speedOfSound);
        }
        pr.tau = est.empty() ? (finite(in.latencyGuess) ? in.latencyGuess : 0.0) : medianD(est);
        pr.tauPrior = pr.tau;
        pr.tauSigma = safe.latencySigma;
    }

    // Starting points per speaker: the layout guess and variants above/below/mirrored, so a
    // speaker is not trapped on the wrong side of the plane of the measuring points.
    auto initialise = [&]() {
        double meanZ = 0.0;
        for (const auto& p : pr.m) meanZ += p.z;
        meanZ /= np;
        for (int i = 0; i < ns; ++i) {
            if (count[static_cast<size_t>(i)] < 3) continue; // (4 are needed to call it solved)
            const D3 g = d3(safe.speakerGuess[static_cast<size_t>(i)]);
            const D3 cur = pr.s[static_cast<size_t>(i)];
            const D3 starts[] = {cur, g, {g.x, g.y, g.z + 1.5}, {g.x, g.y, g.z - 1.5}, {g.x, g.y, 2.0 * meanZ - g.z},
                                 {g.x * 1.5, g.y * 1.5, g.z}, {g.x * 0.6, g.y * 0.6, g.z}};
            double best = std::numeric_limits<double>::infinity();
            D3 bestX = cur;
            for (const auto& st : starts) {
                double c = 0.0;
                const D3 x = fitSingle(pr, i, st, c);
                if (c < best - 1.0e-9) { best = c; bestX = x; }
            }
            pr.s[static_cast<size_t>(i)] = bestX;
        }
    };

    // Joint Levenberg-Marquardt: first plain least squares, then robust (Huber) to tame
    // reflections that were mistaken for the direct sound.
    int iterations = 0;
    bool converged = false;
    auto joint = [&]() {
        converged = false;
        for (int phase = 0; phase < 2; ++phase) {
            pr.huberK = phase == 0 ? std::numeric_limits<double>::infinity() : 3.0;
            double lambda = 1.0e-3;
            double cost = pr.cost(pr.s, pr.m, pr.tau);
            const int maxIt = std::clamp(in.maxIterations, 1, 2000);
            for (int it = 0; it < maxIt; ++it, ++iterations) {
                std::vector<D3> ds;
                std::vector<double> dg;
                if (!pr.step(lambda, ds, dg)) {
                    lambda *= 10.0;
                    if (lambda > 1.0e12) break;
                    continue;
                }
                std::vector<D3> S = pr.s, M = pr.m;
                for (int i = 0; i < ns; ++i) {
                    S[static_cast<size_t>(i)].x += ds[static_cast<size_t>(i)].x;
                    S[static_cast<size_t>(i)].y += ds[static_cast<size_t>(i)].y;
                    S[static_cast<size_t>(i)].z += ds[static_cast<size_t>(i)].z;
                }
                for (int j = 0; j < np; ++j) {
                    M[static_cast<size_t>(j)].x += dg[static_cast<size_t>(j * 3)];
                    M[static_cast<size_t>(j)].y += dg[static_cast<size_t>(j * 3 + 1)];
                    M[static_cast<size_t>(j)].z += dg[static_cast<size_t>(j * 3 + 2)];
                }
                const double t = pr.tau + dg.back();
                const double cn = pr.cost(S, M, t);
                if (finite(cn) && cn < cost) {
                    const double gain = cost - cn;
                    pr.s = std::move(S);
                    pr.m = std::move(M);
                    pr.tau = t;
                    cost = cn;
                    lambda = std::max(1.0e-12, lambda * 0.3);
                    if (gain < 1.0e-10 * (1.0 + cost)) { converged = true; break; }
                } else {
                    lambda *= 10.0;
                    if (lambda > 1.0e12) { converged = true; break; } // no further improvement possible
                }
            }
        }
    };

    // The shared parameters (measuring points, latency) are solved from the speakers that were
    // heard from at least five points: they have measurements to spare, so a reflection that was
    // mistaken for the direct sound shows up as a misfit and is removed. Speakers heard from
    // fewer points are placed afterwards on that geometry, one by one.
    std::fill(count.begin(), count.end(), 0);
    for (const auto& o : pr.obs_) ++count[static_cast<size_t>(o.speaker)];
    std::vector<char> core(static_cast<size_t>(ns), 0);
    int numCore = 0;
    for (int i = 0; i < ns; ++i)
        if (count[static_cast<size_t>(i)] >= 5) { core[static_cast<size_t>(i)] = 1; ++numCore; }
    if (numCore < 2)
        for (int i = 0; i < ns; ++i) core[static_cast<size_t>(i)] = count[static_cast<size_t>(i)] >= 3 ? 1 : 0;
    std::vector<Observation> others;
    {
        std::vector<Observation> coreObs;
        for (const auto& o : pr.obs_) (core[static_cast<size_t>(o.speaker)] ? coreObs : others).push_back(o);
        pr.obs_ = std::move(coreObs);
    }
    auto residualM = [&](const Observation& o) {
        return pr.obsResidual(o, pr.s, pr.m, pr.tau) * safe.speedOfSound * safe.timeSigma /
               std::sqrt(std::max(1.0e-6, static_cast<double>(o.weight)));
    };
    std::vector<char> unreliable(static_cast<size_t>(ns), 0);
    double thr = 0.05;
    for (int round = 0; round < 8; ++round) {
        std::fill(count.begin(), count.end(), 0);
        for (const auto& o : pr.obs_) ++count[static_cast<size_t>(o.speaker)];
        initialise();
        joint();
        std::vector<double> resid(pr.obs_.size());
        std::vector<double> absRes;
        for (size_t k = 0; k < pr.obs_.size(); ++k) {
            resid[k] = residualM(pr.obs_[k]);
            absRes.push_back(std::fabs(resid[k]));
        }
        if (absRes.empty()) break;
        thr = std::max(0.05, 6.0 * 1.4826 * medianD(absRes));
        std::vector<int> worst(static_cast<size_t>(ns), -1);
        for (size_t k = 0; k < pr.obs_.size(); ++k) {
            const int sp = pr.obs_[k].speaker;
            int& w = worst[static_cast<size_t>(sp)];
            if (std::fabs(resid[k]) > thr && (w < 0 || std::fabs(resid[k]) > std::fabs(resid[static_cast<size_t>(w)]))) w = static_cast<int>(k);
        }
        std::vector<char> drop(pr.obs_.size(), 0);
        bool changed = false;
        for (int i = 0; i < ns; ++i) {
            const int w = worst[static_cast<size_t>(i)];
            if (w < 0) continue;
            changed = true;
            if (count[static_cast<size_t>(i)] >= 5) {
                drop[static_cast<size_t>(w)] = 1;
            } else {
                unreliable[static_cast<size_t>(i)] = 1;
                for (size_t k = 0; k < pr.obs_.size(); ++k)
                    if (pr.obs_[k].speaker == i) drop[k] = 2;
            }
        }
        if (!changed) break;
        std::vector<Observation> kept;
        for (size_t k = 0; k < pr.obs_.size(); ++k) {
            if (drop[k] == 0) kept.push_back(pr.obs_[k]);
            else if (drop[k] == 2) others.push_back(pr.obs_[k]);
        }
        pr.obs_ = std::move(kept);
    }

    // The remaining speakers, one by one on the fixed geometry. With a measurement to spare, the
    // latest arrival (reflections are always late) is dropped if the fit does not close.
    for (int i = 0; i < ns; ++i) {
        std::vector<Observation> mine;
        for (const auto& o : others)
            if (o.speaker == i) mine.push_back(o);
        if (mine.empty()) continue;
        const size_t base = pr.obs_.size();
        pr.obs_.insert(pr.obs_.end(), mine.begin(), mine.end());
        auto place = [&]() {
            double best = std::numeric_limits<double>::infinity();
            D3 bestX = pr.s[static_cast<size_t>(i)];
            const D3 g = d3(safe.speakerGuess[static_cast<size_t>(i)]);
            const D3 starts[] = {g, pr.s[static_cast<size_t>(i)], {g.x, g.y, g.z + 1.5}, {g.x, g.y, g.z - 1.5}};
            for (const auto& st : starts) {
                double c = 0.0;
                const D3 x = fitSingle(pr, i, st, c);
                if (c < best - 1.0e-9) { best = c; bestX = x; }
            }
            pr.s[static_cast<size_t>(i)] = bestX;
        };
        if (pr.obs_.size() - base >= 3) place();
        while (pr.obs_.size() - base >= 4) {
            size_t late = base;
            double worstR = 0.0;
            for (size_t k = base; k < pr.obs_.size(); ++k) {
                const double r = residualM(pr.obs_[k]);
                if (std::fabs(r) > worstR) worstR = std::fabs(r);
                if (residualM(pr.obs_[k]) < residualM(pr.obs_[late])) late = k;
            }
            if (worstR <= thr) break;
            pr.obs_.erase(pr.obs_.begin() + static_cast<long>(late));
            unreliable[static_cast<size_t>(i)] = 1;
            place();
        }
    }
    std::fill(count.begin(), count.end(), 0);
    for (const auto& o : pr.obs_) ++count[static_cast<size_t>(o.speaker)];

    // Uncertainty from the (undamped) normal equations, scaled by the fit quality.
    pr.huberK = 3.0;
    std::vector<Mat3> A, cov;
    std::vector<double> B, G, gs, gg, dg;
    std::vector<D3> ds;
    pr.build(A, B, G, gs, gg);
    const bool haveCov = pr.solveSystem(A, B, G, gs, gg, 0.0, ds, dg, &cov);

    double sumSq = 0.0;
    int nObs = 0;
    std::vector<double> spSq(static_cast<size_t>(ns), 0.0);
    for (const auto& o : pr.obs_) {
        const double r = pr.obsResidual(o, pr.s, pr.m, pr.tau) * safe.speedOfSound * safe.timeSigma /
                         std::sqrt(std::max(1.0e-6, static_cast<double>(o.weight)));
        sumSq += r * r;
        ++nObs;
        spSq[static_cast<size_t>(o.speaker)] += r * r;
    }
    res.rmsError = nObs > 0 ? static_cast<float>(std::sqrt(sumSq / nObs)) : 0.0f;
    const double sigM = safe.speedOfSound * safe.timeSigma;
    const double quality = std::max(1.0, nObs > 0 ? (sumSq / nObs) / (sigM * sigM) : 1.0);

    for (int i = 0; i < ns; ++i) {
        SpeakerFit& f = res.speakers[static_cast<size_t>(i)];
        f.position = safeClamp(v3(pr.s[static_cast<size_t>(i)]), 500.0f);
        f.points = count[static_cast<size_t>(i)];
        f.rmsError = f.points > 0 ? static_cast<float>(std::sqrt(spSq[static_cast<size_t>(i)] / f.points)) : 0.0f;
        double var = 0.0;
        if (haveCov) {
            const Mat3& c = cov[static_cast<size_t>(i)];
            var = std::max(0.0, c.m[0][0] + c.m[1][1] + c.m[2][2]) * quality;
        }
        f.uncertainty = haveCov ? static_cast<float>(std::min(100.0, std::sqrt(var))) : 100.0f;
        f.solved = f.points >= 4 && f.uncertainty < 0.3f && isFinite(f.position) && !unreliable[static_cast<size_t>(i)];
        if (!f.solved) f.position = safe.speakerGuess[static_cast<size_t>(i)];
    }
    for (int j = 0; j < np; ++j) res.points[static_cast<size_t>(j)] = safeClamp(v3(pr.m[static_cast<size_t>(j)]), 500.0f);
    res.latency = finite(pr.tau) ? pr.tau : 0.0;
    res.iterations = iterations;
    res.converged = converged;
    return res;
}

std::vector<Vec3> suggestPoints(const SpeakerLayout& layout, int count) {
    count = std::clamp(count, 1, kMaxPoints);
    std::vector<Vec3> pts;
    const LayoutInfo info = analyze(layout);
    constexpr float raise = 0.5f;
    if (info.facesInwardAndOutward) {
        // Donut: walk round the ring between the inner (outward facing) and outer rings.
        float rIn = 0.0f, rOut = 0.0f;
        int nIn = 0, nOut = 0;
        for (const auto& s : layout.speakers) {
            if (!s.enabled || s.isSubwoofer) continue;
            const Vec3 h{s.position.x, s.position.y, 0.0f};
            const float r = length(h);
            if (r < 0.05f) continue;
            const float d = dot(s.aimDirection(), normalized(h));
            if (d > 0.3f) { rIn += r; ++nIn; }
            else if (d < -0.3f) { rOut += r; ++nOut; }
        }
        rIn = nIn > 0 ? rIn / nIn : 1.0f;
        rOut = nOut > 0 ? rOut / nOut : 4.0f;
        const float rMid = 0.5f * (rIn + rOut);
        for (int k = 0; k < count; ++k) {
            const float az = 360.0f * static_cast<float>(k) / static_cast<float>(count) + 22.5f;
            Vec3 p = toCartesian({az, 0.0f, rMid});
            p.z = (k % 2 == 1) ? raise : 0.0f;
            pts.push_back(p);
        }
        return pts;
    }
    float minR = 1.0e9f;
    for (const auto& s : layout.speakers) {
        if (!s.enabled || s.isSubwoofer) continue;
        const float r = length(Vec3{s.position.x, s.position.y, 0.0f});
        if (r > 0.2f) minR = std::min(minR, r);
    }
    if (minR > 1.0e8f) minR = 2.0f;
    const float d = std::clamp(0.5f * minR, 0.4f, 2.0f);
    pts.push_back({0.0f, 0.0f, 0.0f}); // the listening position first: delays and levels refer to it
    for (int k = 1; k < count; ++k) {
        const float az = 360.0f * static_cast<float>(k - 1) / static_cast<float>(std::max(1, count - 1)) + 45.0f;
        Vec3 p = toCartesian({az, 0.0f, d});
        p.z = (k % 2 == 0) ? raise : 0.0f;
        pts.push_back(p);
    }
    if (count >= 3 && std::none_of(pts.begin(), pts.end(), [](Vec3 p) { return p.z > 0.1f; })) pts.back().z = raise;
    return pts;
}

// ---------------------------------------------------------------------------------------------
// Alignment

std::vector<SpeakerAlign> align(const AlignInput& in) {
    const int n = std::clamp(in.numSpeakers, 0, kMaxSpeakers);
    std::vector<SpeakerAlign> out(static_cast<size_t>(n));
    const float c = safeClamp(in.speedOfSound, 300.0f, 380.0f, 343.0f);
    std::vector<double> arrival(static_cast<size_t>(n), 0.0);
    auto isSub = [&](int i) { return i < static_cast<int>(in.isSubwoofer.size()) && in.isSubwoofer[static_cast<size_t>(i)]; };
    const SolveResult* geo = in.geometry;
    for (int i = 0; i < n; ++i) {
        SpeakerAlign& a = out[static_cast<size_t>(i)];
        const Observation* ref = nullptr;
        std::vector<float> l1;
        for (const auto& o : in.observations) {
            if (o.speaker != i || !(o.weight > 0.0f) || !finite(o.seconds) || !std::isfinite(o.levelDb)) continue;
            if (o.point == in.referencePoint) ref = &o;
            if (geo != nullptr && i < static_cast<int>(geo->speakers.size()) && geo->speakers[static_cast<size_t>(i)].solved &&
                o.point >= 0 && o.point < static_cast<int>(geo->points.size()) && o.weight >= 0.3f) {
                const float r = std::max(0.1f, length(geo->speakers[static_cast<size_t>(i)].position - geo->points[static_cast<size_t>(o.point)]));
                l1.push_back(o.levelDb + 20.0f * std::log10(r));
            }
        }
        if (!l1.empty()) {
            const Vec3 pos = geo->speakers[static_cast<size_t>(i)].position;
            const float d = std::max(0.1f, length(pos - in.referencePosition));
            a.measured = true;
            a.distance = d;
            a.levelDb = median(l1) - 20.0f * std::log10(d);
            arrival[static_cast<size_t>(i)] = d / c;
        } else if (ref != nullptr) {
            a.measured = true;
            a.levelDb = ref->levelDb;
            arrival[static_cast<size_t>(i)] = ref->seconds - (geo != nullptr ? geo->latency : 0.0);
            if (geo != nullptr) a.distance = static_cast<float>(std::max(0.0, arrival[static_cast<size_t>(i)] * c));
        }
    }
    double maxArr = -1.0e9;
    std::vector<float> levels;
    for (int i = 0; i < n; ++i) {
        if (!out[static_cast<size_t>(i)].measured) continue;
        maxArr = std::max(maxArr, arrival[static_cast<size_t>(i)]);
        if (!isSub(i)) levels.push_back(out[static_cast<size_t>(i)].levelDb);
    }
    const float med = median(levels);
    float target = med;
    for (float l : levels)
        if (l >= med - 10.0f) target = std::min(target, l);
    for (int i = 0; i < n; ++i) {
        SpeakerAlign& a = out[static_cast<size_t>(i)];
        if (!a.measured) continue;
        a.delayMs = safeClamp(static_cast<float>((maxArr - arrival[static_cast<size_t>(i)]) * 1000.0), 0.0f, 500.0f, 0.0f);
        if (isSub(i)) {
            a.gainDb = 0.0f;
            continue;
        }
        a.gainDb = safeClamp(target - a.levelDb, -30.0f, 6.0f, 0.0f);
        a.veryQuiet = a.levelDb < med - 10.0f;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Matching

std::vector<int> matchByDistance(const std::vector<Vec3>& speakers, const std::vector<Vec3>& measured) {
    const int n = static_cast<int>(std::min<size_t>(speakers.size(), 512));
    const int m = static_cast<int>(std::min<size_t>(measured.size(), 512));
    std::vector<int> result(static_cast<size_t>(n), -1);
    if (n == 0 || m == 0) return result;
    // Hungarian algorithm (rows <= cols). Transpose if needed.
    const bool transposed = n > m;
    const int rows = transposed ? m : n, cols = transposed ? n : m;
    auto cost = [&](int r, int c) {
        const Vec3 a = transposed ? speakers[static_cast<size_t>(c)] : speakers[static_cast<size_t>(r)];
        const Vec3 b = transposed ? measured[static_cast<size_t>(r)] : measured[static_cast<size_t>(c)];
        const double d = length(a - b);
        return finite(d) ? d : 1.0e6;
    };
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> u(static_cast<size_t>(rows + 1)), v(static_cast<size_t>(cols + 1));
    std::vector<int> p(static_cast<size_t>(cols + 1), 0), way(static_cast<size_t>(cols + 1), 0);
    for (int i = 1; i <= rows; ++i) {
        p[0] = i;
        int j0 = 0;
        std::vector<double> minv(static_cast<size_t>(cols + 1), inf);
        std::vector<char> used(static_cast<size_t>(cols + 1), 0);
        do {
            used[static_cast<size_t>(j0)] = 1;
            const int i0 = p[static_cast<size_t>(j0)];
            double delta = inf;
            int j1 = 0;
            for (int j = 1; j <= cols; ++j) {
                if (used[static_cast<size_t>(j)]) continue;
                const double cur = cost(i0 - 1, j - 1) - u[static_cast<size_t>(i0)] - v[static_cast<size_t>(j)];
                if (cur < minv[static_cast<size_t>(j)]) { minv[static_cast<size_t>(j)] = cur; way[static_cast<size_t>(j)] = j0; }
                if (minv[static_cast<size_t>(j)] < delta) { delta = minv[static_cast<size_t>(j)]; j1 = j; }
            }
            if (j1 == 0) break; // cannot happen with finite costs; guards against endless loops
            for (int j = 0; j <= cols; ++j) {
                if (used[static_cast<size_t>(j)]) {
                    u[static_cast<size_t>(p[static_cast<size_t>(j)])] += delta;
                    v[static_cast<size_t>(j)] -= delta;
                } else {
                    minv[static_cast<size_t>(j)] -= delta;
                }
            }
            j0 = j1;
        } while (p[static_cast<size_t>(j0)] != 0);
        do {
            const int j1 = way[static_cast<size_t>(j0)];
            p[static_cast<size_t>(j0)] = p[static_cast<size_t>(j1)];
            j0 = j1;
        } while (j0 != 0);
    }
    for (int j = 1; j <= cols; ++j) {
        const int r = p[static_cast<size_t>(j)];
        if (r == 0) continue;
        if (transposed) result[static_cast<size_t>(j - 1)] = r - 1;
        else result[static_cast<size_t>(r - 1)] = j - 1;
    }
    return result;
}

std::vector<std::pair<int, int>> suspectedSwaps(const std::vector<Vec3>& planned, const std::vector<Vec3>& measured,
                                                const std::vector<bool>& valid) {
    std::vector<std::pair<int, int>> out;
    const size_t n = std::min(planned.size(), measured.size());
    for (size_t i = 0; i < n; ++i) {
        if (i < valid.size() && !valid[i]) continue;
        const float own = length(measured[i] - planned[i]);
        if (!(own > 0.5f)) continue;
        int best = -1;
        float bestD = own * 0.5f;
        for (size_t j = 0; j < planned.size(); ++j) {
            if (j == i) continue;
            const float d = length(measured[i] - planned[j]);
            if (d < bestD) { bestD = d; best = static_cast<int>(j); }
        }
        if (best >= 0) out.emplace_back(static_cast<int>(i), best);
    }
    return out;
}

} // namespace rk::calib
