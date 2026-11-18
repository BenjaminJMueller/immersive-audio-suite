#include <doctest/doctest.h>
#include <rk/reverb.hpp>

#include <cmath>
#include <vector>

using namespace rk;

namespace {
struct Ir {
    std::vector<std::vector<float>> ch;
};

Ir impulseResponse(const ReverbParams& params, int numOut, int len, double sr = 48000.0) {
    FdnReverb r;
    r.prepare(sr);
    r.setParams(params);
    Ir ir;
    ir.ch.assign(static_cast<size_t>(numOut), std::vector<float>(static_cast<size_t>(len), 0.0f));
    std::vector<float> in(static_cast<size_t>(len), 0.0f);
    in[0] = 1.0f;
    std::vector<float*> ptr;
    for (auto& c : ir.ch) ptr.push_back(c.data());
    const int block = 256;
    for (int off = 0; off < len; off += block) {
        const int n = std::min(block, len - off);
        std::vector<float*> p2;
        for (auto* p : ptr) p2.push_back(p + off);
        r.process(in.data() + off, p2.data(), numOut, n, 1.0f);
    }
    return ir;
}

double energy(const std::vector<float>& v, size_t from, size_t to) {
    double e = 0;
    for (size_t i = from; i < to && i < v.size(); ++i) e += static_cast<double>(v[i]) * v[i];
    return e;
}
} // namespace

TEST_CASE("reverb: finite, decays with the requested RT60") {
    ReverbParams p;
    p.decaySec = 1.0f;
    p.dampingHz = 20000.0f;
    p.preDelayMs = 0.0f;
    const int sr = 48000;
    const Ir ir = impulseResponse(p, 2, sr * 3);
    for (const auto& c : ir.ch)
        for (float v : c) REQUIRE(std::isfinite(v));
    // Energy between 0.2-0.4 s vs 0.7-0.9 s: 0.5 s later should be ~30 dB lower for RT60 = 1 s.
    const double e1 = energy(ir.ch[0], sr / 5, 2 * sr / 5) + energy(ir.ch[1], sr / 5, 2 * sr / 5);
    const double e2 = energy(ir.ch[0], 7 * sr / 10, 9 * sr / 10) + energy(ir.ch[1], 7 * sr / 10, 9 * sr / 10);
    const double dropDb = 10.0 * std::log10(e1 / e2);
    CHECK(dropDb > 22.0);
    CHECK(dropDb < 38.0);
}

TEST_CASE("reverb: outputs are decorrelated") {
    ReverbParams p;
    const Ir ir = impulseResponse(p, 8, 48000);
    for (int a = 0; a < 8; ++a)
        for (int b = a + 1; b < 8; ++b) {
            double sab = 0, saa = 0, sbb = 0;
            for (size_t i = 2400; i < 48000; ++i) {
                sab += static_cast<double>(ir.ch[static_cast<size_t>(a)][i]) * ir.ch[static_cast<size_t>(b)][i];
                saa += static_cast<double>(ir.ch[static_cast<size_t>(a)][i]) * ir.ch[static_cast<size_t>(a)][i];
                sbb += static_cast<double>(ir.ch[static_cast<size_t>(b)][i]) * ir.ch[static_cast<size_t>(b)][i];
            }
            CAPTURE(a);
            CAPTURE(b);
            CHECK(std::fabs(sab / std::sqrt(saa * sbb)) < 0.35);
        }
}

TEST_CASE("reverb: extreme parameters stay stable") {
    for (float decay : {0.0f, 0.2f, 12.0f, 1000.0f, NAN}) {
        for (float size : {0.0f, 0.3f, 2.0f, 50.0f}) {
            ReverbParams p;
            p.decaySec = decay;
            p.size = size;
            p.dampingHz = 1.0e9f;
            p.preDelayMs = 1.0e6f;
            const Ir ir = impulseResponse(p, 3, 48000 * 4);
            double peak = 0;
            for (const auto& c : ir.ch)
                for (float v : c) {
                    REQUIRE(std::isfinite(v));
                    peak = std::max(peak, static_cast<double>(std::fabs(v)));
                }
            CHECK(peak < 2.0);
        }
    }
}

TEST_CASE("reverb: parameter changes while running and garbage input") {
    FdnReverb r;
    r.prepare(44100.0);
    std::vector<float> in(512), out0(512), out1(512);
    float* outs[2] = {out0.data(), out1.data()};
    for (int k = 0; k < 400; ++k) {
        for (size_t i = 0; i < in.size(); ++i) in[i] = (k % 7 == 0) ? NAN : std::sin(static_cast<float>(i + k * 512) * 0.05f);
        ReverbParams p;
        p.size = 0.3f + 0.0042f * static_cast<float>(k);
        p.decaySec = 0.2f + 0.03f * static_cast<float>(k);
        r.setParams(p);
        r.process(in.data(), outs, 2, 512, 1.0f);
        for (float v : out0) REQUIRE(std::isfinite(v));
    }
    FdnReverb unprepared;
    unprepared.process(in.data(), outs, 2, 512, 1.0f); // must not crash
}
