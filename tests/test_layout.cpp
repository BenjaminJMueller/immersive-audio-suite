#include "helpers.hpp"

#include <doctest/doctest.h>
#include <rk/layout.hpp>

#include <limits>

using namespace rk;

TEST_CASE("polar/cartesian round trip and conventions") {
    const Vec3 front = toCartesian({0, 0, 2});
    CHECK(front.y == doctest::Approx(2.0f));
    CHECK(front.x == doctest::Approx(0.0f).epsilon(1e-5));
    const Vec3 left = toCartesian({90, 0, 1});
    CHECK(left.x == doctest::Approx(-1.0f)); // +azimuth is to the left (x is right)
    const Vec3 up = toCartesian({0, 90, 1});
    CHECK(up.z == doctest::Approx(1.0f));
    for (float az = -170; az < 180; az += 37)
        for (float el = -80; el < 90; el += 33) {
            const Polar p = toPolar(toCartesian({az, el, 3.0f}));
            CHECK(p.azimuthDeg == doctest::Approx(az).epsilon(1e-3));
            CHECK(p.elevationDeg == doctest::Approx(el).epsilon(1e-3));
            CHECK(p.distance == doctest::Approx(3.0f));
        }
}

TEST_CASE("every preset is valid") {
    for (const auto& preset : presets::all()) {
        CAPTURE(preset.id);
        SpeakerLayout l = preset.make();
        CHECK_FALSE(l.name.empty());
        CHECK_FALSE(l.speakers.empty());
        CHECK(static_cast<int>(l.speakers.size()) <= kMaxSpeakers);
        CHECK_FALSE(sanitize(l)); // presets need no fixing
        const LayoutInfo info = analyze(l);
        CHECK(info.numActive > 0);
    }
}

TEST_CASE("analyze recommends sensible panners") {
    CHECK(analyze(presets::stereo()).recommended == PannerKind::VBAP);
    const LayoutInfo ring = analyze(presets::ring(8, 3.0f));
    CHECK(ring.surroundsOrigin);
    CHECK(ring.isFlat);
    CHECK(ring.recommended == PannerKind::VBAP);
    CHECK_FALSE(analyze(presets::dome({8, 8, 4}, 3.0f, true)).isFlat);
}

TEST_CASE("donut: rings face each other and DBAP is recommended") {
    const SpeakerLayout d = presets::donut(4, 1.0f, 8, 5.0f);
    REQUIRE(d.speakers.size() == 12);
    // Inner ring points outwards, outer ring points inwards.
    for (const auto& s : d.speakers) {
        const Vec3 radial = normalized(Vec3{s.position.x, s.position.y, 0});
        const float c = dot(s.aimDirection(), radial);
        if (s.group == 0) CHECK(c > 0.99f);
        else CHECK(c < -0.99f);
    }
    const LayoutInfo info = analyze(d);
    CHECK(info.facesInwardAndOutward);
    CHECK(info.recommended == PannerKind::DBAP);
    CHECK(std::find(info.hints.begin(), info.hints.end(), "donut_uses_dbap") != info.hints.end());
}

TEST_CASE("donut clamps nonsense parameters") {
    const SpeakerLayout d = presets::donut(-5, std::numeric_limits<float>::quiet_NaN(), 100000, -3.0f);
    CHECK(d.speakers.size() >= 2);
    CHECK(static_cast<int>(d.speakers.size()) <= kMaxSpeakers);
    for (const auto& s : d.speakers) CHECK(isFinite(s.position));
}

TEST_CASE("sanitize fixes NaN, infinity and huge layouts") {
    SpeakerLayout l;
    for (int i = 0; i < 300; ++i) l.speakers.push_back(Speaker{});
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    l.speakers[0].position = {nan, inf, -inf};
    l.speakers[1].gainDb = 400.0f;
    l.speakers[2].delayMs = -5.0f;
    l.speakers[3].group = -9;
    l.speakers[4].name = std::string(1000, 'x');
    CHECK(sanitize(l));
    CHECK(static_cast<int>(l.speakers.size()) == kMaxSpeakers);
    CHECK(isFinite(l.speakers[0].position));
    CHECK(l.speakers[1].gainDb == 12.0f);
    CHECK(l.speakers[2].delayMs == 0.0f);
    CHECK(l.speakers[3].group == 0);
    CHECK(l.speakers[4].name.size() == 64);
    CHECK_FALSE(sanitize(l));
}

TEST_CASE("analyze handles empty and degenerate layouts") {
    SpeakerLayout empty;
    CHECK(analyze(empty).numActive == 0);
    SpeakerLayout allAtOrigin;
    allAtOrigin.speakers.resize(5);
    for (auto& s : allAtOrigin.speakers) s.position = {0, 0, 0};
    const LayoutInfo info = analyze(allAtOrigin);
    CHECK(info.numActive == 5);
}

TEST_CASE("safe helpers") {
    CHECK(safeClamp(std::numeric_limits<float>::quiet_NaN(), 0, 1, 0.5f) == 0.5f);
    CHECK(safeClamp(std::numeric_limits<float>::infinity(), 0, 1, 0.5f) == 1.0f);
    CHECK(sanitizeSample(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    CHECK(sanitizeSample(1.0e-30f) == 0.0f);
    CHECK(sanitizeSample(0.5f) == 0.5f);
    CHECK(wrapDegrees(540.0f) == doctest::Approx(-180.0f));
    CHECK(wrapDegrees(-190.0f) == doctest::Approx(170.0f));
    CHECK(dbToGain(-200.0f) == 0.0f);
    CHECK(dbToGain(0.0f) == doctest::Approx(1.0f));
}
