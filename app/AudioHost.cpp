#include "AudioHost.h"
#include <rk_ui/Strings.h>

namespace rkapp {

AudioHost::AudioHost() {
    genBuffers_.assign(static_cast<size_t>(kMaxGenerated) * kGenBlock, 0.0f);
    bridgeBuffers_.assign(static_cast<size_t>(rk::bridge::kSlots) * 2 * kGenBlock, 0.0f);
    receiver_.open(); // may fail (e.g. sandbox): the app then simply works without DAW tracks
    returnSender_.open();
    returnBuffers_.assign(static_cast<size_t>(rk::kMaxSpeakers) * kGenBlock, 0.0f);
    for (int c = 0; c < rk::kMaxSpeakers; ++c) route_[(size_t)c].store(c);
    for (int i = 0; i < kMaxGenerated; ++i) {
        testSignal[(size_t)i].store(0);
        pink_[(size_t)i] = rk::PinkNoise(0x9e3779b9u * (uint32_t)(i + 1));
    }
    engine.prepare(48000.0, 512);
}

AudioHost::~AudioHost() { stop(); }

juce::String AudioHost::start(const juce::XmlElement* savedState, bool withInputs) {
    inputsEnabled_ = withInputs;
    std::unique_ptr<juce::XmlElement> state;
    if (savedState != nullptr) {
        state = std::make_unique<juce::XmlElement>(*savedState);
        if (!withInputs) {
            state->setAttribute("audioInputDeviceName", juce::String());
            state->setAttribute("audioDeviceInChans", "0");
        }
    }
    juce::String err = devices.initialise(withInputs ? kMaxDeviceInputs : 0, rk::kMaxSpeakers, state.get(), true);
    if (err.isNotEmpty()) {
        // Fall back to outputs only (e.g. microphone permission denied).
        inputsEnabled_ = false;
        err = devices.initialise(0, rk::kMaxSpeakers, nullptr, true);
    }
    devices.addAudioCallback(this);
    return err;
}

juce::String AudioHost::setInputsEnabled(bool on) {
    if (on == inputsEnabled_) return {};
    auto setup = devices.getAudioDeviceSetup();
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    if (on) {
        if (setup.inputDeviceName.isEmpty()) {
            auto* type = devices.getCurrentDeviceTypeObject();
            if (type != nullptr && type->hasSeparateInputsAndOutputs()) {
                // Prefer the input side of the same interface, else the system default input.
                const auto names = type->getDeviceNames(true);
                setup.inputDeviceName = names.contains(setup.outputDeviceName)
                                            ? setup.outputDeviceName
                                            : names[juce::jmax(0, type->getDefaultDeviceIndex(true))];
            } else {
                setup.inputDeviceName = setup.outputDeviceName;
            }
        }
        setup.inputChannels.setRange(0, kMaxDeviceInputs, true);
    } else {
        setup.inputDeviceName = {};
    }
    const juce::String err = devices.setAudioDeviceSetup(setup, true);
    inputsEnabled_ = on && err.isEmpty();
    return err;
}

void AudioHost::stop() {
    if (simThread_.joinable()) {
        simStop_.store(true);
        simThread_.join();
    }
    devices.removeAudioCallback(this);
    devices.closeAudioDevice();
}

void AudioHost::startSimulation(const rk::SpeakerLayout& realRoom) {
    if (simThread_.joinable()) return;
    devices.removeAudioCallback(this);
    devices.closeAudioDevice();
    std::vector<rk::RoomSimulator::Speaker> spk;
    for (const auto& s : realRoom.speakers) spk.push_back({s.position, s.aimDirection()});
    constexpr double sr = 48000.0;
    constexpr int block = 512;
    room_.prepare(sr, spk, 0.0113, -1.2f, 3.0e-5f, 343.0f);
    room_.setMicrophone({0.0f, 0.0f, 0.0f});
    sampleRate_.store(sr);
    numOutputs_.store((int)spk.size());
    numInputs_.store(1);
    engine.prepare(sr, block);
    simStop_.store(false);
    simThread_ = std::thread([this, n = (int)spk.size()] {
        std::vector<std::vector<float>> outBufs((size_t)n, std::vector<float>(block, 0.0f));
        std::vector<float*> outs;
        for (auto& b : outBufs) outs.push_back(b.data());
        std::vector<float> mic(block, 0.0f);
        const float* ins[1] = {mic.data()};
        juce::AudioIODeviceCallbackContext ctx;
        auto next = std::chrono::steady_clock::now();
        // Runs four times faster than real time: the calibration does not depend on the clock.
        const auto period = std::chrono::microseconds((long long)(1.0e6 * block / sr / 4.0));
        while (!simStop_.load()) {
            audioDeviceIOCallbackWithContext(ins, 1, outs.data(), n, block, ctx);
            room_.process(outs.data(), n, mic.data(), block);
            next += period;
            std::this_thread::sleep_until(next);
        }
    });
}

void AudioHost::setRouting(const std::vector<int>& speakerOutputs) noexcept {
    bool identity = true;
    for (int c = 0; c < rk::kMaxSpeakers; ++c) {
        const int o = c < (int)speakerOutputs.size() ? juce::jlimit(-1, rk::kMaxSpeakers - 1, speakerOutputs[(size_t)c]) : c;
        identity = identity && o == c;
        route_[(size_t)c].store(o, std::memory_order_relaxed);
    }
    routeIdentity_.store(identity, std::memory_order_release);
}

std::vector<rk::bridge::TrackInfo> AudioHost::scanBridge() {
    // Opened once in the constructor only: the audio thread reads the receiver without locks.
    if (!bridgeEnabled.load()) return {};
    if (returnSender_.isOpen()) returnSender_.heartbeat(sampleRate_.load());
    if (!receiver_.isOpen()) return {};
    receiver_.heartbeat(sampleRate_.load());
    return receiver_.scan();
}

rk::bridge::TrackParams AudioHost::bridgeParams(int slot) const noexcept {
    rk::bridge::TrackParams p;
    if (slot < 0 || slot >= rk::bridge::kSlots) return p;
    const auto& a = bridgeParams_[(size_t)slot];
    p.azimuthDeg = a.az.load(std::memory_order_relaxed);
    p.elevationDeg = a.el.load(std::memory_order_relaxed);
    p.distance = a.dist.load(std::memory_order_relaxed);
    p.spread = a.spread.load(std::memory_order_relaxed);
    p.gainDb = a.gain.load(std::memory_order_relaxed);
    p.reverbSendDb = a.send.load(std::memory_order_relaxed);
    p.stereoWidthDeg = a.width.load(std::memory_order_relaxed);
    p.distanceAmount = a.amount.load(std::memory_order_relaxed);
    return p;
}

void AudioHost::readScope(float* dst, int n) const noexcept {
    n = juce::jlimit(0, kScopeSize, n);
    const uint32_t end = scopeWrite_.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        dst[i] = scope_[(end - (uint32_t)n + (uint32_t)i) % kScopeSize].load(std::memory_order_relaxed);
}

juce::StringArray AudioHost::inputNames() {
    juce::StringArray out;
    if (simThread_.joinable()) return {"Messmikrofon (Testraum)"};
    if (auto* d = devices.getCurrentAudioDevice()) {
        const auto names = d->getInputChannelNames();
        const auto active = d->getActiveInputChannels();
        for (int i = 0; i < names.size(); ++i)
            if (active[i]) out.add(names[i]);
    }
    return out;
}

juce::String AudioHost::takeError() {
    const juce::ScopedLock sl(errorLock_);
    juce::String e = error_;
    error_.clear();
    return e;
}

void AudioHost::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    const double sr = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
    const int block = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
    sampleRate_.store(sr);
    numOutputs_.store(device != nullptr ? device->getActiveOutputChannels().countNumberOfSetBits() : 0);
    numInputs_.store(device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : 0);
    engine.prepare(sr, juce::jlimit(64, 4096, block));
}

