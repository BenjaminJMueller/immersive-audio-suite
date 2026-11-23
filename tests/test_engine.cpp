#include "alloc_guard.hpp"
#include "engine_fixture.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <random>

using namespace rk;
using namespace rktest;

TEST_CASE("engine: silence in, silence out") {
    Rig r(4, 8);
    r.setLayout(presets::ring(8, 3.0f), PannerKind::VBAP);
    for (int s = 0; s < 4; ++s) r.engine.source(s).active = true;
    r.settle();
    r.run(1024);
    for (int c = 0; c < 8; ++c) CHECK(r.rms(c, 1024) == 0.0);
}

TEST_CASE("engine: unprepared engine outputs silence and does not crash") {
    Engine e;
    std::vector<float> buf(64, 1.0f);
    float* outs[1] = {buf.data()};
    e.process(nullptr, 0, outs, 1, 64);
    for (float v : buf) CHECK(v == 0.0f);
    e.process(nullptr, 0, nullptr, 0, 64);
    e.process(nullptr, 0, outs, 1, 0);
    e.process(nullptr, 0, outs, 1, -5);
}

TEST_CASE("engine: stereo VBAP places a source correctly") {
    Rig r(1, 2);
    r.noReverb();
    r.setLayout(presets::stereo(), PannerKind::VBAP);
    auto& s = r.engine.source(0);
    s.active = true;
    s.setPosition({0, 2, 0});
    long long ph = 0;
    for (int i = 0; i < 20; ++i) { fillSine(r.in[0], 440, 0.5f, 48000, ph); r.run(512); r.engine.collectGarbage(); }
    fillSine(r.in[0], 440, 0.5f, 48000, ph);
    r.run(4096);
    CHECK(r.rms(0, 4096) == doctest::Approx(r.rms(1, 4096)).epsilon(1e-3));
    CHECK(r.rms(0, 4096) == doctest::Approx(0.5 / std::sqrt(2.0) / std::sqrt(2.0)).epsilon(0.02));

    s.setPosition(presets::stereo().speakers[0].position); // hard left
    for (int i = 0; i < 4; ++i) { fillSine(r.in[0], 440, 0.5f, 48000, ph); r.run(4096); }
    CHECK(r.rms(0, 4096) > 0.3);
    CHECK(r.rms(1, 4096) < 1e-6);
}

TEST_CASE("engine: mute, solo and gain") {
    Rig r(2, 2);
    r.noReverb();
    r.setLayout(presets::stereo(), PannerKind::VBAP);
    for (int i = 0; i < 2; ++i) r.engine.source(i).active = true;
    r.engine.source(0).setPosition(presets::stereo().speakers[0].position);
    r.engine.source(1).setPosition(presets::stereo().speakers[1].position);
    std::fill(r.in[0].begin(), r.in[0].end(), 0.25f);
    std::fill(r.in[1].begin(), r.in[1].end(), 0.25f);
    r.settle();
    r.run(1024);
    CHECK(r.rms(0, 1024) == doctest::Approx(0.25).epsilon(1e-3));
    CHECK(r.rms(1, 1024) == doctest::Approx(0.25).epsilon(1e-3));
    r.engine.source(1).solo = true;
    r.settle();
    r.run(1024);
    CHECK(r.rms(0, 1024) < 1e-6);
    CHECK(r.rms(1, 1024) == doctest::Approx(0.25).epsilon(1e-3));
    r.engine.source(1).solo = false;
    r.engine.source(1).mute = true;
    r.engine.source(0).gainDb = -6.0206f;
    r.settle();
    r.run(1024);
    CHECK(r.rms(0, 1024) == doctest::Approx(0.125).epsilon(1e-3));
    CHECK(r.rms(1, 1024) < 1e-6);
}

