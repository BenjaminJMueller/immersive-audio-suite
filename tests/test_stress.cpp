// Stress and fuzz-style tests. Run them under AddressSanitizer and ThreadSanitizer (see CI).
#include "engine_fixture.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <limits>
#include <random>
#include <thread>

using namespace rk;
using namespace rktest;

namespace {
float wild(std::mt19937& rng) {
    switch (rng() % 12) {
    case 0: return std::numeric_limits<float>::quiet_NaN();
    case 1: return std::numeric_limits<float>::infinity();
    case 2: return -std::numeric_limits<float>::infinity();
    case 3: return 1.0e30f;
    case 4: return -1.0e-40f;
    default: return std::uniform_real_distribution<float>(-10.0f, 10.0f)(rng);
    }
}

SpeakerLayout randomLayout(std::mt19937& rng) {
    SpeakerLayout l;
    const int n = static_cast<int>(rng() % 70);
    for (int i = 0; i < n; ++i) {
        Speaker s;
        s.position = {wild(rng), wild(rng), (rng() % 2) ? 0.0f : wild(rng)};
        s.aim = {wild(rng), wild(rng), wild(rng)};
        s.gainDb = wild(rng);
        s.delayMs = std::fabs(wild(rng)) * 10.0f;
        s.enabled = rng() % 8 != 0;
        s.isSubwoofer = rng() % 12 == 0;
        l.speakers.push_back(s);
    }
    return l;
}
} // namespace

TEST_CASE("stress: audio thread and UI thread hammer the engine concurrently") {
    Engine engine;
    engine.prepare(48000.0, 512);
    std::atomic<bool> stop{false};
    std::atomic<bool> bad{false};
    std::atomic<long> blocks{0};

    std::thread audio([&] {
        std::mt19937 rng(1);
        std::vector<std::vector<float>> in(16, std::vector<float>(1024)), out(64, std::vector<float>(1024));
        std::vector<const float*> ip;
        std::vector<float*> op;
        for (auto& c : in) ip.push_back(c.data());
        for (auto& c : out) op.push_back(c.data());
        while (!stop.load()) {
            for (auto& c : in)
                for (auto& v : c) v = (rng() % 1000 == 0) ? wild(rng) : std::uniform_real_distribution<float>(-1, 1)(rng);
            const int n = 1 + static_cast<int>(rng() % 1024);
            const int nOut = static_cast<int>(rng() % 65);
            engine.process(ip.data(), 16, op.data(), nOut, n);
            for (int c = 0; c < nOut; ++c)
                for (int i = 0; i < n; ++i)
                    if (!std::isfinite(op[static_cast<size_t>(c)][static_cast<size_t>(i)]) || std::fabs(op[static_cast<size_t>(c)][static_cast<size_t>(i)]) > 1.0f) bad = true;
            ++blocks;
        }
    });

    std::mt19937 rng(2);
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    int changes = 0;
    while (std::chrono::steady_clock::now() < until) {
        const int s = static_cast<int>(rng() % kMaxSources);
        auto& src = engine.source(s);
        src.active = rng() % 3 != 0;
        src.inputChannel = static_cast<int>(rng() % 40) - 5;
        src.setPosition({wild(rng), wild(rng), wild(rng)});
        src.gainDb = wild(rng);
        src.spread = wild(rng);
        src.reverbSendDb = wild(rng);
        src.distanceAmount = wild(rng);
        src.mute = rng() % 10 == 0;
        src.solo = rng() % 30 == 0;
        src.touch();
        engine.masterGainDb = wild(rng);
        engine.reverbWetDb = wild(rng);
        engine.subLevelDb = wild(rng);
        engine.limiterEnabled = true;
        if (rng() % 20 == 0) {
            EngineConfig c;
            c.layout = (rng() % 2) ? randomLayout(rng) : presets::all()[rng() % presets::all().size()].make();
            c.panner = static_cast<PannerKind>(rng() % 4); // includes an invalid value
            c.ambiOrder = static_cast<int>(rng() % 6);
            c.outputMode = static_cast<OutputMode>(rng() % 2);
            c.dbap.blur = wild(rng);
            c.dbap.rolloffDb = wild(rng);
            engine.setConfig(c);
            ++changes;
        }
        if (rng() % 10 == 0) {
            ReverbParams p{wild(rng), wild(rng) * 1000.0f, wild(rng) * 10.0f, wild(rng)};
            engine.setReverb(p);
        }
        engine.collectGarbage();
        engine.takeOutputPeak(static_cast<int>(rng() % 200) - 10);
        engine.takeSourcePeak(static_cast<int>(rng() % 200) - 10);
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    stop = true;
    audio.join();
    MESSAGE("blocks processed: " << blocks.load() << ", layout changes: " << changes);
    CHECK_FALSE(bad.load());
    CHECK(blocks.load() > 100);
}

TEST_CASE("stress: random configurations, block sizes and sample rates") {
    std::mt19937 rng(5);
    for (int iter = 0; iter < 60; ++iter) {
        const double sr = std::array<double, 6>{8000, 22050, 44100, 48000, 96000, 192000}[rng() % 6];
        Rig r(1 + static_cast<int>(rng() % 8), 1 + static_cast<int>(rng() % 70), 16 + static_cast<int>(rng() % 2048), sr);
        EngineConfig c;
        c.layout = randomLayout(rng);
        c.panner = static_cast<PannerKind>(rng() % 3);
        c.outputMode = static_cast<OutputMode>(rng() % 2);
        r.engine.setConfig(c);
        for (int s = 0; s < r.numIn; ++s) {
            r.engine.source(s).active = true;
            r.engine.source(s).setPosition({wild(rng), wild(rng), wild(rng)});
        }
        for (int b = 0; b < 20; ++b) {
            for (auto& ch : r.in)
                for (auto& v : ch) v = (rng() % 500 == 0) ? wild(rng) : std::uniform_real_distribution<float>(-2, 2)(rng);
            r.run(1 + static_cast<int>(rng() % 4096));
            r.engine.collectGarbage();
            for (auto& ch : r.out)
                for (float v : ch) REQUIRE((std::isfinite(v) && std::fabs(v) <= 1.0f));
        }
    }
}
