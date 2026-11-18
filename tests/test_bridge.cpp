// Plugin -> app bridge: continuity, clock drift, slot handling, sample-rate mismatch,
// corrupted shared memory, realtime safety and concurrent use.
#include "alloc_guard.hpp"

#include <doctest/doctest.h>
#include <rk/bridge.hpp>
#include <rk/engine.hpp>

#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define RK_GETPID _getpid
#else
#include <unistd.h>
#define RK_GETPID getpid
#endif

using namespace rk::bridge;

namespace {

std::string testName() {
    static int counter = 0;
#if defined(_WIN32)
    return "Local\\RkBridgeTest" + std::to_string(RK_GETPID()) + "_" + std::to_string(++counter);
#else
    return "/rkbt" + std::to_string(RK_GETPID()) + "_" + std::to_string(++counter);
#endif
}

struct Names {
    std::string name = testName();
    ~Names() { SharedMemory::unlink(name); }
};

struct Out {
    std::vector<float> l, r;
    float* ptr[2];
    explicit Out(int n) : l((size_t)n), r((size_t)n) { ptr[0] = l.data(); ptr[1] = r.data(); }
};

} // namespace

TEST_CASE("bridge: audio arrives continuously and in order") {
    Names nm;
    Sender tx;
    Receiver rx;
    REQUIRE(tx.open(nm.name));
    REQUIRE(rx.open(nm.name));
    REQUIRE(tx.claim() >= 0);
    const int slot = tx.slot();

    std::vector<float> block(128);
    const float* in[1] = {block.data()};
    long long written = 0;
    auto writeBlock = [&] {
        for (int k = 0; k < 128; ++k) block[(size_t)k] = (float)((written + k) % 1000) * 1.0e-3f;
        written += 128;
        tx.write(in, 1, 128, 48000.0, TrackParams{});
    };

    rx.beginBlock(48000.0);
    Out out(256);
    TrackParams p;
    std::vector<float> received;
    for (int it = 0; it < 400; ++it) {
        writeBlock();
        writeBlock();
        const int ch = rx.read(slot, out.ptr, 256, p);
        if (ch > 0) received.insert(received.end(), out.l.begin(), out.l.end());
    }
    REQUIRE(received.size() > 256 * 300);
    // After the fade-in, consecutive samples step by 0.001 (wrapping at 1000).
    int bad = 0;
    for (size_t i = 512; i + 1 < received.size(); ++i) {
        const float d = received[i + 1] - received[i];
        if (std::fabs(d - 1.0e-3f) > 2.0e-4f && std::fabs(d + 0.999f) > 2.0e-2f) ++bad;
    }
    CHECK(bad == 0);
    CHECK(rx.takeDropouts() == 0);
}

TEST_CASE("bridge: clock drift between DAW and app causes no dropouts") {
    for (double drift : {-0.002, 0.002}) {
        Names nm;
        Sender tx;
        Receiver rx;
        REQUIRE(tx.open(nm.name));
        REQUIRE(rx.open(nm.name));
        REQUIRE(tx.claim() >= 0);
        rx.beginBlock(48000.0);

        std::vector<float> block(512);
        const float* in[1] = {block.data()};
        double phase = 0.0, owed = 0.0;
        Out out(256);
        TrackParams p;
        int silentAfterStart = 0, reads = 0;
        // 60 seconds of audio: the DAW writes (1 + drift) times as many frames as the app reads.
        for (int it = 0; it < 48000 * 60 / 256; ++it) {
            owed += 256.0 * (1.0 + drift);
            while (owed >= 128.0) {
                for (int k = 0; k < 128; ++k) {
                    block[(size_t)k] = 0.5f * (float)std::sin(phase);
                    phase += 0.05;
                }
                tx.write(in, 1, 128, 48000.0, p);
                owed -= 128.0;
            }
            const int ch = rx.read(tx.slot(), out.ptr, 256, p);
            if (++reads > 20 && ch == 0) ++silentAfterStart;
        }
        CAPTURE(drift);
        CHECK(silentAfterStart == 0);
        CHECK(rx.takeDropouts() == 0);
    }
}

