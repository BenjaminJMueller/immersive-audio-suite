#pragma once

#include <rk/layout.hpp>

#include <cmath>
#include <random>
#include <vector>

namespace rktest {

inline float energy(const float* g, int n) {
    float s = 0.0f;
    for (int i = 0; i < n; ++i) s += g[i] * g[i];
    return s;
}

inline int argmax(const float* g, int n) {
    int best = 0;
    for (int i = 1; i < n; ++i)
        if (g[i] > g[best]) best = i;
    return best;
}

inline rk::Vec3 randomDirection(std::mt19937& rng) {
    std::normal_distribution<float> nd(0.0f, 1.0f);
    for (;;) {
        rk::Vec3 v{nd(rng), nd(rng), nd(rng)};
        const float l = rk::length(v);
        if (l > 1.0e-3f) return v * (1.0f / l);
    }
}

inline std::vector<rk::SpeakerLayout> allPresets() {
    std::vector<rk::SpeakerLayout> v;
    for (const auto& p : rk::presets::all()) v.push_back(p.make());
    return v;
}

} // namespace rktest