juce::String AudioHost::deviceSummary() {
    auto* d = devices.getCurrentAudioDevice();
    if (d == nullptr || !d->isOpen()) return {};
    const double sr = d->getCurrentSampleRate();
    if (sr <= 0.0) return {};
    const int samples = d->getCurrentBufferSizeSamples() + d->getOutputLatencyInSamples();
    const juce::String dot = juce::String(", ");
    const juce::String rate = std::abs(sr - std::round(sr / 1000.0) * 1000.0) < 1.0
                                  ? juce::String(juce::roundToInt(sr / 1000.0))
                                  : juce::String(sr / 1000.0, 1).replaceCharacter('.', ',');
    return rate + " kHz, " + juce::String(1000.0 * samples / sr, 1).replaceCharacter('.', ',') + " ms " + rkui::tr("Latenz");
}

void AudioHost::audioDeviceStopped() {}

void AudioHost::audioDeviceError(const juce::String& message) {
    const juce::ScopedLock sl(errorLock_);
    error_ = message;
}

void AudioHost::generate(int s, int type, float* out, int n) noexcept {
    const double sr = sampleRate_.load(std::memory_order_relaxed);
    switch (type) {
    case 1: {
        auto& p = pink_[(size_t)s];
        for (int i = 0; i < n; ++i) out[i] = p.next();
        break;
    }
    case 2: { // sine, a different pitch per source so they can be told apart
        const double f = 220.0 * std::pow(2.0, (s % 12) / 12.0 * 7.0 / 7.0) * (1 + (s / 12) % 3);
        double ph = phase_[(size_t)s];
        const double inc = 2.0 * juce::MathConstants<double>::pi * f / sr;
        for (int i = 0; i < n; ++i) {
            out[i] = 0.25f * (float)std::sin(ph);
            ph += inc;
        }
        phase_[(size_t)s] = std::fmod(ph, 2.0 * juce::MathConstants<double>::pi);
        break;
    }
    case 3: { // short noise bursts, 2 per second, staggered between sources
        const long long period = (long long)(sr * 0.5);
        const long long burst = (long long)(sr * 0.012);
        long long c = clickCounter_[(size_t)s];
        auto& p = pink_[(size_t)s];
        for (int i = 0; i < n; ++i) {
            const long long pos = (c + (long long)s * period / 7) % period;
            out[i] = pos < burst ? p.next() * 3.0f * (1.0f - (float)pos / (float)burst) : 0.0f;
            ++c;
        }
        clickCounter_[(size_t)s] = c % (period * 1000);
        break;
    }
    default: std::fill(out, out + n, 0.0f); break;
    }
}