TEST_CASE("engine: NaN, infinity and huge input never reach the speakers") {
    Rig r(3, 8);
    r.setLayout(presets::ring(8, 3.0f), PannerKind::VBAP);
    for (int i = 0; i < 3; ++i) r.engine.source(i).active = true;
    for (int k = 0; k < 4096; ++k) {
        r.in[0][static_cast<size_t>(k)] = std::numeric_limits<float>::quiet_NaN();
        r.in[1][static_cast<size_t>(k)] = (k % 2) ? std::numeric_limits<float>::infinity() : -1.0e30f;
        r.in[2][static_cast<size_t>(k)] = 1000.0f;
    }
    for (int i = 0; i < 10; ++i) r.run(4096);
    for (int c = 0; c < 8; ++c)
        for (float v : r.out[static_cast<size_t>(c)]) {
            REQUIRE(std::isfinite(v));
            REQUIRE(std::fabs(v) <= 0.99f);
        }
    CHECK(r.engine.takeInvalidInputEvents() > 0);
    CHECK(r.engine.takeLimiterEvents() > 0);
}

TEST_CASE("engine: result does not depend on the host block size") {
    auto render = [](int block) {
        Rig r(2, 8, 256);
        r.setLayout(presets::ring(8, 3.0f), PannerKind::DBAP);
        r.engine.source(0).active = true;
        r.engine.source(0).setPosition({1, 2, 0});
        r.engine.source(1).active = true;
        r.engine.source(1).setPosition({-2, -1, 0});
        r.settle(); // includes initial fades
        std::vector<std::vector<float>> result(8);
        long long ph0 = 0, ph1 = 0;
        const int total = 9000;
        std::vector<float> a(total), b(total);
        fillSine(a, 300, 0.3f, 48000, ph0);
        fillSine(b, 1234, 0.3f, 48000, ph1);
        for (int off = 0; off < total; off += block) {
            const int n = std::min(block, total - off);
            std::copy(a.begin() + off, a.begin() + off + n, r.in[0].begin());
            std::copy(b.begin() + off, b.begin() + off + n, r.in[1].begin());
            r.run(n);
            for (int c = 0; c < 8; ++c) result[static_cast<size_t>(c)].insert(result[static_cast<size_t>(c)].end(), r.out[static_cast<size_t>(c)].begin(), r.out[static_cast<size_t>(c)].begin() + n);
        }
        return result;
    };
    const auto ref = render(64);
    for (int block : {1, 7, 256, 1000, 4096}) {
        CAPTURE(block);
        const auto got = render(block);
        double maxDiff = 0;
        for (size_t c = 0; c < 8; ++c)
            for (size_t i = 0; i < ref[c].size(); ++i) maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(ref[c][i] - got[c][i])));
        CHECK(maxDiff < 1e-5);
    }
}

TEST_CASE("engine: in-place processing (inputs alias outputs)") {
    Rig r(2, 2);
    r.setLayout(presets::stereo(), PannerKind::VBAP);
    r.engine.source(0).active = true;
    r.engine.source(1).active = true;
    r.engine.source(1).setPosition({2, 0.5f, 0});
    r.settle();
    long long ph = 0;
    std::vector<float> sig(2048);
    fillSine(sig, 500, 0.4f, 48000, ph);
    std::vector<float> a = sig, b = sig;
    float* io[2] = {a.data(), b.data()};
    // Reference: separate buffers on an identical engine.
    Rig ref(2, 2);
    ref.setLayout(presets::stereo(), PannerKind::VBAP);
    ref.engine.source(0).active = true;
    ref.engine.source(1).active = true;
    ref.engine.source(1).setPosition({2, 0.5f, 0});
    ref.settle();
    std::copy(sig.begin(), sig.end(), ref.in[0].begin());
    std::copy(sig.begin(), sig.end(), ref.in[1].begin());
    ref.run(2048);
    r.engine.process(io, 2, io, 2, 2048);
    for (size_t i = 0; i < 2048; ++i) {
        CHECK(a[i] == doctest::Approx(ref.out[0][i]).epsilon(1e-6));
        CHECK(b[i] == doctest::Approx(ref.out[1][i]).epsilon(1e-6));
    }
}

