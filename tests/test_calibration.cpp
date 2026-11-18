// Automatic speaker calibration: measurement analysis, multilateration, alignment, matching.
// The room is simulated: every speaker-to-microphone path is a fractional delay with 1/r
// attenuation, plus a wall reflection and noise, so the whole chain runs as it would live.
#include <doctest/doctest.h>
#include <rk/calibration.hpp>

#include <cmath>
#include <random>

using namespace rk;
using namespace rk::calib;

namespace {

struct Path {
    double seconds;
    double gain;
};

/// Recording of `sweep` through the given paths (windowed-sinc fractional delays) plus noise.
std::vector<float> simulate(const std::vector<float>& sweep, int recLen, double sr, const std::vector<Path>& paths,
                            float noiseRms, uint32_t seed) {
    std::vector<float> rec(static_cast<size_t>(recLen), 0.0f);
    constexpr int half = 32;
    for (const auto& p : paths) {
        const double d = p.seconds * sr;
        const int base = static_cast<int>(std::floor(d));
        const double frac = d - base;
        double taps[2 * half];
        for (int k = -half; k < half; ++k) {
            const double x = k - frac;
            const double sinc = std::fabs(x) < 1e-9 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double w = 0.5 + 0.5 * std::cos(kPi * x / (half + 1));
            taps[k + half] = sinc * w * p.gain;
        }
        for (int k = -half; k < half; ++k) {
            const int off = base + k;
            const double t = taps[k + half];
            for (int i = 0; i < static_cast<int>(sweep.size()); ++i) {
                const int j = i + off;
                if (j >= 0 && j < recLen) rec[static_cast<size_t>(j)] += static_cast<float>(sweep[static_cast<size_t>(i)] * t);
            }
        }
    }
    std::mt19937 rng(seed);
    std::normal_distribution<float> nd(0.0f, noiseRms);
    for (auto& v : rec) v += nd(rng);
    return rec;
}

SweepParams testSweep() {
    SweepParams p;
    p.sampleRate = 48000.0;
    p.seconds = 0.5;
    p.tailSeconds = 0.4;
    return p;
}

/// Observations as an ideal (or noisy) measurement would deliver them.
std::vector<Observation> syntheticObservations(const std::vector<Vec3>& speakers, const std::vector<Vec3>& points,
                                               double latency, float c, double jitter, uint32_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> nd(0.0, jitter);
    std::vector<Observation> obs;
    for (int i = 0; i < static_cast<int>(speakers.size()); ++i)
        for (int j = 0; j < static_cast<int>(points.size()); ++j) {
            Observation o;
            o.speaker = i;
            o.point = j;
            const float d = length(speakers[static_cast<size_t>(i)] - points[static_cast<size_t>(j)]);
            o.seconds = latency + d / c + (jitter > 0 ? nd(rng) : 0.0);
            o.levelDb = -20.0f * std::log10(d);
            obs.push_back(o);
        }
    return obs;
}

std::vector<Vec3> positions(const SpeakerLayout& l) {
    std::vector<Vec3> p;
    for (const auto& s : l.speakers) p.push_back(s.position);
    return p;
}

} // namespace

TEST_CASE("calibration: speed of sound follows the temperature") {
    CHECK(speedOfSound(20.0f) == doctest::Approx(343.2f).epsilon(0.002));
    CHECK(speedOfSound(0.0f) == doctest::Approx(331.3f).epsilon(0.001));
    CHECK(std::isfinite(speedOfSound(std::nanf(""))));
    CHECK(speedOfSound(1.0e9f) < 370.0f);
}