TEST_CASE("bridge: names, parameters, channel count and release are visible to the app") {
    Names nm;
    Sender tx;
    Receiver rx;
    REQUIRE(tx.open(nm.name));
    REQUIRE(rx.open(nm.name));
    CHECK(rx.scan().empty());
    REQUIRE(tx.claim() >= 0);
    tx.heartbeat("Gitarre \303\274berall");
    std::vector<float> a(64, 0.1f), b(64, -0.1f);
    const float* in[2] = {a.data(), b.data()};
    TrackParams p;
    p.azimuthDeg = 45.0f;
    p.elevationDeg = 10.0f;
    p.distance = 3.0f;
    p.gainDb = -6.0f;
    tx.write(in, 2, 64, 48000.0, p);

    auto tracks = rx.scan();
    REQUIRE(tracks.size() == 1);
    CHECK(tracks[0].name == std::string("Gitarre \303\274berall"));
    CHECK(tracks[0].channels == 2);
    CHECK(tracks[0].params.azimuthDeg == doctest::Approx(45.0f));
    CHECK(tracks[0].params.distance == doctest::Approx(3.0f));
    CHECK(tracks[0].params.gainDb == doctest::Approx(-6.0f));
    CHECK(tracks[0].sending);

    rx.heartbeat(48000.0);
    CHECK(tx.appRunning());
    CHECK(tx.appSampleRate() == doctest::Approx(48000.0));

    tx.release();
    CHECK(rx.scan().empty());
}

TEST_CASE("bridge: slots are limited and freed slots are reused") {
    Names nm;
    std::vector<std::unique_ptr<Sender>> senders;
    for (int i = 0; i < kSlots; ++i) {
        senders.push_back(std::make_unique<Sender>());
        REQUIRE(senders.back()->open(nm.name));
        CHECK(senders.back()->claim() == i);
    }
    Sender extra;
    REQUIRE(extra.open(nm.name));
    CHECK(extra.claim() == -1);
    senders[7]->release();
    CHECK(extra.claim() == 7);
    senders.clear(); // destructors release
    Receiver rx;
    REQUIRE(rx.open(nm.name));
    CHECK(rx.scan().size() == 1);
}

TEST_CASE("bridge: different sample rates stay silent and are reported") {
    Names nm;
    Sender tx;
    Receiver rx;
    REQUIRE(tx.open(nm.name));
    REQUIRE(rx.open(nm.name));
    REQUIRE(tx.claim() >= 0);
    rx.heartbeat(48000.0);
    rx.beginBlock(48000.0);
    std::vector<float> a(256, 0.5f);
    const float* in[1] = {a.data()};
    Out out(128);
    TrackParams p;
    for (int i = 0; i < 50; ++i) {
        tx.write(in, 1, 256, 44100.0, p);
        CHECK(rx.read(tx.slot(), out.ptr, 128, p) == 0);
        for (float v : out.l) REQUIRE(v == 0.0f);
    }
    auto tracks = rx.scan();
    REQUIRE(tracks.size() == 1);
    CHECK(tracks[0].sampleRateMismatch);
}

