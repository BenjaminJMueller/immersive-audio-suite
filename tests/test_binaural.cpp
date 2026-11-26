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

// ---- measured HRIRs ----------------------------------------------------------------

#include "alloc_guard.hpp"
#include <rk/engine.hpp>

namespace {
/// Two directions: left (90 deg) and front. Left: the left ear hears an impulse after
/// 10 samples, the right ear half as loud 30 samples later. Front: both ears after 12.
HrirSet testSet(double rate = 48000.0) {
    HrirSet s;
    s.sampleRate = rate;
    s.length = 128;
    s.directions = {toCartesian({90, 0, 1}), toCartesian({0, 0, 1})};
    s.left.assign(2 * 128, 0.0f);
    s.right.assign(2 * 128, 0.0f);
    s.left[10] = 1.0f;
    s.right[40] = 0.5f;
    s.left[128 + 12] = 0.8f;
    s.right[128 + 12] = 0.8f;
    return s;
}
int peakIndex(const std::vector<float>& v) {
    int best = 0;
    for (size_t i = 0; i < v.size(); ++i)
        if (std::fabs(v[i]) > std::fabs(v[(size_t)best])) best = (int)i;
    return best;
}
} // namespace

TEST_CASE("hrir set: finalize trims the common onset, keeps ear differences and normalises") {
    HrirSet s = testSet();
    REQUIRE(s.finalize());
    CHECK(s.valid());
    // Onset was 10, 4 samples of margin stay: the left impulse moves to 4, the right to 34.
    CHECK(std::fabs(s.left[4]) > 0.0f);
    CHECK(std::fabs(s.right[34]) > 0.0f);
    CHECK(s.right[34] / s.left[4] == doctest::Approx(0.5f));
    double e = 0.0;
    for (size_t i = 0; i < s.left.size(); ++i) e += 0.5 * ((double)s.left[i] * s.left[i] + (double)s.right[i] * s.right[i]);
    CHECK(e / 2.0 == doctest::Approx(1.0).epsilon(1e-4));
    CHECK(s.nearest(toCartesian({80, 10, 3})) == 0);
    CHECK(s.nearest(toCartesian({-20, 0, 3})) == 1);

    HrirSet bad = testSet();
    bad.left[3] = NAN;
    CHECK_FALSE(bad.finalize());
    HrirSet silent = testSet();
    std::fill(silent.left.begin(), silent.left.end(), 0.0f);
    std::fill(silent.right.begin(), silent.right.end(), 0.0f);
    CHECK_FALSE(silent.finalize());
}

TEST_CASE("binaural fold with measured HRIRs: convolves exactly, with one block of latency") {
    HrirSet s = testSet();
    REQUIRE(s.finalize());
    SpeakerLayout l;
    Speaker left;
    left.position = toCartesian({90, 0, 2});
    l.speakers.push_back(left);
    BinauralFold f;
    f.prepare(l, 48000.0, &s);
    REQUIRE(f.measured());
    CHECK(f.latency() == BinauralFold::kBlock);
    const int n = 1024;
    std::vector<float> in(n, 0.0f), L(n), R(n);
    in[0] = 1.0f;
    const float* sp[1] = {in.data()};
    f.process(sp, 1, L.data(), R.data(), n);
    const int lat = f.latency();
    CHECK(peakIndex(L) == lat + 4);
    CHECK(peakIndex(R) == lat + 34);
    CHECK(L[(size_t)(lat + 4)] == doctest::Approx(s.left[4]).epsilon(1e-4));
    CHECK(R[(size_t)(lat + 34)] == doctest::Approx(s.right[34]).epsilon(1e-4));
    // Nothing else: a clean convolution, no smearing.
    double rest = 0.0;
    for (int i = 0; i < n; ++i)
        if (i != lat + 4) rest += (double)L[(size_t)i] * L[(size_t)i];
    CHECK(rest < 1.0e-8);
}

