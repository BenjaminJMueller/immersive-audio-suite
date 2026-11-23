// Host-style stress test for both plugins, without a DAW: bus layouts, random block
// sizes, NaN input, automation from another thread, state round trips, editors.
// pluginval runs in CI as well; this test also runs under the sanitizers.
#include "../return/ReturnProcessor.h"
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
// The whole chain without a DAW: "Quelle" sends its track to an app-side receiver, the
// DAW output stays silent, and switching the destination to "In der DAW" renders locally.
void bridgeEndToEnd() {
    rk::bridge::Receiver app;
    if (!app.open()) {
        std::printf("bridge: shared memory unavailable, end-to-end test skipped\n");
        return;
    }
    app.heartbeat(48000.0);
    rkplug::SourceProcessor src; // connects to the app in its constructor
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::mono());
    layout.outputBuses.add(juce::AudioChannelSet::discreteChannels(8));
    EXPECT(src.setBusesLayout(layout));
    src.setRateAndBufferSizeDetails(48000.0, 256);
    src.prepareToPlay(48000.0, 256);
    const auto st = src.linkStatus();
    EXPECT(st.appRunning);
    EXPECT(st.slot >= 0);
    if (st.slot < 0) return;

    juce::AudioBuffer<float> buf(8, 256);
    juce::MidiBuffer midi;
    std::vector<float> l(256), r(256);
    double phase = 0.0;
    int heard = 0;
    float dawLeak = 0.0f;
    for (int b = 0; b < 400; ++b) {
        buf.clear();
        for (int i = 0; i < 256; ++i) {
            buf.setSample(0, i, 0.5f * (float)std::sin(phase));
            phase += 2.0 * juce::MathConstants<double>::pi * 440.0 / 48000.0;
        }
        src.processBlock(buf, midi);
        for (int c = 0; c < 8; ++c) dawLeak = std::max(dawLeak, buf.getMagnitude(c, 0, 256));
        app.beginBlock(48000.0);
        float* out[2] = {l.data(), r.data()};
        rk::bridge::TrackParams tp;
        const int ch = app.read(st.slot, out, 256, tp);
        float peak = 0.0f;
        for (float v : l) peak = std::max(peak, std::abs(v));
        if (ch == 1 && peak > 0.4f && peak < 0.6f) ++heard;
        if (b % 100 == 0) app.heartbeat(48000.0);
    }
    EXPECT(dawLeak == 0.0f); // the track plays in the app only, never twice
    EXPECT(heard > 350);
    const auto tracks = app.scan();
    EXPECT(tracks.size() == 1 && tracks[0].sending && tracks[0].channels == 1);
    EXPECT(src.linkStatus().sending);

    // Destination "In der DAW": the plugin renders the speakers itself again.
    auto* route = src.params.getParameter("route");
    route->setValueNotifyingHost(route->convertTo0to1(2.0f));
    float dawPeak = 0.0f;
    for (int b = 0; b < 40; ++b) {
        buf.clear();
        for (int i = 0; i < 256; ++i) buf.setSample(0, i, 0.5f * (float)std::sin(phase += 0.0576));
        src.processBlock(buf, midi);
        for (int c = 0; c < 8; ++c) dawPeak = std::max(dawPeak, buf.getMagnitude(c, 0, 256));
    }
    EXPECT(dawPeak > 0.01f);
    EXPECT(!src.linkStatus().sending);
    std::unique_ptr<juce::AudioProcessorEditor> e(src.createEditorIfNeeded());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    src.editorBeingDeleted(e.get());
}

void returnEndToEnd() {
    rk::bridge::ReturnSender app;
    if (!app.open()) {
        std::printf("return: shared memory unavailable, end-to-end test skipped\n");
        return;
    }
    app.heartbeat(48000.0);
    rkplug::ReturnProcessor ret; // announces itself as listener in its constructor
    EXPECT(app.listening());
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::discreteChannels(4));
    layout.outputBuses.add(juce::AudioChannelSet::discreteChannels(4));
    EXPECT(ret.setBusesLayout(layout));
    ret.setRateAndBufferSizeDetails(48000.0, 256);
    ret.prepareToPlay(48000.0, 256);
    auto* first = ret.params.getParameter("first");
    first->setValueNotifyingHost(first->convertTo0to1(9.0f)); // speakers 9 to 12

    // The app renders 12 speakers; speaker c carries a constant (c + 1) * 0.05.
    std::vector<std::vector<float>> spk(12, std::vector<float>(256));
    std::vector<const float*> ptr;
    for (int c = 0; c < 12; ++c) {
        std::fill(spk[(size_t)c].begin(), spk[(size_t)c].end(), (float)(c + 1) * 0.05f);
        ptr.push_back(spk[(size_t)c].data());
    }
    juce::AudioBuffer<float> buf(4, 256);
    juce::MidiBuffer midi;
    int right = 0;
    for (int b = 0; b < 400; ++b) {
        app.write(ptr.data(), 12, 256);
        for (int c = 0; c < 4; ++c) buf.clear();
        buf.setSample(0, 0, 0.9f); // the track's own input must be replaced
        ret.processBlock(buf, midi);
        bool ok = true;
        for (int c = 0; c < 4; ++c)
            ok = ok && std::abs(buf.getSample(c, 200) - (float)(9 + c) * 0.05f) < 1.0e-4f;
        if (ok) ++right;
    }
    EXPECT(right > 350);
    const auto st = ret.status();
    EXPECT(st.appRunning && st.receiving && st.appChannels == 12 && st.firstChannel == 9);
    std::unique_ptr<juce::AudioProcessorEditor> e(ret.createEditorIfNeeded());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    ret.editorBeingDeleted(e.get());
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
            rkplug::ReturnProcessor ret, ret2;
            for (int ch : {1, 2, 8, 12, 16, 64}) hammer(ret, ch, ch, rng);
            stateRoundTrip(ret, ret2);
            editors(ret);
        }
    }
    bridgeEndToEnd();
    returnEndToEnd();
    std::printf("%s (%d failures)\n", failures == 0 ? "PLUGIN STRESS TEST PASSED" : "PLUGIN STRESS TEST FAILED", failures);
    return failures == 0 ? 0 : 1;
}