TEST_CASE("calibration: sweep is bounded, faded and has the requested length") {
    SweepParams p = testSweep();
    const auto s = makeSweep(p);
    REQUIRE(s.size() == 24000u);
    float peak = 0.0f;
    for (float v : s) peak = std::max(peak, std::fabs(v));
    CHECK(peak <= dbToGain(p.levelDb) + 1e-6f);
    CHECK(peak > 0.5f * dbToGain(p.levelDb));
    CHECK(std::fabs(s.front()) < 1e-6f);
    CHECK(std::fabs(s.back()) < 1e-3f);
    CHECK(recordLength(p) == 43200);

    SweepParams bad;
    bad.sampleRate = std::nan("");
    bad.f1 = -5;
    bad.f2 = 1e9;
    bad.seconds = 1e9;
    bad.levelDb = 40.0f;
    const SweepParams fixed = sanitize(bad);
    CHECK(fixed.sampleRate == 48000.0);
    CHECK(fixed.f2 <= 0.45 * fixed.sampleRate);
    CHECK(fixed.f1 < fixed.f2);
    CHECK(fixed.seconds <= 10.0);
    CHECK(fixed.levelDb <= -3.0f);
}

TEST_CASE("calibration: arrival time and level from a simulated room") {
    const SweepParams p = testSweep();
    const auto sweep = makeSweep(p);
    const int len = recordLength(p);
    // 12.3 ms interface latency, speaker 3.21 m away, a wall reflection 2.4 m longer.
    const double lat = 0.0123, c = 343.0;
    const double direct = lat + 3.21 / c;
    const auto rec = simulate(sweep, len, p.sampleRate, {{direct, 1.0 / 3.21}, {direct + 2.4 / c, 0.6 / 5.61}}, 1.0e-4f, 1);
    const Measurement m = analyse(rec, sweep, p);
    REQUIRE(m.arrival.valid);
    CHECK_FALSE(m.clipped);
    CHECK(m.arrival.seconds == doctest::Approx(direct).epsilon(2.0e-3));
    CHECK(std::fabs(m.arrival.seconds - direct) * c < 0.01); // better than 1 cm
    CHECK(m.arrival.snrDb > 30.0f);

    // Twice the distance: 6 dB less.
    const auto rec2 = simulate(sweep, len, p.sampleRate, {{lat + 6.42 / c, 1.0 / 6.42}}, 1.0e-4f, 2);
    const Measurement m2 = analyse(rec2, sweep, p);
    REQUIRE(m2.arrival.valid);
    CHECK(m.arrival.levelDb - m2.arrival.levelDb == doctest::Approx(6.0f).epsilon(0.08));
}

TEST_CASE("calibration: direct sound is found even if a reflection is louder (speaker faces away)") {
    const SweepParams p = testSweep();
    const auto sweep = makeSweep(p);
    const double c = 343.0, lat = 0.005;
    const double direct = lat + 2.0 / c;
    const auto rec = simulate(sweep, recordLength(p), p.sampleRate, {{direct, 0.12}, {direct + 1.5 / c, 0.3}}, 5.0e-5f, 3);
    const Measurement m = analyse(rec, sweep, p);
    REQUIRE(m.arrival.valid);
    CHECK(std::fabs(m.arrival.seconds - direct) * c < 0.02);
    CHECK(m.arrival.directness < 0.6f);
}

TEST_CASE("calibration: silence, noise and garbage never give a valid arrival") {
    const SweepParams p = testSweep();
    const auto sweep = makeSweep(p);
    const int len = recordLength(p);
    std::vector<float> silence(static_cast<size_t>(len), 0.0f);
    CHECK_FALSE(analyse(silence, sweep, p).arrival.valid);
    const auto noise = simulate(sweep, len, p.sampleRate, {}, 0.1f, 4);
    const Measurement mn = analyse(noise, sweep, p);
    CHECK_FALSE(mn.arrival.valid);
    CHECK(mn.tooQuiet);
    std::vector<float> nan(static_cast<size_t>(len), std::nanf(""));
    CHECK_FALSE(analyse(nan, sweep, p).arrival.valid);
    CHECK_FALSE(analyse({}, sweep, p).arrival.valid);
    CHECK_FALSE(analyse(silence, {}, p).arrival.valid);
    CHECK_FALSE(findArrival({}, 48000.0).valid);
    CHECK_FALSE(findArrival(std::vector<float>(100, 1.0f), std::nan("")).valid);

    std::vector<float> loud = simulate(sweep, len, p.sampleRate, {{0.01, 8.0}}, 0.0f, 5);
    CHECK(analyse(loud, sweep, p).clipped);
}