void AudioHost::audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs,
                                                 int numOutputs, int numSamples,
                                                 const juce::AudioIODeviceCallbackContext&) {
    const float* in[kTotalInputs];
    float* out[rk::kMaxSpeakers];
    numOutputs = juce::jlimit(0, rk::kMaxSpeakers, numOutputs);
    numInputs = juce::jlimit(0, kMaxDeviceInputs, numInputs);
    const bool bridge = receiver_.isOpen() && bridgeEnabled.load(std::memory_order_relaxed);
    if (bridge) receiver_.beginBlock(sampleRate_.load(std::memory_order_relaxed));
    // While a return plugin listens, every speaker is rendered (also those the audio
    // device has no output for), so the DAW can record the complete speaker mix.
    const bool toDaw = returnSender_.isOpen() && bridgeEnabled.load(std::memory_order_relaxed) && returnSender_.listening();

    for (int off = 0; off < numSamples; off += kGenBlock) {
        const int n = std::min(kGenBlock, numSamples - off);
        for (int i = 0; i < kMaxDeviceInputs; ++i) in[i] = (i < numInputs && inputs != nullptr && inputs[i] != nullptr) ? inputs[i] + off : nullptr;
        for (int s = 0; s < kMaxGenerated; ++s) {
            const int type = testSignal[(size_t)s].load(std::memory_order_relaxed);
            float* buf = &genBuffers_[(size_t)s * kGenBlock];
            if (type != 0) generate(s, type, buf, n);
            in[kGeneratorBase + s] = type != 0 ? buf : nullptr;
        }
        for (int k = 0; k < rk::bridge::kSlots; ++k) {
            float* l = &bridgeBuffers_[((size_t)k * 2) * kGenBlock];
            float* r = &bridgeBuffers_[((size_t)k * 2 + 1) * kGenBlock];
            int ch = 0;
            rk::bridge::TrackParams tp;
            if (bridge) {
                float* both[2] = {l, r};
                ch = receiver_.read(k, both, n, tp);
            }
            in[kBridgeInputBase + 2 * k] = ch > 0 ? l : nullptr;
            in[kBridgeInputBase + 2 * k + 1] = ch > 1 ? r : nullptr;
            rk::bridge::applyTrack(engine, kBridgeSourceBase + 2 * k, kBridgeInputBase + 2 * k, ch > 0 ? ch : 1, tp, ch > 0);
            if (ch > 0) {
                auto& a = bridgeParams_[(size_t)k];
                a.az.store(tp.azimuthDeg, std::memory_order_relaxed);
                a.el.store(tp.elevationDeg, std::memory_order_relaxed);
                a.dist.store(tp.distance, std::memory_order_relaxed);
                a.spread.store(tp.spread, std::memory_order_relaxed);
                a.gain.store(tp.gainDb, std::memory_order_relaxed);
                a.send.store(tp.reverbSendDb, std::memory_order_relaxed);
                a.width.store(tp.stereoWidthDeg, std::memory_order_relaxed);
                a.amount.store(tp.distanceAmount, std::memory_order_relaxed);
            }
        }
        // Measuring: calibration.process() replaces the engine output (silence + sweep on one
        // speaker) and records the mic, before the signals go to the device or back to the DAW.
        const bool identity = routeIdentity_.load(std::memory_order_relaxed);
        if (toDaw || !identity) {
            // Speakers first into their own buffers, then to the DAW and through the routing.
            const int spk = juce::jlimit(1, rk::kMaxSpeakers, engine.numOutputChannels());
            for (int c = 0; c < spk; ++c) out[c] = &returnBuffers_[(size_t)c * kGenBlock];
            engine.process(in, kTotalInputs, out, spk, n);
            calibration.process(in, numInputs, out, spk, n);
            if (toDaw) returnSender_.write(out, std::min(spk, rk::bridge::kReturnChannels), n);
            for (int c = 0; c < numOutputs; ++c)
                if (outputs[c] != nullptr) std::fill(outputs[c] + off, outputs[c] + off + n, 0.0f);
            for (int c = 0; c < spk; ++c) {
                const int o = identity ? c : route_[(size_t)c].load(std::memory_order_relaxed);
                if (o < 0 || o >= numOutputs || outputs[o] == nullptr) continue;
                float* dst = outputs[o] + off;
                const float* src = out[c];
                for (int i = 0; i < n; ++i) dst[i] += src[i];
            }
            for (int c = 0; c < numOutputs; ++c) out[c] = outputs[c] != nullptr ? outputs[c] + off : nullptr;
        } else {
            for (int c = 0; c < numOutputs; ++c) out[c] = outputs[c] != nullptr ? outputs[c] + off : nullptr;
            engine.process(in, kTotalInputs, out, numOutputs, n);
            calibration.process(in, numInputs, out, numOutputs, n);
        }

        // Summed output for the visualisation (lock-free ring, newest samples win).
        uint32_t w = scopeWrite_.load(std::memory_order_relaxed);
        const float norm = numOutputs > 0 ? 1.0f / std::sqrt((float)numOutputs) : 0.0f;
        for (int i = 0; i < n; ++i) {
            float sum = 0.0f;
            for (int c = 0; c < numOutputs; ++c)
                if (out[c] != nullptr) sum += out[c][i];
            scope_[w % kScopeSize].store(sum * norm, std::memory_order_relaxed);
            ++w;
        }
        scopeWrite_.store(w, std::memory_order_release);
    }
}

} // namespace rkapp