TEST_CASE("bridge: corrupted shared memory never crashes the app or the plugin") {
    Names nm;
    Sender tx;
    Receiver rx;
    REQUIRE(tx.open(nm.name));
    REQUIRE(rx.open(nm.name));
    REQUIRE(tx.claim() >= 0);
    SharedMemory raw;
    REQUIRE(raw.open(nm.name, sharedSize()));
    auto* bytes = static_cast<unsigned char*>(raw.data());
    std::mt19937 rng(11);
    std::vector<float> a(512, 0.25f);
    const float* in[2] = {a.data(), a.data()};
    Out out(512);
    TrackParams p;
    rx.beginBlock(48000.0);
    for (int it = 0; it < 3000; ++it) {
        // Scribble over the header and the first slots' control fields (and some audio).
        const int writes = 1 + (int)(rng() % 16);
        for (int k = 0; k < writes; ++k) {
            const size_t region = rng() % 3 == 0 ? sharedSize() : 4096;
            const size_t pos = rng() % region;
            if (rng() % 4 == 0 && pos + 8 <= sharedSize()) {
                uint64_t v = (uint64_t(rng()) << 32) | rng();
                if (rng() % 3 == 0) v = ~0ull;
                std::memcpy(bytes + pos, &v, 8);
            } else {
                bytes[pos] = (unsigned char)rng();
            }
        }
        const int n = 1 + (int)(rng() % 512);
        tx.write(in, 1 + (int)(rng() % 2), n, 48000.0, p);
        tx.heartbeat("x");
        for (int s : {0, 1, (int)(rng() % kSlots)}) {
            rx.read(s, out.ptr, n, p);
            for (int i = 0; i < n; ++i) {
                REQUIRE(std::isfinite(out.l[(size_t)i]));
                REQUIRE(std::fabs(out.r[(size_t)i]) <= 1.0e6f);
            }
            const TrackParams q = p.sanitized();
            REQUIRE(std::isfinite(q.azimuthDeg));
            REQUIRE(q.distance >= 0.0f);
        }
        if (it % 100 == 0) {
            for (const auto& t : rx.scan()) {
                REQUIRE(t.channels >= 1);
                REQUIRE(t.channels <= 2);
                REQUIRE(t.name.size() <= 64);
            }
        }
    }
    // A peer that destroyed the magic number makes the block unusable for new openers,
    // but must not crash them either.
    std::memset(bytes, 0xff, 8);
    Sender late;
    late.open(nm.name);
    CHECK(late.claim() == -1);
}

TEST_CASE("bridge: write and read never allocate") {
    Names nm;
    Sender tx;
    Receiver rx;
    REQUIRE(tx.open(nm.name));
    REQUIRE(rx.open(nm.name));
    REQUIRE(tx.claim() >= 0);
    std::vector<float> a(256, 0.1f);
    const float* in[2] = {a.data(), a.data()};
    Out out(256);
    TrackParams p;
    rk::Engine engine;
    engine.prepare(48000.0, 256);
    rktest::AllocGuard guard;
    rx.beginBlock(48000.0);
    for (int i = 0; i < 200; ++i) {
        tx.write(in, 2, 256, 48000.0, p);
        const int ch = rx.read(tx.slot(), out.ptr, 256, p);
        applyTrack(engine, 64, 128, ch, p, ch > 0);
    }
    CHECK(guard.allocations() == 0);
}

TEST_CASE("bridge: plugin and app threads running at the same time") {
    Names nm;
    Sender tx;
    Receiver rx;
    REQUIRE(tx.open(nm.name));
    REQUIRE(rx.open(nm.name));
    REQUIRE(tx.claim() >= 0);
    std::atomic<bool> stop{false};
    std::thread plugin([&] {
        std::vector<float> a(64);
        const float* in[2] = {a.data(), a.data()};
        std::mt19937 rng(5);
        TrackParams p;
        while (!stop) {
            const int n = 16 + (int)(rng() % 1024);
            a.resize((size_t)n);
            for (auto& v : a) v = std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
            in[0] = in[1] = a.data();
            p.azimuthDeg = std::uniform_real_distribution<float>(-400.0f, 400.0f)(rng);
            tx.write(in, 1 + (int)(rng() % 2), n, 48000.0, p);
            if (rng() % 50 == 0) tx.heartbeat("Spur " + std::to_string(rng() % 100));
            if (rng() % 500 == 0) { tx.release(); tx.claim(); }
            std::this_thread::yield();
        }
    });
    std::thread ui([&] {
        while (!stop) {
            rx.heartbeat(48000.0);
            for (const auto& t : rx.scan()) REQUIRE(t.channels <= 2);
            std::this_thread::yield();
        }
    });
    Out out(1024);
    TrackParams p;
    std::mt19937 rng(9);
    rx.beginBlock(48000.0);
    for (int it = 0; it < 20000; ++it) {
        const int n = 16 + (int)(rng() % 1000);
        for (int s = 0; s < 2; ++s) {
            rx.read(s, out.ptr, n, p);
            for (int i = 0; i < n; ++i) REQUIRE(std::isfinite(out.l[(size_t)i]));
        }
    }
    stop = true;
    plugin.join();
    ui.join();
}

