// "Raumklang Return" (Rückweg): brings the speaker signals the Raumklang app renders back
// into the DAW, one channel per speaker, for recording and real-time bounces.
// Goes on an (aux or audio) track with as many channels as should come back; the
// track's own input is replaced. Several instances can each take a range of channels.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <rk/bridge.hpp>

#include <array>
#include <atomic>

namespace rkplug {

class ReturnProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    ReturnProcessor();
    ~ReturnProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return juce::String("Raumklang Return"); }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    // Long tail: the audio comes from the app, not from the track's input. Hosts such
    // as Logic stop processing plugins whose input is silent once the tail has passed.
    double getTailLengthSeconds() const override { return 3600.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    juce::AudioProcessorValueTreeState params;

    struct Status {
        bool available = false;     // shared memory could be opened (not blocked by a sandbox)
        bool appRunning = false;
        bool rateMismatch = false;
        bool receiving = false;     // audio arrived in the last second
        bool offline = false;       // the host rendered offline recently
        bool processing = false;    // the host called the plugin in the last half second
        double sampleRate = 0.0, appSampleRate = 0.0;
        int appChannels = 0;        // speakers the app renders
        int trackChannels = 0;      // channels of this track
        int firstChannel = 1;       // 1-based
        uint32_t dropouts = 0;      // since the editor last asked
    };
    /// Message thread.
    Status status();
    /// Any thread: peak of output channel `ch` since the last call.
    float takePeak(int ch) noexcept;

private:
    void timerCallback() override;

    rk::bridge::ReturnReceiver receiver_;
    std::atomic<float>* first_ = nullptr;
    std::atomic<double> sampleRate_{0.0};
    std::atomic<int> channels_{2};
    std::atomic<int64_t> lastAudioMs_{0}, lastOfflineMs_{0}, lastProcessMs_{0};
    std::atomic<uint32_t> dropouts_{0};
    std::array<std::atomic<float>, rk::bridge::kReturnChannels> peaks_{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReturnProcessor)
};

} // namespace rkplug
