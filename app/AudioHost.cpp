#include "AudioHost.h"

namespace rkapp {

AudioHost::AudioHost() {
    genBuffers_.assign(static_cast<size_t>(kMaxGenerated) * kGenBlock, 0.0f);
    for (int i = 0; i < kMaxGenerated; ++i) {
        testSignal[(size_t)i].store(0);
        pink_[(size_t)i] = rk::PinkNoise(0x9e3779b9u * (uint32_t)(i + 1));
    }
    engine.prepare(48000.0, 512);
}

AudioHost::~AudioHost() { stop(); }

juce::String AudioHost::start(const juce::XmlElement* savedState) {
    juce::String err = devices.initialise(kMaxDeviceInputs, rk::kMaxSpeakers, savedState, true);
    if (err.isNotEmpty()) {
        // Fall back to outputs only (e.g. microphone permission denied).
        err = devices.initialise(0, rk::kMaxSpeakers, nullptr, true);
    }
    devices.addAudioCallback(this);
    return err;
}

void AudioHost::stop() {
    devices.removeAudioCallback(this);
    devices.closeAudioDevice();
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
    constexpr int kTotalInputs = kMaxDeviceInputs + kMaxGenerated;
    const float* in[kTotalInputs];
    float* out[rk::kMaxSpeakers];
    numOutputs = juce::jlimit(0, rk::kMaxSpeakers, numOutputs);
    numInputs = juce::jlimit(0, kMaxDeviceInputs, numInputs);

    for (int off = 0; off < numSamples; off += kGenBlock) {
        const int n = std::min(kGenBlock, numSamples - off);
        for (int i = 0; i < kMaxDeviceInputs; ++i) in[i] = (i < numInputs && inputs != nullptr && inputs[i] != nullptr) ? inputs[i] + off : nullptr;
        for (int s = 0; s < kMaxGenerated; ++s) {
            const int type = testSignal[(size_t)s].load(std::memory_order_relaxed);
            float* buf = &genBuffers_[(size_t)s * kGenBlock];
            if (type != 0) generate(s, type, buf, n);
            in[kGeneratorBase + s] = type != 0 ? buf : nullptr;
        }
        for (int c = 0; c < numOutputs; ++c) out[c] = outputs[c] != nullptr ? outputs[c] + off : nullptr;
        engine.process(in, kTotalInputs, out, numOutputs, n);
    }
}

} // namespace rkapp