// ---- return path: app -> DAW -------------------------------------------------------

namespace {
struct Multi {
    std::vector<std::vector<float>> data;
    std::vector<float*> ptr;
    Multi(int ch, int n) : data((size_t)ch, std::vector<float>((size_t)n)), ptr((size_t)ch) {
        for (int c = 0; c < ch; ++c) ptr[(size_t)c] = data[(size_t)c].data();
    }
};
} // namespace

TEST_CASE("return: speaker channels arrive continuously, at the chosen first channel") {
    Names nm;
    ReturnSender tx;
    ReturnReceiver rx;
    REQUIRE(tx.open(nm.name));
    REQUIRE(rx.open(nm.name));
    CHECK_FALSE(tx.listening());
    rx.heartbeat();
    CHECK(tx.listening());
    tx.heartbeat(48000.0);
    CHECK(rx.appRunning());

    // Channel c carries the ramp offset by c * 100, so each channel is recognisable.
    Multi app(12, 128);
    long long written = 0;
    Multi out(4, 256);
    std::vector<std::vector<float>> got(4);
    for (int it = 0; it < 400; ++it) {
        for (int rep = 0; rep < 2; ++rep) {
            for (int c = 0; c < 12; ++c)
                for (int k = 0; k < 128; ++k)
                    app.data[(size_t)c][(size_t)k] = (float)(((written + k) % 1000) + c * 100) * 1.0e-4f;
            written += 128;
            tx.write(app.ptr.data(), 12, 128);
        }
        const int ch = rx.read(8, out.ptr.data(), 4, 256, 48000.0);
        if (ch > 0) {
            CHECK(ch == 4);
            for (int c = 0; c < 4; ++c) got[(size_t)c].insert(got[(size_t)c].end(), out.data[(size_t)c].begin(), out.data[(size_t)c].end());
        }
    }
    REQUIRE(got[0].size() > 256 * 300);
    int bad = 0;
    for (int c = 0; c < 4; ++c) {
        for (size_t i = 512; i + 1 < got[(size_t)c].size(); ++i) {
            const float d = got[(size_t)c][i + 1] - got[(size_t)c][i];
            if (std::fabs(d - 1.0e-4f) > 2.0e-5f && std::fabs(d + 0.0999f) > 2.0e-3f) ++bad;
        }
        // Output c is app channel 8 + c: its level sits above (8 + c) * 100 * 1e-4.
        const float v = got[(size_t)c][2000];
        CHECK(v >= (float)(8 + c) * 0.01f - 1.0e-4f);
        CHECK(v < (float)(8 + c) * 0.01f + 0.1f);
    }
    CHECK(bad == 0);
    CHECK(rx.takeDropouts() == 0);

    // Outputs beyond what the app renders stay silent.
    const int ch = rx.read(10, out.ptr.data(), 4, 256, 48000.0);
    CHECK(ch == 2);
    for (float s : out.data[3]) CHECK(s == 0.0f);
}

