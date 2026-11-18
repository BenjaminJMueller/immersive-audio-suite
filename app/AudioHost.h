// Raumklang app: owns the audio device and the engine. Device inputs and the built-in
// test signals feed the sources, the engine output goes straight to the device outputs.
// No virtual audio cables or routing tools are needed.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <rk/dsp.hpp>
#include <rk/engine.hpp>

#include <array>
#include <atomic>

namespace rkapp {

class AudioHost : public juce::AudioIODeviceCallback {
public:
    static constexpr int kMaxDeviceInputs = 64;
    static constexpr int kMaxGenerated = 64; // one generator per source
    static constexpr int kGeneratorBase = kMaxDeviceInputs;

    AudioHost();
    ~AudioHost() override;

    /// Opens the default device (or the one stored in `savedState`). Returns an error text or empty.
    juce::String start(const juce::XmlElement* savedState);
    void stop();

    rk::Engine engine;
    juce::AudioDeviceManager devices;
    /// Test signal per source: 0 off, 1 pink noise, 2 sine, 3 clicks.
    std::array<std::atomic<int>, kMaxGenerated> testSignal{};

    int numDeviceOutputs() const noexcept { return numOutputs_.load(); }
    int numDeviceInputs() const noexcept { return numInputs_.load(); }
    double sampleRate() const noexcept { return sampleRate_.load(); }
    juce::String takeError();

    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs,
                                          int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& message) override;

private:
    static constexpr int kGenBlock = 2048;
    void generate(int source, int type, float* out, int n) noexcept;

    std::vector<float> genBuffers_; // kMaxGenerated x kGenBlock
    std::array<rk::PinkNoise, kMaxGenerated> pink_;
    std::array<double, kMaxGenerated> phase_{};
    std::array<long long, kMaxGenerated> clickCounter_{};
    std::atomic<int> numOutputs_{0}, numInputs_{0};
    std::atomic<double> sampleRate_{48000.0};
    juce::CriticalSection errorLock_;
    juce::String error_;
};

} // namespace rkapp