TEST_CASE("engine: moving sources and layout changes do not click") {
    Rig r(1, 8);
    r.resize(256); // the input buffer holds exactly one block, so the sine stays continuous
    r.noReverb();
    r.setLayout(presets::ring(8, 3.0f), PannerKind::VBAP);
    auto& s = r.engine.source(0);
    s.active = true;
    long long ph = 0;
    const float amp = 0.5f, freq = 60.0f;
    // Largest possible step of the dry sine itself.
    const double sineStep = amp * 2.0 * 3.14159265 * freq / 48000.0;
    float last[8] = {};
    double worst = 0;
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> az(-180, 180);
    for (int block = 0; block < 400; ++block) {
        if (block % 10 == 0) s.setPosition(toCartesian({az(rng), 0, 3}));
        if (block == 150) r.setLayout(presets::ring(8, 3.0f, 0.0f, true), PannerKind::DBAP);
        if (block == 250) r.setLayout(presets::ring(8, 3.0f), PannerKind::Ambisonics);
        fillSine(r.in[0], freq, amp, 48000, ph);
        r.run(256);
        r.engine.collectGarbage();
        for (int c = 0; c < 8; ++c)
            for (int i = 0; i < 256; ++i) {
                const float v = r.out[static_cast<size_t>(c)][static_cast<size_t>(i)];
                if (block > 0) worst = std::max(worst, static_cast<double>(std::fabs(v - last[c])));
                last[c] = v;
            }
    }
    // A hard switch would jump by up to ~amp. Smoothed gains stay within a few sine steps.
    CHECK(worst < sineStep * 4.0 + 0.004);
}

TEST_CASE("engine: headphone preview renders two channels") {
    Rig r(1, 2);
    r.noReverb();
    r.setLayout(presets::ring(8, 3.0f), PannerKind::VBAP, OutputMode::Headphones);
    CHECK(r.engine.numOutputChannels() == 2);
    auto& s = r.engine.source(0);
    s.active = true;
    s.setPosition(toCartesian({90, 0, 3})); // left
    long long ph = 0;
    // Head shadow is a high-frequency effect: test at 3 kHz.
    for (int i = 0; i < 20; ++i) { fillSine(r.in[0], 3000, 0.3f, 48000, ph); r.run(512); r.engine.collectGarbage(); }
    fillSine(r.in[0], 3000, 0.3f, 48000, ph);
    r.run(4096);
    CHECK(r.rms(0, 4096) > 2.0 * r.rms(1, 4096));
}

TEST_CASE("engine: binaural layout places sources all around the head") {
    for (Polar where : {Polar{90, 0, 2}, Polar{-120, 30, 2}, Polar{60, -30, 2}, Polar{10, 80, 2}}) {
        Rig r(1, 2);
        r.noReverb();
        r.setLayout(binauralVirtualLayout(), PannerKind::VBAP, OutputMode::Headphones);
        auto& s = r.engine.source(0);
        s.active = true;
        s.setPosition(toCartesian(where));
        long long ph = 0;
        for (int i = 0; i < 20; ++i) { fillSine(r.in[0], 3000, 0.3f, 48000, ph); r.run(512); r.engine.collectGarbage(); }
        fillSine(r.in[0], 3000, 0.3f, 48000, ph);
        r.run(4096);
        const double l = r.rms(0, 4096), rr = r.rms(1, 4096);
        CHECK(l + rr > 0.02);
        if (where.azimuthDeg > 30.0f && std::fabs(where.elevationDeg) < 45.0f) CHECK(l > 1.5 * rr);
        if (where.azimuthDeg < -30.0f && std::fabs(where.elevationDeg) < 45.0f) CHECK(rr > 1.5 * l);
    }
}