TEST_CASE("calibration: sweep runner plays, records and stops without touching idle audio") {
    SweepRunner r;
    std::vector<float> sweep(300);
    for (size_t i = 0; i < sweep.size(); ++i) sweep[i] = 0.001f * static_cast<float>(i + 1);
    r.prepare(sweep, 1000);
    float o0[128], o1[128], i0[128], i1[128];
    float* outs[2] = {o0, o1};
    const float* ins[2] = {i0, i1};
    std::fill(o0, o0 + 128, 0.5f);
    CHECK_FALSE(r.process(ins, 2, outs, 2, 128)); // idle: untouched
    CHECK(o0[0] == 0.5f);
    CHECK_FALSE(r.start(-1, 0));
    REQUIRE(r.start(1, 0, 1));
    CHECK_FALSE(r.start(1, 0)); // busy
    int played = 0;
    std::vector<float> heard;
    while (r.busy()) {
        for (int k = 0; k < 128; ++k) { i0[k] = 0.25f; i1[k] = -0.5f; }
        REQUIRE(r.process(ins, 2, outs, 2, 128));
        for (int k = 0; k < 128; ++k) {
            CHECK(o0[k] == 0.0f); // other speakers are silent
            heard.push_back(o1[k]);
        }
        ++played;
        REQUIRE(played < 20);
    }
    CHECK(r.state() == SweepRunner::State::Done);
    CHECK(heard[0] == doctest::Approx(0.001f));
    CHECK(heard[299] == doctest::Approx(0.3f));
    CHECK(heard[300] == 0.0f);
    CHECK(r.inputPeak() == doctest::Approx(0.25f));
    std::vector<float> rec, loop;
    REQUIRE(r.take(rec, loop));
    CHECK(rec.size() == 1000u);
    CHECK(rec[999] == 0.25f);
    CHECK(loop[0] == -0.5f);
    CHECK(r.state() == SweepRunner::State::Idle);

    // Missing channels and null buffers are fine; cancel stops at the next block.
    REQUIRE(r.start(7, 9));
    CHECK(r.process(nullptr, 0, nullptr, 0, 64));
    float* nulls[2] = {nullptr, nullptr};
    CHECK(r.process(ins, 2, nulls, 2, 64));
    r.cancel();
    CHECK_FALSE(r.process(ins, 2, outs, 2, 64));
    CHECK(r.state() == SweepRunner::State::Idle);
}

TEST_CASE("calibration: ring of 8 found from 5 measuring points with unknown latency") {
    const SpeakerLayout truth = presets::ring(8, 3.0f, 0.0f, true);
    std::vector<Vec3> real = positions(truth);
    real[2].z += 0.6f;  // one hangs higher than planned
    real[5].x += 0.3f;  // one stands a bit off
    const auto points = suggestPoints(truth, 5);
    REQUIRE(points.size() == 5u);
    CHECK(pointsSpanVolume(points));
    const float c = 343.0f;
    SolveInput in;
    in.speakerGuess = positions(truth); // the plan, not the truth
    in.pointGuess = points;
    in.observations = syntheticObservations(real, points, 0.0087, c, 20.0e-6, 7);
    in.speedOfSound = c;
    const SolveResult r = solvePositions(in);
    CHECK(r.latency == doctest::Approx(0.0087).epsilon(0.05));
    for (int i = 0; i < 8; ++i) {
        CAPTURE(i);
        CHECK(r.speakers[static_cast<size_t>(i)].solved);
        CHECK(length(r.speakers[static_cast<size_t>(i)].position - real[static_cast<size_t>(i)]) < 0.15f);
    }
    CHECK(r.rmsError < 0.02f);
}

