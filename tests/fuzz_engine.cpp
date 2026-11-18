// libFuzzer target: builds arbitrary layouts and parameter sequences from the fuzz input
// and runs the engine. Any crash, sanitizer report, non-finite or >0 dBFS sample fails.
#include <rk/engine.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
struct Reader {
    const uint8_t* d;
    size_t n, pos = 0;
    uint8_t u8() { return pos < n ? d[pos++] : 0; }
    float f32() {
        float v = 0;
        if (pos + 4 <= n) { std::memcpy(&v, d + pos, 4); pos += 4; }
        return v;
    }
    bool more() const { return pos < n; }
};
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    Reader r{data, size};
    static const double rates[] = {8000, 44100, 48000, 96000, 192000, -1, 1e12};
    rk::Engine engine;
    engine.prepare(rates[r.u8() % 7], 1 + r.u8() * 8);

    std::vector<std::vector<float>> in(4, std::vector<float>(700)), out(40, std::vector<float>(700));
    std::vector<const float*> ip;
    std::vector<float*> op;
    for (auto& c : in) ip.push_back(c.data());
    for (auto& c : out) op.push_back(c.data());

    while (r.more()) {
        switch (r.u8() % 6) {
        case 0: { // layout
            rk::EngineConfig c;
            const int n = r.u8() % 40;
            for (int i = 0; i < n; ++i) {
                rk::Speaker s;
                s.position = {r.f32(), r.f32(), r.f32()};
                s.aim = {r.f32(), r.f32(), r.f32()};
                s.gainDb = r.f32();
                s.delayMs = r.f32();
                const uint8_t flags = r.u8();
                s.enabled = flags & 1;
                s.isSubwoofer = flags & 2;
                c.layout.speakers.push_back(s);
            }
            c.panner = static_cast<rk::PannerKind>(r.u8() % 4);
            c.outputMode = static_cast<rk::OutputMode>(r.u8() % 2);
            c.ambiOrder = r.u8();
            c.dbap = {r.f32(), r.f32(), r.f32()};
            engine.setConfig(c);
            break;
        }
        case 1: { // source
            auto& s = engine.source(r.u8());
            s.active = r.u8() & 1;
            s.inputChannel = static_cast<int8_t>(r.u8());
            s.setPosition({r.f32(), r.f32(), r.f32()});
            s.gainDb = r.f32();
            s.spread = r.f32();
            s.reverbSendDb = r.f32();
            s.distanceAmount = r.f32();
            s.touch();
            break;
        }
        case 2: engine.setReverb({r.f32(), r.f32(), r.f32(), r.f32()}); break;
        case 3:
            engine.masterGainDb = r.f32();
            engine.reverbWetDb = r.f32();
            engine.subLevelDb = r.f32();
            engine.limiterEnabled = true;
            break;
        case 4: engine.collectGarbage(); break;
        default: { // audio
            for (auto& c : in)
                for (auto& v : c) v = r.f32();
            const int frames = 1 + (r.u8() * 3) % 700;
            const int nOut = r.u8() % 41;
            engine.process(ip.data(), 4, op.data(), nOut, frames);
            for (int c = 0; c < nOut; ++c)
                for (int i = 0; i < frames; ++i) {
                    const float v = out[static_cast<size_t>(c)][static_cast<size_t>(i)];
                    if (!std::isfinite(v) || std::fabs(v) > 1.0f) std::abort();
                }
            break;
        }
        }
    }
    return 0;
}