TEST_CASE("engine: more speakers than outputs and vice versa") {
    Rig few(1, 2);
    few.setLayout(presets::ring(16, 3.0f), PannerKind::VBAP);
    few.engine.source(0).active = true;
    std::fill(few.in[0].begin(), few.in[0].end(), 0.1f);
    few.settle();
    few.run(512);
    Rig many(1, 32);
    many.setLayout(presets::quad(), PannerKind::VBAP);
    many.engine.source(0).active = true;
    std::fill(many.in[0].begin(), many.in[0].end(), 0.1f);
    many.settle();
    many.run(512);
    for (int c = 4; c < 32; ++c) CHECK(many.rms(c, 512) == 0.0);
}

TEST_CASE("engine: subwoofer gets low-passed signal only") {
    Rig r(1, 6);
    r.noReverb();
    r.setLayout(presets::surround51(), PannerKind::VBAP);
    r.engine.source(0).active = true;
    long long ph = 0;
    for (int i = 0; i < 30; ++i) { fillSine(r.in[0], 5000, 0.5f, 48000, ph); r.run(512); r.engine.collectGarbage(); }
    fillSine(r.in[0], 5000, 0.5f, 48000, ph);
    r.run(4096);
    const double highOnSub = r.rms(3, 4096);
    for (int i = 0; i < 10; ++i) { fillSine(r.in[0], 50, 0.5f, 48000, ph); r.run(4096); }
    CHECK(r.rms(3, 4096) > 20.0 * highOnSub);
}

TEST_CASE("engine: process() never allocates or frees memory") {
    Rig r(8, 16);
    r.setLayout(presets::ring(16, 3.0f), PannerKind::VBAP);
    for (int s = 0; s < 8; ++s) {
        r.engine.source(s).active = true;
        std::fill(r.in[static_cast<size_t>(s)].begin(), r.in[static_cast<size_t>(s)].end(), 0.1f * static_cast<float>(s));
    }
    r.settle();
    std::mt19937 rng(9);
    std::uniform_real_distribution<float> u(-5, 5);
    std::size_t allocations = 0;
    for (int block = 0; block < 300; ++block) {
        // Control-thread work (allowed to allocate) happens outside the guard.
        for (int s = 0; s < 8; ++s) r.engine.source(s).setPosition({u(rng), u(rng), u(rng)});
        if (block % 50 == 10) r.setLayout(block % 100 == 10 ? presets::dome({8, 8, 4}, 3, true) : presets::donut(4, 1, 8, 5),
                                          static_cast<PannerKind>(block % 3), block % 150 == 10 ? OutputMode::Headphones : OutputMode::Speakers);
        if (block % 30 == 0) {
            ReverbParams p;
            p.size = 0.5f + 0.004f * static_cast<float>(block);
            r.engine.setReverb(p);
        }
        r.engine.collectGarbage();
        {
            AllocGuard guard;
            r.run(block % 2 ? 4096 : 333);
            allocations += guard.allocations();
        }
    }
    CHECK(allocations == 0);
}

TEST_CASE("engine: speaker test plays noise on exactly one speaker") {
    Rig r(1, 8);
    r.noReverb();
    r.setLayout(presets::ring(8, 3.0f), PannerKind::VBAP);
    r.engine.speakerTest = 5;
    r.settle();
    r.run(2048);
    for (int c = 0; c < 8; ++c) {
        CAPTURE(c);
        if (c == 5) CHECK(r.rms(c, 2048) > 0.01);
        else CHECK(r.rms(c, 2048) == 0.0);
    }
    r.engine.speakerTest = 99; // out of range: ignored
    r.run(512);
}

