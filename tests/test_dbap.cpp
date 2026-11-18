#include "helpers.hpp"

#include <doctest/doctest.h>
#include <rk/panners.hpp>

using namespace rk;
using namespace rktest;

TEST_CASE("DBAP: energy preserved, nearest speaker loudest") {
    for (const auto& l : allPresets()) {
        CAPTURE(l.name);
        DbapPanner p;
        p.prepare(l, {});
        const int n = static_cast<int>(l.speakers.size());
        float g[kMaxSpeakers];
        for (int i = 0; i < n; ++i) {
            const Speaker& s = l.speakers[static_cast<size_t>(i)];
            if (s.isSubwoofer) continue;
            // Slightly in front of the speaker, inside its coverage.
            p.compute({s.position + s.aimDirection() * 0.2f, 0}, g);
            CHECK(energy(g, n) == doctest::Approx(1.0f).epsilon(1e-4));
            CHECK(argmax(g, n) == i);
        }
    }
}

TEST_CASE("DBAP: speakers facing away contribute less (donut)") {
    const SpeakerLayout d = presets::donut(4, 1.0f, 8, 5.0f);
    DbapPanner::Params withDir;
    withDir.directivity = 1.0f;
    DbapPanner::Params noDir;
    noDir.directivity = 0.0f;
    DbapPanner a, b;
    a.prepare(d, withDir);
    b.prepare(d, noDir);
    float ga[kMaxSpeakers], gb[kMaxSpeakers];
    // Source in the audience ring at the front (radius 3 m).
    const Vec3 src = toCartesian({0, 0, 3.0f}) + Vec3{0, 0, 1.5f};
    a.compute({src, 0}, ga);
    b.compute({src, 0}, gb);
    // Inner speaker at the back (I3, azimuth 180) faces away from the source.
    const int innerBack = 2;
    CHECK(d.speakers[innerBack].name == "I3");
    CHECK(ga[innerBack] < gb[innerBack] * 0.5f);
    // The nearest inner speaker (I1, front) faces the source and dominates the inner ring.
    CHECK(ga[0] > ga[1]);
    CHECK(ga[0] > ga[3]);
}

TEST_CASE("DBAP: spread makes the image wider, never louder") {
    const SpeakerLayout l = presets::ring(16, 4.0f);
    DbapPanner p;
    p.prepare(l, {});
    float tight[kMaxSpeakers], wide[kMaxSpeakers];
    p.compute({{0, 3.5f, 0}, 0.0f}, tight);
    p.compute({{0, 3.5f, 0}, 1.0f}, wide);
    CHECK(energy(wide, 16) == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(wide[argmax(tight, 16)] < tight[argmax(tight, 16)]);
}

TEST_CASE("DBAP: robust against NaN and empty layouts") {
    DbapPanner p;
    p.prepare(SpeakerLayout{}, {});
    float g[kMaxSpeakers];
    p.compute({{1, 2, 3}, 0}, g);
    p.prepare(presets::quad(), {NAN, NAN, NAN});
    p.compute({{NAN, 0, INFINITY}, NAN}, g);
    CHECK(energy(g, 4) == doctest::Approx(1.0f).epsilon(1e-4));
    // Source exactly on a speaker: no division by zero.
    p.compute({presets::quad().speakers[0].position, 0}, g);
    for (int i = 0; i < 4; ++i) CHECK(std::isfinite(g[i]));
}
