#include "rk/room_sim.hpp"

#include <algorithm>
#include <cmath>

namespace rk {

namespace {

void fractionalTaps(double delay, int& base, std::array<float, RoomSimulator::kTaps>& taps, float gain) {
    constexpr int half = RoomSimulator::kTaps / 2;
    const int whole = static_cast<int>(std::floor(delay));
    const double frac = delay - whole;
    base = whole - half + 1;
    for (int k = 0; k < RoomSimulator::kTaps; ++k) {
        const double x = static_cast<double>(k - half + 1) - frac;
        const double sinc = std::fabs(x) < 1.0e-9 ? 1.0 : std::sin(3.14159265358979323846 * x) / (3.14159265358979323846 * x);
        const double w = 0.5 + 0.5 * std::cos(3.14159265358979323846 * x / (half + 0.5));
        taps[static_cast<size_t>(k)] = static_cast<float>(sinc * w * gain);
    }
}

} // namespace

void RoomSimulator::prepare(double sampleRate, const std::vector<Speaker>& speakers, double latencySeconds, float floorZ,
                            float noiseRms, float speedOfSound) {
    sr_ = std::isfinite(sampleRate) ? std::clamp(sampleRate, 8000.0, 384000.0) : 48000.0;
    latency_ = std::isfinite(latencySeconds) ? std::clamp(latencySeconds, 0.0, 0.25) : 0.0;
    floorZ_ = safeClamp(floorZ, -10.0f, 0.0f, -1.2f);
    noise_ = safeClamp(noiseRms, 0.0f, 1.0f, 0.0f);
    c_ = safeClamp(speedOfSound, 300.0f, 380.0f, 343.0f);
    speakers_ = speakers;
    if (speakers_.size() > 128) speakers_.resize(128);
    for (auto& s : speakers_) {
        s.position = safeClamp(s.position, 100.0f);
        s.aim = length(s.aim) > 1.0e-6f ? normalized(s.aim) : Vec3{};
    }
    history_.assign(speakers_.size() * kHistory, 0.0f);
    paths_.assign(speakers_.size() * 2, Path{});
    write_ = 0;
    seenVersion_ = -1;
}

void RoomSimulator::setMicrophone(Vec3 p) noexcept {
    p = safeClamp(p, 100.0f);
    mx_.store(p.x, std::memory_order_relaxed);
    my_.store(p.y, std::memory_order_relaxed);
    mz_.store(p.z, std::memory_order_relaxed);
    micVersion_.fetch_add(1, std::memory_order_release);
}

void RoomSimulator::updateTaps() noexcept {
    const Vec3 mic{mx_.load(std::memory_order_relaxed), my_.load(std::memory_order_relaxed), mz_.load(std::memory_order_relaxed)};
    const double maxDelay = kHistory - kTaps - 2;
    for (size_t i = 0; i < speakers_.size(); ++i) {
        const Speaker& s = speakers_[i];
        const Vec3 mirrored{s.position.x, s.position.y, 2.0f * floorZ_ - s.position.z};
        for (int k = 0; k < 2; ++k) {
            const Vec3 src = k == 0 ? s.position : mirrored;
            const Vec3 d = mic - src;
            const float r = std::max(0.1f, length(d));
            float dir = 1.0f;
            if (length(s.aim) > 0.5f) {
                // Cardioid-like: facing away is ~14 dB quieter.
                const Vec3 out = k == 0 ? s.aim : Vec3{s.aim.x, s.aim.y, -s.aim.z};
                dir = 0.2f + 0.8f * (0.5f + 0.5f * dot(out, d * (1.0f / r)));
            }
            const float g = dir * (k == 0 ? 1.0f : 0.5f) / r;
            const double delay = std::min(maxDelay, (latency_ + r / c_) * sr_);
            fractionalTaps(delay, paths_[i * 2 + static_cast<size_t>(k)].base, paths_[i * 2 + static_cast<size_t>(k)].taps, g);
        }
    }
}

void RoomSimulator::process(const float* const* sig, int numSpeakers, float* mic, int n) noexcept {
    const int v = micVersion_.load(std::memory_order_acquire);
    if (v != seenVersion_) {
        seenVersion_ = v;
        updateTaps();
    }
    const int ns = std::min(numSpeakers, static_cast<int>(speakers_.size()));
    for (int t = 0; t < n; ++t) {
        for (int i = 0; i < static_cast<int>(speakers_.size()); ++i) {
            const float x = (i < ns && sig != nullptr && sig[i] != nullptr && std::isfinite(sig[i][t])) ? sig[i][t] : 0.0f;
            history_[static_cast<size_t>(i) * kHistory + static_cast<size_t>(write_)] = x;
        }
        float y = 0.0f;
        for (size_t p = 0; p < paths_.size(); ++p) {
            const float* h = &history_[(p / 2) * kHistory];
            const Path& path = paths_[p];
            for (int k = 0; k < kTaps; ++k) {
                const int idx = (write_ - path.base - k) & (kHistory - 1);
                y += h[idx] * path.taps[static_cast<size_t>(k)];
            }
        }
        // Noise: uniform, scaled to the requested RMS.
        rng_ = rng_ * 1664525u + 1013904223u;
        const float u = static_cast<float>(rng_ >> 8) * (1.0f / 16777216.0f) - 0.5f;
        y += u * noise_ * 3.4641f;
        if (mic != nullptr) mic[t] = y;
        write_ = (write_ + 1) & (kHistory - 1);
    }
}

} // namespace rk