TEST_CASE("engine: inputs and sources beyond 128 work (app: device, test signals, DAW tracks)") {
    // The app feeds 64 device inputs, 64 test signals and the DAW tracks after them.
    Rig r(260, 2);
    r.noReverb();
    r.setLayout(presets::stereo(), PannerKind::VBAP);
    auto& s = r.engine.source(200);
    s.active = true;
    s.inputChannel = 250;
    s.setPosition({0, 2, 0});
    long long ph = 0;
    for (int i = 0; i < 10; ++i) { fillSine(r.in[250], 440, 0.5f, 48000, ph); r.run(512); r.engine.collectGarbage(); }
    CHECK(r.rms(0, 512) > 0.05);
    CHECK(r.rms(1, 512) > 0.05);
    CHECK(r.engine.takeSourcePeak(200) > 0.1f);
}

namespace {
// Plays a sine through one source in front (stereo pair) and returns the output level.
double directLevel(float yawDeg, float directivity, float freq) {
    Rig r(1, 2);
    r.noReverb();
    r.setLayout(presets::stereo(2.0f), PannerKind::VBAP);
    auto& s = r.engine.source(0);
    s.active = true;
    s.yawDeg = yawDeg;
    s.directivity = directivity;
    s.setPosition(toCartesian({0, 0, 2}));
    long long ph = 0;
    for (int i = 0; i < 20; ++i) { fillSine(r.in[0], freq, 0.3f, 48000, ph); r.run(512); r.engine.collectGarbage(); }
    fillSine(r.in[0], freq, 0.3f, 48000, ph);
    r.run(4096);
    return r.rms(0, 4096) + r.rms(1, 4096);
}
} // namespace

TEST_CASE("engine: a directed source turned away is quieter and darker") {
    // Omni: the direction does not matter.
    CHECK(directLevel(180, 0, 500) == doctest::Approx(directLevel(0, 0, 500)).epsilon(1e-3));
    // Facing the listener: unchanged by directivity.
    CHECK(directLevel(0, 1, 8000) == doctest::Approx(directLevel(0, 0, 8000)).epsilon(1e-3));
    // Turned away: about 12 dB down in the bass, much more in the treble.
    const double lowRatio = directLevel(180, 1, 200) / directLevel(0, 1, 200);
    const double highRatio = directLevel(180, 1, 8000) / directLevel(0, 1, 8000);
    CHECK(lowRatio == doctest::Approx(0.25).epsilon(0.05));
    CHECK(highRatio < lowRatio * 0.5);
    // Halfway lies between.
    const double side = directLevel(90, 1, 200) / directLevel(0, 1, 200);
    CHECK(side < 0.9);
    CHECK(side > lowRatio);
}

TEST_CASE("engine: Doppler delays by distance and shifts the pitch while moving") {
    Rig r(1, 2);
    r.noReverb();
    r.setLayout(presets::stereo(2.0f), PannerKind::VBAP);
    auto& s = r.engine.source(0);
    s.active = true;
    s.doppler = true;
    s.distanceAmount = 0.0f;
    s.setPosition(toCartesian({0, 0, 34.3f})); // 100 ms away
    std::fill(r.in[0].begin(), r.in[0].end(), 0.0f);
    r.settle();
    // An impulse arrives 100 ms (4800 samples) later.
    r.resize(8192);
    r.in[0][0] = 1.0f;
    r.run(8192);
    int first = -1;
    for (int i = 0; i < 8192 && first < 0; ++i)
        if (std::fabs(r.out[0][static_cast<size_t>(i)]) + std::fabs(r.out[1][static_cast<size_t>(i)]) > 0.05f) first = i;
    CHECK(first >= 4795);
    CHECK(first <= 4805);

    // Approaching at 34.3 m/s (a tenth of the speed of sound) raises the pitch by ~10 %.
    const int block = 480; // 10 ms
    r.resize(block);
    long long ph = 0;
    float dist = 34.3f;
    int crossings = 0;
    float prev = 0.0f;
    for (int b = 0; b < 60; ++b) {
        dist -= 0.343f;
        s.setPosition(toCartesian({0, 0, dist}));
        fillSine(r.in[0], 1000, 0.3f, 48000, ph);
        r.run(block);
        if (b >= 20) // after the first 200 ms the moving sound has arrived
            for (int i = 0; i < block; ++i) {
                const float v = r.out[0][static_cast<size_t>(i)] + r.out[1][static_cast<size_t>(i)];
                if (prev < 0.0f && v >= 0.0f) ++crossings;
                prev = v;
            }
    }
    const double hz = crossings / 0.4;
    CHECK(hz > 1060.0);
    CHECK(hz < 1160.0);
}

