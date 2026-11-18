#pragma once

#include <rk/engine.hpp>

#include <cmath>
#include <vector>

namespace rktest {

/// Owns input/output buffers and runs an engine block by block.
struct Rig {
    rk::Engine engine;
    int numIn;
    int numOut;
    std::vector<std::vector<float>> in, out;
    std::vector<const float*> inPtr;
    std::vector<float*> outPtr;

    Rig(int inputs, int outputs, int maxBlock = 512, double sr = 48000.0) : numIn(inputs), numOut(outputs) {
        engine.prepare(sr, maxBlock);
        resize(4096);
    }

    void resize(int frames) {
        in.assign(static_cast<size_t>(numIn), std::vector<float>(static_cast<size_t>(frames), 0.0f));
        out.assign(static_cast<size_t>(numOut), std::vector<float>(static_cast<size_t>(frames), 0.0f));
        inPtr.clear();
        outPtr.clear();
        for (auto& c : in) inPtr.push_back(c.data());
        for (auto& c : out) outPtr.push_back(c.data());
    }

    void run(int frames) {
        if (frames > static_cast<int>(out.empty() ? 0 : out[0].size())) resize(frames);
        engine.process(inPtr.data(), numIn, outPtr.data(), numOut, frames);
    }

    void noReverb() {
        engine.reverbWetDb = -80.0f;
        for (int s = 0; s < rk::kMaxSources; ++s) engine.source(s).reverbSendDb = -80.0f;
    }

    void setLayout(const rk::SpeakerLayout& l, rk::PannerKind k, rk::OutputMode mode = rk::OutputMode::Speakers) {
        rk::EngineConfig c;
        c.layout = l;
        c.panner = k;
        c.outputMode = mode;
        engine.setConfig(c);
    }

    /// Runs long enough that layout fades and gain ramps are complete.
    void settle() {
        for (int i = 0; i < 20; ++i) {
            run(256);
            engine.collectGarbage();
        }
    }

    double rms(int ch, int frames) const {
        double e = 0;
        for (int i = 0; i < frames; ++i) e += static_cast<double>(out[static_cast<size_t>(ch)][static_cast<size_t>(i)]) * out[static_cast<size_t>(ch)][static_cast<size_t>(i)];
        return std::sqrt(e / frames);
    }
};

inline void fillSine(std::vector<float>& v, float freq, float amp, double sr, long long& phase) {
    for (auto& x : v) {
        x = amp * static_cast<float>(std::sin(2.0 * 3.14159265358979 * freq * static_cast<double>(phase) / sr));
        ++phase;
    }
}

} // namespace rktest
