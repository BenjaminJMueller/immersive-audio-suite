#include "helpers.hpp"

#include <doctest/doctest.h>
#include <rk/panners.hpp>

using namespace rk;
using namespace rktest;

TEST_CASE("SN3D encoder: known values") {
    float y[kMaxAmbiChannels];
    encodeSN3D({0, 1, 0}, 3, y); // front
    CHECK(y[0] == 1.0f);
    CHECK(y[1] == doctest::Approx(0.0f)); // Y (left)
    CHECK(y[2] == doctest::Approx(0.0f)); // Z
    CHECK(y[3] == doctest::Approx(1.0f)); // X (front)
    encodeSN3D({-1, 0, 0}, 1, y);         // left
    CHECK(y[1] == doctest::Approx(1.0f));
    encodeSN3D({0, 0, 1}, 2, y); // up
    CHECK(y[2] == doctest::Approx(1.0f));
    CHECK(y[6] == doctest::Approx(1.0f));
}

TEST_CASE("SN3D encoder: orthogonality over the sphere") {
    // For SN3D: mean over the sphere of Y_i * Y_j = delta_ij / (2n + 1).
    constexpr int K = 20000;
    const float golden = kPi * (3.0f - std::sqrt(5.0f));
    double acc[16][16] = {};
    float y[kMaxAmbiChannels];
    for (int k = 0; k < K; ++k) {
        const float z = 1.0f - 2.0f * (k + 0.5f) / K;
        const float r = std::sqrt(1.0f - z * z);
        encodeSN3D({r * std::cos(golden * k), r * std::sin(golden * k), z}, 3, y);
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 16; ++j) acc[i][j] += static_cast<double>(y[i]) * y[j] / K;
    }
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j) {
            const int n = static_cast<int>(std::sqrt(static_cast<double>(i)) + 1e-6);
            const double expect = i == j ? 1.0 / (2 * n + 1) : 0.0;
            CAPTURE(i);
            CAPTURE(j);
            CHECK(acc[i][j] == doctest::Approx(expect).epsilon(0.01).scale(1.0));
        }
}

TEST_CASE("max-rE weights") {
    CHECK(maxReWeight(0, 3) == 1.0f);
    for (int order = 1; order <= 3; ++order)
        for (int n = 1; n <= order; ++n) {
            CHECK(maxReWeight(n, order) < maxReWeight(n - 1, order));
            CHECK(maxReWeight(n, order) > 0.0f);
        }
}

TEST_CASE("Ambisonics panner: localisation follows the source") {
    for (const auto& l : {presets::ring(16, 3.0f), presets::dome({8, 8, 4}, 3.0f, true), presets::surround714()}) {
        CAPTURE(l.name);
        for (int order = 1; order <= 3; ++order) {
            AmbisonicsPanner p;
            p.prepare(l, order);
            const int n = static_cast<int>(l.speakers.size());
            float g[kMaxSpeakers];
            for (float az = -180.0f; az < 180.0f; az += 15.0f) {
                const Vec3 src = toCartesian({az, 0, 3.0f});
                p.compute({src, 0}, g);
                CHECK(energy(g, n) == doctest::Approx(1.0f).epsilon(1e-3));
                // Energy vector (rE) should point roughly towards the source.
                Vec3 rE{};
                for (int i = 0; i < n; ++i) rE = rE + normalized(l.speakers[static_cast<size_t>(i)].position) * (g[i] * g[i]);
                const float err = std::acos(std::clamp(dot(normalized(rE), normalized(src)), -1.0f, 1.0f)) * kRadToDeg;
                CAPTURE(az);
                CAPTURE(order);
                CHECK(err < 20.0f);
            }
        }
    }
}

TEST_CASE("Ambisonics panner: spread and NaN") {
    AmbisonicsPanner p;
    p.prepare(presets::ring(8, 2.0f), 3);
    float g[kMaxSpeakers];
    p.compute({{0, 2, 0}, 1.0f}, g); // fully diffuse = omni
    for (int i = 1; i < 8; ++i) CHECK(g[i] == doctest::Approx(g[0]).epsilon(1e-3));
    p.compute({{NAN, NAN, NAN}, NAN}, g);
    CHECK(energy(g, 8) == doctest::Approx(1.0f).epsilon(1e-3));
    p.prepare(SpeakerLayout{}, 99);
    CHECK(p.order() == kMaxAmbiOrder);
    p.compute({{0, 1, 0}, 0}, g);
}