TEST_CASE("engine: more Doppler sources than delay lines stay safe") {
    Rig r(40, 8);
    r.setLayout(presets::ring(8, 3.0f), PannerKind::VBAP);
    for (int i = 0; i < 40; ++i) {
        auto& s = r.engine.source(i);
        s.active = true;
        s.doppler = true;
        s.setPosition(toCartesian({float(i * 9), 0, 5.0f}));
        std::fill(r.in[static_cast<size_t>(i)].begin(), r.in[static_cast<size_t>(i)].end(), 0.1f);
    }
    r.settle();
    r.run(512);
    for (int c = 0; c < 8; ++c)
        for (int i = 0; i < 512; ++i) CHECK(std::isfinite(r.out[static_cast<size_t>(c)][static_cast<size_t>(i)]));
}

TEST_CASE("limiter: adjustable ceiling and release, gain reduction is reported") {
    rk::PeakLimiter lim;
    lim.prepare(48000.0);
    lim.setCeilingDb(-6.0f);
    std::vector<float> x(480, 1.0f);
    float peak = 0.0f;
    CHECK(lim.process(x.data(), (int)x.size(), true, peak));
    const float ceiling = std::pow(10.0f, -6.0f / 20.0f);
    CHECK(peak <= ceiling + 1.0e-6f);
    CHECK(peak == doctest::Approx(ceiling).epsilon(0.001));
    CHECK(lim.minGain() == doctest::Approx(ceiling).epsilon(0.001));

    // Faster release: the gain recovers sooner after the peak.
    auto recovered = [](float releaseMs) {
        rk::PeakLimiter l;
        l.prepare(48000.0);
        l.setRelease(releaseMs);
        std::vector<float> a(64, 2.0f), b(4800, 0.1f);
        float pk = 0.0f;
        l.process(a.data(), (int)a.size(), true, pk);
        l.process(b.data(), (int)b.size(), true, pk);
        return b.back() / 0.1f;
    };
    CHECK(recovered(10.0f) > recovered(500.0f));
    CHECK(recovered(10.0f) > 0.99f);

    // Off: no gain reduction, values only sanitised.
    std::vector<float> y(64, 0.5f);
    lim.setCeilingDb(-20.0f);
    CHECK_FALSE(lim.process(y.data(), (int)y.size(), false, peak));
    CHECK(y[10] == 0.5f);
    CHECK(lim.minGain() == 1.0f);
    // Garbage settings fall back to sane values.
    lim.setCeilingDb(NAN);
    lim.setRelease(INFINITY);
    std::vector<float> z(64, 4.0f);
    lim.process(z.data(), (int)z.size(), true, peak);
    CHECK(peak <= rk::PeakLimiter::kCeiling);
}