TEST_CASE("calibration: Benny's donut (4 inside facing out, 8 outside facing in), points round the ring") {
    const SpeakerLayout plan = presets::donut(4, 1.0f, 8, 5.0f, 1.5f);
    std::vector<Vec3> real = positions(plan);
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> off(-0.4f, 0.4f);
    for (auto& p : real) p = p + Vec3{off(rng), off(rng), 0.5f * off(rng)};
    const auto points = suggestPoints(plan, 8);
    REQUIRE(points.size() == 8u);
    for (const auto& p : points) {
        const float r = length(Vec3{p.x, p.y, 0.0f});
        CHECK(r > 1.5f); // between the rings, not inside the column
        CHECK(r < 4.5f);
    }
    // Microphone placed only roughly (5 cm), timing noise, inner speakers not heard from
    // the far side of the column, and one reflection mistaken for the direct sound.
    std::vector<Vec3> realPoints = points;
    for (auto& p : realPoints) p = p + Vec3{0.5f * off(rng) / 4, 0.5f * off(rng) / 4, 0.0f};
    auto obs = syntheticObservations(real, realPoints, 0.0213, 343.0f, 30.0e-6, 12);
    std::vector<Observation> kept;
    for (const auto& o : obs) {
        const Vec3 s = real[static_cast<size_t>(o.speaker)], m = realPoints[static_cast<size_t>(o.point)];
        const bool inner = o.speaker < 4;
        if (inner && dot(normalized(Vec3{s.x, s.y, 0}), normalized(Vec3{m.x, m.y, 0})) < -0.2f) continue; // blocked
        kept.push_back(o);
    }
    kept[5].seconds += 1.2 / 343.0; // a reflection
    SolveInput in;
    in.speakerGuess = positions(plan);
    in.pointGuess = points;
    in.observations = kept;
    const SolveResult r = solvePositions(in);
    int solved = 0;
    for (int i = 0; i < 12; ++i) {
        CAPTURE(i);
        const auto& f = r.speakers[static_cast<size_t>(i)];
        CHECK(length(f.position - real[static_cast<size_t>(i)]) < 0.25f); // even the doubtful one
        if (!f.solved) continue;
        ++solved;
        CHECK(length(f.position - real[static_cast<size_t>(i)]) < 0.12f);
        CHECK(f.uncertainty < 0.3f);
    }
    // The speaker whose reflection was taken as direct sound has no measurement to spare: it is
    // placed from the remaining three but honestly reported as not solved.
    CHECK(solved >= 11);
}

TEST_CASE("calibration: a tape-measured distance or a loopback fixes the latency") {
    const SpeakerLayout plan = presets::ring(6, 4.0f);
    const auto real = positions(plan);
    // Four points close together: weak geometry on purpose.
    const std::vector<Vec3> points = {{0, 0, 0}, {1.0f, 0, 0}, {0, 1.0f, 0}, {0, 0, 1.0f}};
    SolveInput in;
    in.speakerGuess = real;
    for (auto& p : in.speakerGuess) p = p * 1.15f; // the plan is 15 % too big
    in.pointGuess = points;
    in.observations = syntheticObservations(real, points, 0.040, 343.0f, 10.0e-6, 13);
    SolveInput loop = in;
    loop.knownLatency = 0.040;
    const SolveResult rl = solvePositions(loop);
    CHECK(rl.latency == doctest::Approx(0.040));
    for (int i = 0; i < 6; ++i) CHECK(length(rl.speakers[static_cast<size_t>(i)].position - real[static_cast<size_t>(i)]) < 0.1f);

    SolveInput tape = in;
    tape.distances.push_back({0, 0, length(real[0])});
    const SolveResult rt = solvePositions(tape);
    CHECK(rt.latency == doctest::Approx(0.040).epsilon(0.01));
    for (int i = 0; i < 6; ++i) CHECK(length(rt.speakers[static_cast<size_t>(i)].position - real[static_cast<size_t>(i)]) < 0.15f);
}

