// Host-style stress test for both plugins, without a DAW: bus layouts, random block
// sizes, NaN input, automation from another thread, state round trips, editors.
// pluginval runs in CI as well; this test also runs under the sanitizers.
#include "../room/RoomProcessor.h"
#include "../source/SourceProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <random>
#include <thread>

namespace {
int failures = 0;
#define EXPECT(c)                                                                \
    do {                                                                         \
        if (!(c)) {                                                              \
            ++failures;                                                          \
            std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #c);   \
        }                                                                        \
    } while (0)

void hammer(juce::AudioProcessor& p, int inCh, int outCh, std::mt19937& rng) {
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::discreteChannels(inCh));
    layout.outputBuses.add(juce::AudioChannelSet::discreteChannels(outCh));
    if (!p.checkBusesLayoutSupported(layout)) return;
    EXPECT(p.setBusesLayout(layout));
    const double rates[] = {44100.0, 48000.0, 96000.0};
    const int maxBlock = 32 + int(rng() % 2048);
    const double sr = rates[rng() % 3];
    p.setRateAndBufferSizeDetails(sr, maxBlock);
    p.prepareToPlay(sr, maxBlock);

    std::atomic<bool> stop{false};
    std::thread automation([&] {
        std::mt19937 r2(7);
        while (!stop) {
            auto& params = p.getParameters();
            if (!params.isEmpty())
                params[int(r2() % (unsigned)params.size())]->setValue(std::uniform_real_distribution<float>(0, 1)(r2));
            std::this_thread::yield();
        }
    });

    juce::AudioBuffer<float> buf(juce::jmax(inCh, outCh), maxBlock);
    juce::MidiBuffer midi;
    bool ok = true;
    for (int b = 0; b < 300 && ok; ++b) {
        const int n = 1 + int(rng() % (unsigned)maxBlock);
        buf.setSize(buf.getNumChannels(), n, false, false, true);
        for (int c = 0; c < buf.getNumChannels(); ++c)
            for (int i = 0; i < n; ++i)
                buf.setSample(c, i, rng() % 997 == 0 ? std::numeric_limits<float>::quiet_NaN()
                                                     : std::uniform_real_distribution<float>(-1.5f, 1.5f)(rng));
        p.processBlock(buf, midi);
        for (int c = 0; c < outCh && ok; ++c)
            for (int i = 0; i < n; ++i)
                if (!std::isfinite(buf.getSample(c, i))) { ok = false; break; }
        if (b % 50 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    }
    EXPECT(ok);
    stop = true;
    automation.join();
    p.releaseResources();
}

void stateRoundTrip(juce::AudioProcessor& a, juce::AudioProcessor& b) {
    juce::MemoryBlock m;
    a.getStateInformation(m);
    b.setStateInformation(m.getData(), (int)m.getSize());
    juce::MemoryBlock m2;
    b.getStateInformation(m2);
    EXPECT(m2.getSize() > 0);
    // Corrupted state must be ignored, not crash.
    std::mt19937 rng(3);
    for (int i = 0; i < 200; ++i) {
        juce::MemoryBlock junk(m);
        for (int k = 0; k < 8 && junk.getSize() > 0; ++k) static_cast<char*>(junk.getData())[rng() % junk.getSize()] = (char)rng();
        b.setStateInformation(junk.getData(), (int)junk.getSize());
    }
    b.setStateInformation(nullptr, 0);
}

void editors(juce::AudioProcessor& p) {
    for (int i = 0; i < 3; ++i) {
        std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditorIfNeeded());
        EXPECT(e != nullptr);
        if (e == nullptr) continue;
        e->setSize(800 + i * 300, 500 + i * 200);
        juce::Image img(juce::Image::ARGB, e->getWidth(), e->getHeight(), true);
        juce::Graphics g(img);
        e->paintEntireComponent(g, true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        p.editorBeingDeleted(e.get());
    }
}
} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    std::mt19937 rng(1);
    for (int round = 0; round < 3; ++round) {
        {
            rkplug::SourceProcessor src, src2;
            for (int in : {1, 2})
                for (int out : {1, 2, 8, 12, 24, 64}) hammer(src, in, out, rng);
            stateRoundTrip(src, src2);
            editors(src);
        }
        {
            rkplug::RoomProcessor room, room2;
            for (int ch : {1, 2, 6, 8, 12, 32, 64}) hammer(room, ch, ch, rng);
            stateRoundTrip(room, room2);
            editors(room);
        }
    }
    std::printf("%s (%d failures)\n", failures == 0 ? "PLUGIN STRESS TEST PASSED" : "PLUGIN STRESS TEST FAILED", failures);
    return failures == 0 ? 0 : 1;
}