TEST_CASE("compressor: one gain for all outputs, the ratio of the channels stays") {
    rk::LinkedCompressor comp;
    comp.prepare(48000.0);
    rk::LinkedCompressor::Params p;
    p.enabled = true;
    p.thresholdDb = -20.0f;
    p.ratio = 4.0f;
    p.kneeDb = 0.0f;
    p.attackMs = 1.0f;
    p.releaseMs = 100.0f;
    // Loud channel at 0 dBFS, quiet channel at -20 dBFS, constant.
    std::vector<float> a(48000, 1.0f), b(48000, 0.1f);
    float* ch[2] = {a.data(), b.data()};
    const float most = comp.process(ch, 2, (int)a.size(), p);
    // 20 dB over the threshold at 4:1 -> 15 dB gain reduction, settled after the attack.
    CHECK(most == doctest::Approx(15.0f).epsilon(0.01));
    CHECK(20.0f * std::log10(a.back()) == doctest::Approx(-15.0f).epsilon(0.01));
    // The quiet channel is turned down by exactly the same amount: the image stays.
    CHECK(a.back() / b.back() == doctest::Approx(10.0f).epsilon(1.0e-4));

    // Below the threshold nothing happens (after the release).
    std::vector<float> c(96000, 0.05f);
    float* q[1] = {c.data()};
    comp.process(q, 1, (int)c.size(), p);
    CHECK(c.back() == doctest::Approx(0.05f).epsilon(0.001));

    // Soft knee: reduction starts below the threshold, but only by little.
    CHECK(rk::LinkedCompressor::curveReductionDb(-22.0f, -20.0f, 4.0f, 6.0f) > 0.0f);
    CHECK(rk::LinkedCompressor::curveReductionDb(-24.0f, -20.0f, 4.0f, 6.0f) == 0.0f);
    CHECK(rk::LinkedCompressor::curveReductionDb(-10.0f, -20.0f, 4.0f, 6.0f) == doctest::Approx(7.5f));

    // Make-up raises the level, switching off releases smoothly without a jump.
    p.makeupDb = 6.0f;
    std::vector<float> d(4800, 0.05f);
    float* r[1] = {d.data()};
    comp.process(r, 1, (int)d.size(), p);
    CHECK(d.back() == doctest::Approx(0.05f * std::pow(10.0f, 6.0f / 20.0f)).epsilon(0.001));
    p.enabled = false;
    std::vector<float> e(4800, 0.05f);
    float* t[1] = {e.data()};
    comp.process(t, 1, (int)e.size(), p);
    float maxStep = 0.0f;
    for (size_t i = 1; i < e.size(); ++i) maxStep = std::max(maxStep, std::fabs(e[i] - e[i - 1]));
    CHECK(maxStep < 1.0e-4f);
    CHECK(e.back() == doctest::Approx(0.05f).epsilon(0.001));

    // Garbage parameters never produce garbage.
    p.enabled = true;
    p.thresholdDb = NAN;
    p.ratio = INFINITY;
    p.attackMs = -1.0f;
    std::vector<float> f(256, 2.0f);
    float* u[1] = {f.data()};
    comp.process(u, 1, (int)f.size(), p);
    for (float v : f) CHECK(std::isfinite(v));
}

TEST_CASE("compressor: engine turns all speakers down together, before the limiter") {
    Rig r(1, 4);
    r.setLayout(presets::ring(4, 2.0f), PannerKind::VBAP);
    r.noReverb();
    auto& s = r.engine.source(0);
    s.active = true;
    s.setPosition(toCartesian({30.0f, 0, 2.0f}));
    std::fill(r.in[0].begin(), r.in[0].end(), 0.5f);
    r.settle();
    r.run(4096);
    std::vector<float> before(4);
    for (int c = 0; c < 4; ++c) before[(size_t)c] = std::fabs(r.out[(size_t)c][4000]);
    r.engine.compressorEnabled = true;
    r.engine.compressorThresholdDb = -30.0f;
    r.engine.compressorRatio = 10.0f;
    r.engine.compressorAttackMs = 1.0f;
    for (int k = 0; k < 4; ++k) r.run(4096);
    CHECK(r.engine.takeCompressorReduction() > 3.0f);
    int loudest = 0;
    for (int c = 1; c < 4; ++c)
        if (before[(size_t)c] > before[(size_t)loudest]) loudest = c;
    const float g = std::fabs(r.out[(size_t)loudest][4000]) / before[(size_t)loudest];
    CHECK(g < 0.7f);
    for (int c = 0; c < 4; ++c)
        if (before[(size_t)c] > 1.0e-3f) CHECK(std::fabs(r.out[(size_t)c][4000]) / before[(size_t)c] == doctest::Approx(g).epsilon(0.01));
}