TEST_CASE("calibration: solver survives broken input") {
    SolveInput in;
    CHECK(solvePositions(in).speakers.empty());
    in.speakerGuess = {{0, 2, 0}, {std::nanf(""), 1e30f, 0}};
    in.pointGuess = {{0, 0, 0}};
    in.observations = {{0, 0, 0.01, 0, 1}, {1, 0, std::nan(""), 0, 1}, {5, 9, 0.01, 0, 1}, {0, 0, 0.02, 0, -1}};
    in.distances = {{0, 0, -3}, {7, 7, 1}};
    in.speedOfSound = std::nanf("");
    in.maxIterations = 1000000;
    const SolveResult r = solvePositions(in);
    REQUIRE(r.speakers.size() == 2u);
    for (const auto& s : r.speakers) {
        CHECK(isFinite(s.position));
        CHECK_FALSE(s.solved); // one point is never enough
    }
    CHECK(std::isfinite(r.latency));
    // Many speakers stay fast (Schur complement).
    SolveInput big;
    const SpeakerLayout dome = presets::dome({16, 12, 8, 4}, 4.0f, true);
    big.speakerGuess = positions(dome);
    big.pointGuess = suggestPoints(dome, 8);
    big.observations = syntheticObservations(big.speakerGuess, big.pointGuess, 0.01, 343.0f, 20e-6, 3);
    const SolveResult rb = solvePositions(big);
    int ok = 0;
    for (const auto& s : rb.speakers) ok += s.solved ? 1 : 0;
    CHECK(ok == static_cast<int>(big.speakerGuess.size()));
}

TEST_CASE("calibration: measuring points span a volume") {
    CHECK_FALSE(pointsSpanVolume({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}}));
    CHECK(pointsSpanVolume({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}));
    CHECK_FALSE(pointsSpanVolume({{0, 0, 0}}));
    const auto one = suggestPoints(presets::stereo(), 1);
    REQUIRE(one.size() == 1u);
    CHECK(length(one[0]) < 1e-6f);
    CHECK(suggestPoints(SpeakerLayout{}, 99).size() == static_cast<size_t>(kMaxPoints));
}

TEST_CASE("calibration: one point aligns delay and level at the listening position") {
    // Speakers at 2, 3 and 4 m; the near one is louder and earlier.
    std::vector<Observation> obs = {{0, 0, 0.010 + 2.0 / 343.0, -6.0f, 1}, {1, 0, 0.010 + 3.0 / 343.0, -9.5f, 1},
                                    {2, 0, 0.010 + 4.0 / 343.0, -12.0f, 1}, {3, 0, 0.0, 0.0f, 0}};
    AlignInput in;
    in.numSpeakers = 5;
    in.observations = obs;
    const auto a = align(in);
    REQUIRE(a.size() == 5u);
    CHECK(a[0].delayMs == doctest::Approx(2000.0 / 343.0).epsilon(1e-3));
    CHECK(a[2].delayMs == doctest::Approx(0.0f));
    CHECK(a[0].gainDb == doctest::Approx(-6.0f));
    CHECK(a[2].gainDb == doctest::Approx(0.0f));
    CHECK_FALSE(a[3].measured);
    CHECK_FALSE(a[4].measured);

    // A speaker 20 dB quieter than the rest (not connected properly) is flagged, not followed.
    in.observations.push_back({4, 0, 0.02, -32.0f, 1});
    const auto b = align(in);
    CHECK(b[4].veryQuiet);
    CHECK(b[2].gainDb == doctest::Approx(0.0f));
    CHECK(b[4].gainDb <= 6.0f);
}