TEST_CASE("return: clock drift between app and DAW causes no dropouts") {
    for (double drift : {-0.002, 0.002}) {
        Names nm;
        ReturnSender tx;
        ReturnReceiver rx;
        REQUIRE(tx.open(nm.name));
        REQUIRE(rx.open(nm.name));
        tx.heartbeat(48000.0);
        Multi app(2, 128);
        Multi out(2, 256);
        double phase = 0.0, owed = 0.0;
        int silent = 0;
        for (int it = 0; it < 48000 * 30 / 256; ++it) {
            owed += 256.0 * (1.0 + drift);
            while (owed >= 128.0) {
                for (int k = 0; k < 128; ++k) {
                    app.data[0][(size_t)k] = app.data[1][(size_t)k] = 0.5f * (float)std::sin(phase);
                    phase += 0.05;
                }
                tx.write(app.ptr.data(), 2, 128);
                owed -= 128.0;
            }
            if (rx.read(0, out.ptr.data(), 2, 256, 48000.0) == 0 && it > 20) ++silent;
        }
        CAPTURE(drift);
        CHECK(silent == 0);
        CHECK(rx.takeDropouts() == 0);
    }
}

TEST_CASE("return: no app, a stopped app or another sample rate give silence") {
    Names nm;
    ReturnReceiver rx;
    REQUIRE(rx.open(nm.name));
    Multi out(2, 64);
    for (auto& d : out.data) std::fill(d.begin(), d.end(), 1.0f);
    CHECK_FALSE(rx.appRunning());
    CHECK(rx.read(0, out.ptr.data(), 2, 64, 48000.0) == 0);
    for (auto& d : out.data)
        for (float s : d) CHECK(s == 0.0f);

    ReturnSender tx;
    REQUIRE(tx.open(nm.name));
    tx.heartbeat(44100.0);
    Multi app(2, 64);
    for (int i = 0; i < 20; ++i) tx.write(app.ptr.data(), 2, 64);
    CHECK(rx.appSampleRate() == doctest::Approx(44100.0));
    CHECK(rx.read(0, out.ptr.data(), 2, 64, 48000.0) == 0);
}

TEST_CASE("return: corrupted shared memory never crashes app or plugin") {
    Names nm;
    ReturnSender tx;
    ReturnReceiver rx;
    REQUIRE(tx.open(nm.name));
    REQUIRE(rx.open(nm.name));
    SharedMemory raw;
    REQUIRE(raw.open(nm.name, returnSize()));
    auto* bytes = static_cast<unsigned char*>(raw.data());
    std::mt19937 rng(23);
    Multi app(8, 512);
    for (auto& d : app.data) std::fill(d.begin(), d.end(), 0.25f);
    Multi out(8, 512);
    for (int it = 0; it < 3000; ++it) {
        for (int k = 0; k < 1 + (int)(rng() % 16); ++k) {
            const size_t pos = rng() % (rng() % 3 == 0 ? returnSize() : 256);
            if (rng() % 4 == 0 && pos + 8 <= returnSize()) {
                uint64_t v = (uint64_t(rng()) << 32) | rng();
                if (rng() % 3 == 0) v = ~0ull;
                std::memcpy(bytes + pos, &v, 8);
            } else {
                bytes[pos] = (unsigned char)rng();
            }
        }
        const int n = 1 + (int)(rng() % 512);
        tx.heartbeat(48000.0);
        rx.heartbeat();
        tx.write(app.ptr.data(), (int)(rng() % 9), n);
        const int got = rx.read((int)(rng() % 80) - 8, out.ptr.data(), 1 + (int)(rng() % 8), n, 48000.0);
        REQUIRE(got >= 0);
        REQUIRE(got <= 8);
        for (auto& d : out.data)
            for (int i = 0; i < n; ++i) REQUIRE(std::fabs(d[(size_t)i]) <= 1.0e6f);
        REQUIRE(rx.appChannels() <= kReturnChannels);
    }
}

TEST_CASE("return: write and read never allocate") {
    Names nm;
    ReturnSender tx;
    ReturnReceiver rx;
    REQUIRE(tx.open(nm.name));
    REQUIRE(rx.open(nm.name));
    tx.heartbeat(48000.0);
    Multi app(16, 256);
    Multi out(16, 256);
    rktest::AllocGuard guard;
    for (int i = 0; i < 200; ++i) {
        tx.write(app.ptr.data(), 16, 256);
        rx.read(0, out.ptr.data(), 16, 256, 48000.0);
    }
    CHECK(guard.allocations() == 0);
}