TEST_CASE("binaural fold with measured HRIRs: any block size gives the same output, without allocating") {
    HrirSet s = testSet();
    REQUIRE(s.finalize());
    SpeakerLayout l;
    for (float az : {30.0f, -30.0f, 110.0f, -110.0f, 0.0f}) {
        Speaker sp;
        sp.position = toCartesian({az, 0, 2});
        l.speakers.push_back(sp);
    }
    const int n = 3000;
    std::vector<std::vector<float>> in(5, std::vector<float>(n));
    uint32_t seed = 1;
    for (auto& ch : in)
        for (auto& x : ch) {
            seed = seed * 1664525u + 1013904223u;
            x = (float)((seed >> 9) & 0xffff) / 32768.0f - 1.0f;
        }
    auto run = [&](int block, std::vector<float>& L, std::vector<float>& R) {
        BinauralFold f;
        f.prepare(l, 48000.0, &s);
        L.assign(n, 0.0f);
        R.assign(n, 0.0f);
        rktest::AllocGuard guard;
        for (int i = 0; i < n;) {
            const int k = std::min(block, n - i);
            const float* sp[5];
            for (int m = 0; m < 5; ++m) sp[m] = in[(size_t)m].data() + i;
            f.process(sp, 5, L.data() + i, R.data() + i, k);
            i += k;
        }
        CHECK(guard.allocations() == 0);
    };
    std::vector<float> L1, R1, L2, R2, L3, R3;
    run(n, L1, R1);
    run(1, L2, R2);
    run(97, L3, R3);
    for (int i = 0; i < n; ++i) {
        CHECK(L2[(size_t)i] == doctest::Approx(L1[(size_t)i]).epsilon(1e-5));
        CHECK(R3[(size_t)i] == doctest::Approx(R1[(size_t)i]).epsilon(1e-5));
    }
}

TEST_CASE("binaural fold with measured HRIRs: other sample rates are resampled") {
    HrirSet s = testSet(44100.0);
    REQUIRE(s.finalize());
    SpeakerLayout l;
    Speaker left;
    left.position = toCartesian({90, 0, 2});
    l.speakers.push_back(left);
    BinauralFold f;
    f.prepare(l, 96000.0, &s);
    const int n = 1024;
    std::vector<float> in(n, 0.0f), L(n), R(n);
    in[0] = 1.0f;
    const float* sp[1] = {in.data()};
    f.process(sp, 1, L.data(), R.data(), n);
    // 30 samples between the ears at 44.1 kHz are about 65 at 96 kHz.
    const int gap = peakIndex(R) - peakIndex(L);
    CHECK(gap >= 64);
    CHECK(gap <= 66);
}

TEST_CASE("engine: headphones with measured HRIRs play and switch back to the model") {
    auto set = std::make_shared<HrirSet>(testSet());
    REQUIRE(set->finalize());
    Engine e;
    e.prepare(48000.0, 256);
    EngineConfig c;
    c.layout = binauralVirtualLayout();
    c.outputMode = OutputMode::Headphones;
    c.hrirs = set;
    e.setConfig(c);
    e.source(0).active = true;
    e.source(0).setPosition(toCartesian({90, 0, 2}));
    e.source(0).touch();
    std::vector<float> in(256), L(256), R(256);
    double el = 0, er = 0;
    for (int b = 0; b < 40; ++b) {
        for (int i = 0; i < 256; ++i) in[(size_t)i] = 0.3f * std::sin(0.05f * (float)(b * 256 + i));
        const float* ins[1] = {in.data()};
        float* outs[2] = {L.data(), R.data()};
        e.process(ins, 1, outs, 2, 256);
        if (b > 20)
            for (int i = 0; i < 256; ++i) {
                el += (double)L[(size_t)i] * L[(size_t)i];
                er += (double)R[(size_t)i] * R[(size_t)i];
            }
        e.collectGarbage();
    }
    CHECK(el > 0.0);
    CHECK(el > er * 2.0); // the left set: the right ear is 6 dB down
    c.hrirs.reset();
    e.setConfig(c);
    for (int b = 0; b < 10; ++b) {
        const float* ins[1] = {in.data()};
        float* outs[2] = {L.data(), R.data()};
        e.process(ins, 1, outs, 2, 256);
    }
    for (float x : L) CHECK(std::isfinite(x));
}