TEST_CASE("calibration: with geometry, levels from all points are combined") {
    const SpeakerLayout plan = presets::ring(4, 2.0f);
    const auto real = positions(plan);
    const auto points = suggestPoints(plan, 5);
    SolveInput in;
    in.speakerGuess = real;
    in.pointGuess = points;
    in.observations = syntheticObservations(real, points, 0.01, 343.0f, 0.0, 1);
    for (auto& o : in.observations)
        if (o.speaker == 1) o.levelDb -= 3.0f; // a less sensitive speaker
    const SolveResult g = solvePositions(in);
    AlignInput ai;
    ai.numSpeakers = 4;
    ai.observations = in.observations;
    ai.geometry = &g;
    ai.referencePosition = {0, 0, 0};
    const auto a = align(ai);
    for (int i = 0; i < 4; ++i) {
        CAPTURE(i);
        CHECK(a[static_cast<size_t>(i)].measured);
        CHECK(a[static_cast<size_t>(i)].distance == doctest::Approx(2.0f).epsilon(0.01));
        CHECK(a[static_cast<size_t>(i)].delayMs < 0.1f);
    }
    CHECK(a[1].gainDb == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(a[0].gainDb == doctest::Approx(-3.0f).epsilon(0.05));
}

TEST_CASE("calibration: matching measured positions to speakers") {
    const std::vector<Vec3> planned = {{0, 2, 0}, {2, 0, 0}, {0, -2, 0}, {-2, 0, 0}};
    const std::vector<Vec3> measured = {{-2.1f, 0.1f, 0}, {0.1f, 2.1f, 0}, {1.9f, 0, 0.2f}, {0, -2, 0}};
    const auto m = matchByDistance(planned, measured);
    CHECK(m == std::vector<int>{1, 2, 3, 0});
    CHECK(matchByDistance(planned, {}).size() == 4u);
    const auto few = matchByDistance(planned, {{2, 0, 0}});
    CHECK(few == std::vector<int>{-1, 0, -1, -1});
    const auto more = matchByDistance({{0, 2, 0}}, measured);
    CHECK(more == std::vector<int>{1});
    std::vector<Vec3> nanv = {{std::nanf(""), 0, 0}, {0, 2, 0}};
    const auto mn = matchByDistance(planned, nanv);
    CHECK(mn.size() == 4u);

    // Speakers 1 and 3 swapped (cables crossed).
    const std::vector<Vec3> heard = {{0, 2, 0}, {-2, 0, 0}, {0, -2, 0}, {2, 0, 0}};
    const auto sw = suspectedSwaps(planned, heard, {true, true, true, true});
    REQUIRE(sw.size() == 2u);
    CHECK(sw[0] == std::make_pair(1, 3));
    CHECK(sw[1] == std::make_pair(3, 1));
    CHECK(suspectedSwaps(planned, planned, {}).empty());
}

#include <rk/room_sim.hpp>

TEST_CASE("calibration end to end: simulated donut room, sweep out, microphone in, positions back") {
    // The real room: Benny's donut, every speaker a bit off the plan, the inner ones higher.
    const SpeakerLayout plan = presets::donut(4, 1.0f, 8, 5.0f, 1.5f);
    std::vector<RoomSimulator::Speaker> real;
    std::mt19937 rng(21);
    std::uniform_real_distribution<float> off(-0.3f, 0.3f);
    for (const auto& s : plan.speakers) real.push_back({s.position + Vec3{off(rng), off(rng), 0.3f * off(rng)}, s.aimDirection()});
    const double sr = 48000.0;
    RoomSimulator room;
    room.prepare(sr, real, 0.0137, -1.2f, 2.0e-5f, 343.0f);

    SweepParams p;
    p.sampleRate = sr;
    p.seconds = 0.5;
    p.tailSeconds = 0.4;
    const auto sweep = makeSweep(p);
    SweepRunner runner;
    runner.prepare(sweep, recordLength(p));

    const auto points = suggestPoints(plan, 8);
    constexpr int kBlock = 256;
    std::vector<std::vector<float>> outBufs(plan.speakers.size(), std::vector<float>(kBlock));
    std::vector<float*> outs;
    for (auto& b : outBufs) outs.push_back(b.data());
    std::vector<float> mic(kBlock, 0.0f), micPrev(kBlock, 0.0f);
    std::vector<Observation> obs;
    for (int j = 0; j < static_cast<int>(points.size()); ++j) {
        room.setMicrophone(points[static_cast<size_t>(j)] + Vec3{0.3f * off(rng) / 3, 0.3f * off(rng) / 3, 0.0f}); // placed within ~3 cm
        for (int i = 0; i < static_cast<int>(plan.speakers.size()); ++i) {
            REQUIRE(runner.start(i, 0));
            int guard = 0;
            while (runner.busy()) {
                const float* ins[1] = {micPrev.data()};
                runner.process(ins, 1, outs.data(), static_cast<int>(outs.size()), kBlock);
                room.process(outs.data(), static_cast<int>(outs.size()), mic.data(), kBlock);
                std::swap(mic, micPrev);
                REQUIRE(++guard < 10000);
            }
            std::vector<float> rec, loop;
            REQUIRE(runner.take(rec, loop));
            const Measurement m = analyse(rec, sweep, p);
            CHECK_FALSE(m.clipped);
            if (!m.arrival.valid) continue;
            Observation o;
            o.speaker = i;
            o.point = j;
            o.seconds = m.arrival.seconds;
            o.levelDb = m.arrival.levelDb;
            o.weight = std::clamp((m.arrival.snrDb - 15.0f) / 20.0f, 0.2f, 1.0f) * std::clamp(m.arrival.directness * 1.5f, 0.3f, 1.0f);
            obs.push_back(o);
        }
    }
    CHECK(obs.size() >= 90u); // 96 sweeps, nearly all heard

    SolveInput in;
    for (const auto& s : plan.speakers) in.speakerGuess.push_back(s.position);
    in.pointGuess = points;
    in.observations = obs;
    const SolveResult r = solvePositions(in);
    int solved = 0;
    for (size_t i = 0; i < real.size(); ++i) {
        CAPTURE(i);
        if (!r.speakers[i].solved) continue;
        ++solved;
        CHECK(length(r.speakers[i].position - real[i].position) < 0.10f);
    }
    CHECK(solved >= 11);
    // Latency of the interface plus one audio block.
    CHECK(r.latency == doctest::Approx(0.0137 + kBlock / sr).epsilon(0.03));

    AlignInput ai;
    ai.numSpeakers = static_cast<int>(plan.speakers.size());
    ai.observations = obs;
    ai.geometry = &r;
    ai.referencePosition = r.points[0];
    const auto a = align(ai);
    // The farthest speaker gets no delay, nearer ones wait for it.
    float maxD = 0.0f;
    for (size_t i = 0; i < real.size(); ++i) maxD = std::max(maxD, length(real[i].position - points[0]));
    for (size_t i = 0; i < real.size(); ++i) {
        CAPTURE(i);
        REQUIRE(a[i].measured);
        const float expect = (maxD - length(real[i].position - points[0])) / 343.0f * 1000.0f;
        CHECK(std::fabs(a[i].delayMs - expect) < 0.5f); // half a millisecond = 17 cm
        CHECK(a[i].gainDb <= 6.0f);
        CHECK(a[i].gainDb >= -30.0f);
    }
}

#include <thread>

TEST_CASE("calibration threads: sweep runner on a real audio thread, start, take and cancel from another") {
    SweepRunner r;
    std::vector<float> sweep(2000, 0.1f);
    r.prepare(sweep, 3000);
    std::atomic<bool> stop{false};
    std::thread audio([&] {
        float o[2][64], in[64];
        float* outs[2] = {o[0], o[1]};
        const float* ins[1] = {in};
        for (float& v : in) v = 0.01f;
        while (!stop.load()) {
            r.process(ins, 1, outs, 2, 64);
            std::this_thread::yield();
        }
    });
    int completed = 0, cancelled = 0;
    for (int k = 0; k < 200; ++k) {
        while (r.busy()) std::this_thread::yield();
        std::vector<float> rec, loop;
        if (r.take(rec, loop)) {
            CHECK(rec.size() == 3000u);
            ++completed;
        }
        REQUIRE(r.start(k % 2, 0));
        if (k % 3 == 0) {
            r.cancel();
            ++cancelled;
        }
    }
    while (r.busy()) std::this_thread::yield();
    stop.store(true);
    audio.join();
    CHECK(completed + cancelled >= 150);
}
