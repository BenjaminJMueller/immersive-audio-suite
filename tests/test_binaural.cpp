#include <doctest/doctest.h>
#include <rk/binaural.hpp>

#include <cmath>
#include <vector>

using namespace rk;

TEST_CASE("binaural fold: left speaker is louder and earlier in the left ear") {
    SpeakerLayout l;
    Speaker left;
    left.position = toCartesian({90, 0, 2});
    l.speakers.push_back(left);
    BinauralFold f;
    f.prepare(l, 48000.0);
    const int n = 4800;
    std::vector<float> in(n, 0.0f), L(n), R(n);
    for (int i = 0; i < n; ++i) in[static_cast<size_t>(i)] = std::sin(static_cast<float>(i) * 0.2f) * 0.5f;
    const float* sp[1] = {in.data()};
    f.process(sp, 1, L.data(), R.data(), n);
    double el = 0, er = 0;
    for (int i = 0; i < n; ++i) {
        el += static_cast<double>(L[static_cast<size_t>(i)]) * L[static_cast<size_t>(i)];
        er += static_cast<double>(R[static_cast<size_t>(i)]) * R[static_cast<size_t>(i)];
    }
    CHECK(el > er * 2.0);
    // Impulse: left ear responds first.
    f.reset();
    std::fill(in.begin(), in.end(), 0.0f);
    in[0] = 1.0f;
    f.process(sp, 1, L.data(), R.data(), n);
    auto firstAbove = [](const std::vector<float>& v) {
        for (size_t i = 0; i < v.size(); ++i)
            if (std::fabs(v[i]) > 0.05f) return static_cast<int>(i);
        return -1;
    };
    CHECK(firstAbove(L) >= 0);
    CHECK(firstAbove(L) < firstAbove(R));
}

TEST_CASE("binaural fold: front speaker is symmetric") {
    SpeakerLayout l;
    Speaker c;
    c.position = {0, 2, 0};
    l.speakers.push_back(c);
    BinauralFold f;
    f.prepare(l, 48000.0);
    std::vector<float> in(1000, 0.25f), L(1000), R(1000);
    const float* sp[1] = {in.data()};
    f.process(sp, 1, L.data(), R.data(), 1000);
    for (size_t i = 0; i < 1000; ++i) CHECK(L[i] == doctest::Approx(R[i]).epsilon(1e-5));
}

namespace {
// Energy of the left ear above ~6 kHz (first difference as a crude high-pass), white noise in.
double trebleEnergy(Polar where) {
    SpeakerLayout l;
    Speaker s;
    s.position = toCartesian(where);
    l.speakers.push_back(s);
    BinauralFold f;
    f.prepare(l, 48000.0);
    const int n = 48000;
    std::vector<float> in(n), L(n), R(n);
    uint32_t seed = 1;
    for (auto& v : in) {
        seed = seed * 1664525u + 1013904223u;
        v = static_cast<float>(seed >> 8) / 16777216.0f - 0.5f;
    }
    const float* sp[1] = {in.data()};
    f.process(sp, 1, L.data(), R.data(), n);
    double e = 0;
    for (int i = 1; i < n; ++i) {
        const double d = static_cast<double>(L[static_cast<size_t>(i)]) - L[static_cast<size_t>(i - 1)];
        e += d * d;
    }
    return e;
}
} // namespace

TEST_CASE("binaural fold: front and back sound different, so do low and high") {
    const double front = trebleEnergy({30, 0, 2});
    const double back = trebleEnergy({150, 0, 2});
    CHECK(back < front * 0.8);
    const double low = trebleEnergy({30, -30, 2});
    const double high = trebleEnergy({30, 60, 2});
    CHECK(std::fabs(10.0 * std::log10(high / low)) > 1.0);
}

TEST_CASE("binaural fold: mirrored speakers give mirrored ears") {
    for (float az : {30.0f, 110.0f, 160.0f}) {
        SpeakerLayout a, b;
        Speaker s;
        s.position = toCartesian({az, 20, 2});
        a.speakers.push_back(s);
        s.position = toCartesian({-az, 20, 2});
        b.speakers.push_back(s);
        BinauralFold fa, fb;
        fa.prepare(a, 48000.0);
        fb.prepare(b, 48000.0);
        std::vector<float> in(500, 0.0f), La(500), Ra(500), Lb(500), Rb(500);
        in[0] = 1.0f;
        const float* sp[1] = {in.data()};
        fa.process(sp, 1, La.data(), Ra.data(), 500);
        fb.process(sp, 1, Lb.data(), Rb.data(), 500);
        for (size_t i = 0; i < 500; ++i) CHECK(La[i] == doctest::Approx(Rb[i]).epsilon(1e-4));
    }
}

TEST_CASE("binaural virtual layout surrounds the head") {
    const SpeakerLayout l = binauralVirtualLayout();
    CHECK(l.speakers.size() == 19);
    int below = 0, above = 0;
    for (const auto& s : l.speakers) {
        CHECK(length(s.position) == doctest::Approx(2.0f).epsilon(1e-4));
        below += s.position.z < -0.1f ? 1 : 0;
        above += s.position.z > 0.1f ? 1 : 0;
    }
    CHECK(below == 4);
    CHECK(above == 7);
}
