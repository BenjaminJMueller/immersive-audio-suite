#include "helpers.hpp"

#include <doctest/doctest.h>
#include <rk/panners.hpp>

using namespace rk;
using namespace rktest;

namespace {
std::vector<float> pan(const VbapPanner& p, int n, Vec3 pos, float spread = 0.0f) {
    std::vector<float> g(kMaxSpeakers, -1.0f);
    p.compute({pos, spread}, g.data());
    g.resize(static_cast<size_t>(n));
    return g;
}
} // namespace

TEST_CASE("VBAP: a source at a speaker plays only from that speaker") {
    for (const auto& l : allPresets()) {
        CAPTURE(l.name);
        VbapPanner p;
        p.prepare(l);
        const int n = static_cast<int>(l.speakers.size());
        for (int i = 0; i < n; ++i) {
            const Speaker& s = l.speakers[static_cast<size_t>(i)];
            if (s.isSubwoofer) continue;
            const auto g = pan(p, n, s.position);
            CAPTURE(i);
            CHECK(g[static_cast<size_t>(i)] == doctest::Approx(1.0f).epsilon(1e-3));
            CHECK(energy(g.data(), n) == doctest::Approx(1.0f).epsilon(1e-4));
        }
    }
}

TEST_CASE("VBAP: energy is preserved and gains are non-negative everywhere") {
    std::mt19937 rng(1);
    for (const auto& l : allPresets()) {
        CAPTURE(l.name);
        VbapPanner p;
        p.prepare(l);
        const int n = static_cast<int>(l.speakers.size());
        for (int k = 0; k < 2000; ++k) {
            const Vec3 d = randomDirection(rng) * 3.0f;
            const auto g = pan(p, n, d, (k % 5) * 0.25f);
            CHECK(energy(g.data(), n) == doctest::Approx(1.0f).epsilon(1e-3));
            for (int i = 0; i < n; ++i) {
                CHECK(g[static_cast<size_t>(i)] >= 0.0f);
                if (l.speakers[static_cast<size_t>(i)].isSubwoofer) CHECK(g[static_cast<size_t>(i)] == 0.0f);
            }
        }
    }
}

TEST_CASE("VBAP: stereo phantom centre") {
    VbapPanner p;
    p.prepare(presets::stereo());
    CHECK(p.is2D());
    const auto g = pan(p, 2, {0, 2, 0});
    CHECK(g[0] == doctest::Approx(0.70710678f).epsilon(1e-4));
    CHECK(g[1] == doctest::Approx(0.70710678f).epsilon(1e-4));
    // Behind the listener: outside the 60° arc, snaps to the closest speaker.
    const auto back = pan(p, 2, toCartesian({100, 0, 2}));
    CHECK(back[0] == doctest::Approx(1.0f));
}

TEST_CASE("VBAP: panning is continuous (no jumps when a source moves)") {
    std::mt19937 rng(7);
    for (const auto& l : allPresets()) {
        CAPTURE(l.name);
        VbapPanner p;
        p.prepare(l);
        const int n = static_cast<int>(l.speakers.size());
        // Walk around the listener on a few circles in tiny steps.
        for (float el : {-30.0f, 0.0f, 20.0f, 50.0f, 85.0f}) {
            auto prev = pan(p, n, toCartesian({-180.0f, el, 3.0f}));
            float maxJump = 0.0f;
            for (float az = -179.75f; az <= 180.0f; az += 0.25f) {
                const auto g = pan(p, n, toCartesian({az, el, 3.0f}));
                for (int i = 0; i < n; ++i) maxJump = std::max(maxJump, std::fabs(g[static_cast<size_t>(i)] - prev[static_cast<size_t>(i)]));
                prev = g;
            }
            CAPTURE(el);
            // Stereo snaps to the nearest speaker behind the listener; everything else is smooth.
            if (l.name != "Stereo") CHECK(maxJump < 0.12f);
        }
    }
}

TEST_CASE("VBAP: dome uses imaginary speaker below and stays audible") {
    const SpeakerLayout dome = presets::dome({8, 8, 4}, 3.0f, true);
    VbapPanner p;
    p.prepare(dome);
    CHECK_FALSE(p.is2D());
    CHECK(p.numTriangles() >= 2 * 21 - 4);
    const int n = static_cast<int>(dome.speakers.size());
    const auto below = pan(p, n, {0, 0, -3});
    CHECK(energy(below.data(), n) == doctest::Approx(1.0f).epsilon(1e-3));
    // Straight below: all ear-level speakers share the sound.
    int used = 0;
    for (int i = 0; i < 8; ++i) used += below[static_cast<size_t>(i)] > 0.05f;
    CHECK(used >= 6);
}

TEST_CASE("VBAP: cube triangulation covers the sphere") {
    VbapPanner p;
    p.prepare(presets::cube());
    CHECK(p.numTriangles() == 12);
}

TEST_CASE("VBAP: degenerate layouts never crash") {
    VbapPanner p;
    SpeakerLayout empty;
    p.prepare(empty);
    float g[kMaxSpeakers];
    p.compute({{0, 1, 0}, 0}, g);

    SpeakerLayout one;
    one.speakers.push_back(Speaker{});
    p.prepare(one);
    p.compute({{3, -1, 2}, 0.5f}, g);
    CHECK(g[0] == doctest::Approx(1.0f));

    SpeakerLayout same; // all speakers in the same direction
    for (int i = 0; i < 4; ++i) {
        Speaker s;
        s.position = {0, 1.0f + static_cast<float>(i), 0};
        same.speakers.push_back(s);
    }
    p.prepare(same);
    p.compute({{1, 1, 0}, 0}, g);
    CHECK(energy(g, 4) == doctest::Approx(1.0f).epsilon(1e-4));
    for (int i = 0; i < 4; ++i) CHECK(g[i] == doctest::Approx(0.5f).epsilon(1e-3));

    SpeakerLayout disabled = presets::quad();
    for (auto& s : disabled.speakers) s.enabled = false;
    p.prepare(disabled);
    p.compute({{0, 1, 0}, 0}, g);
    CHECK(energy(g, 4) == 0.0f);

    // Source exactly at the listener, NaN position.
    p.prepare(presets::quad());
    p.compute({{0, 0, 0}, 0}, g);
    CHECK(energy(g, 4) == doctest::Approx(1.0f).epsilon(1e-4));
    p.compute({{NAN, INFINITY, 0}, NAN}, g);
    CHECK(energy(g, 4) == doctest::Approx(1.0f).epsilon(1e-4));
}

TEST_CASE("VBAP: random layouts (fuzz)") {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> count(1, 40);
    std::uniform_real_distribution<float> coord(-6.0f, 6.0f);
    for (int iter = 0; iter < 300; ++iter) {
        SpeakerLayout l;
        const int n = count(rng);
        for (int i = 0; i < n; ++i) {
            Speaker s;
            s.position = {coord(rng), coord(rng), (iter % 3 == 0) ? 0.0f : coord(rng)};
            s.enabled = (rng() % 10) != 0;
            s.isSubwoofer = (rng() % 15) == 0;
            l.speakers.push_back(s);
        }
        VbapPanner p;
        p.prepare(l);
        float g[kMaxSpeakers];
        for (int k = 0; k < 50; ++k) {
            p.compute({randomDirection(rng) * 2.0f, (k % 3) * 0.3f}, g);
            const float e = energy(g, n);
            CHECK((e == doctest::Approx(1.0f).epsilon(1e-3) || e == 0.0f));
            for (int i = 0; i < n; ++i) CHECK(std::isfinite(g[i]));
        }
    }
}
