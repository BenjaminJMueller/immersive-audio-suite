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
