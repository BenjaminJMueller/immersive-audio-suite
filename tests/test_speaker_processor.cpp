#include "alloc_guard.hpp"

#include <doctest/doctest.h>
#include <rk/speaker_processor.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace rk;

namespace {
struct Bufs {
    std::vector<std::vector<float>> ch;
    std::vector<float*> ptr;
    Bufs(int n, int frames) : ch(static_cast<size_t>(n), std::vector<float>(static_cast<size_t>(frames), 0.0f)) {
        for (auto& c : ch) ptr.push_back(c.data());
    }
};
void settle(SpeakerProcessor& p, int numCh) {
    Bufs b(numCh, 256);
    for (int i = 0; i < 20; ++i) { p.process(b.ptr.data(), numCh, 256); p.collectGarbage(); }
}
} // namespace

TEST_CASE("speaker processor: applies calibration delay and gain") {
    SpeakerLayout l = presets::quad();
    l.speakers[1].delayMs = 1.0f;   // 48 samples
    l.speakers[2].gainDb = -6.0206f;
    SpeakerProcessor p;
    p.prepare(48000.0, 256);
    p.setLayout(l, false);
    settle(p, 4);
    Bufs b(4, 256);
    for (auto& c : b.ch) c[10] = 0.5f;
    p.process(b.ptr.data(), 4, 256);
    CHECK(b.ch[0][10] == doctest::Approx(0.5f));
    CHECK(b.ch[1][10] == 0.0f);
    CHECK(b.ch[1][58] == doctest::Approx(0.5f));
    CHECK(b.ch[2][10] == doctest::Approx(0.25f).epsilon(1e-3));
}

TEST_CASE("speaker processor: protects against garbage and overload") {
    SpeakerProcessor p;
    p.prepare(44100.0, 128);
    p.setLayout(presets::ring(8, 3.0f), false);
    Bufs b(8, 1000);
    for (int k = 0; k < 30; ++k) {
        for (size_t c = 0; c < 8; ++c)
            for (size_t i = 0; i < 1000; ++i)
                b.ch[c][i] = (i % 3 == 0) ? std::numeric_limits<float>::quiet_NaN() : (i % 3 == 1 ? 1.0e9f : -50.0f);
        p.process(b.ptr.data(), 8, 1000);
        p.collectGarbage();
        for (auto& c : b.ch)
            for (float v : c) REQUIRE((std::isfinite(v) && std::fabs(v) <= 0.99f));
    }
    CHECK(p.takeLimiterEvents() > 0);
}

TEST_CASE("speaker processor: headphones, speaker test and no allocation") {
    SpeakerProcessor p;
    p.prepare(48000.0, 512);
    p.setLayout(presets::ring(8, 3.0f), true);
    settle(p, 8);
    Bufs b(8, 512);
    p.speakerTest = 2; // left speaker
    std::size_t allocs = 0;
    double el = 0, er = 0, rest = 0;
    for (int k = 0; k < 50; ++k) {
        for (auto& c : b.ch) std::fill(c.begin(), c.end(), 0.0f);
        {
            rktest::AllocGuard g;
            p.process(b.ptr.data(), 8, 512);
            allocs += g.allocations();
        }
        for (int i = 0; i < 512; ++i) {
            el += static_cast<double>(b.ch[0][static_cast<size_t>(i)]) * b.ch[0][static_cast<size_t>(i)];
            er += static_cast<double>(b.ch[1][static_cast<size_t>(i)]) * b.ch[1][static_cast<size_t>(i)];
            for (size_t c = 2; c < 8; ++c) rest += std::fabs(b.ch[c][static_cast<size_t>(i)]);
        }
        if (k == 25) p.setLayout(presets::donut(4, 1, 8, 5), true); // swap while running
        p.collectGarbage();
    }
    CHECK(allocs == 0);
    CHECK(el > er);
    CHECK(rest == 0.0);
}

TEST_CASE("speaker processor: unprepared and odd channel counts") {
    SpeakerProcessor p;
    Bufs b(3, 64);
    p.process(b.ptr.data(), 3, 64);
    p.process(nullptr, 3, 64);
    p.prepare(48000.0, 64);
    p.setLayout(presets::surround714(), false);
    Bufs big(200, 100);
    p.process(big.ptr.data(), 200, 100);
    Bufs longer(3, 1000);
    p.process(longer.ptr.data(), 3, 1000);
}
