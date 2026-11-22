// Renders the plugin windows to PNG files (2x, like a Retina screen) for the README and
// for reviewing the design. Not part of the test run: `rk_plugin_shots <folder>`.
#include "../return/ReturnEditor.h"
#include "../return/ReturnProcessor.h"
#include "../source/SourceEditor.h"
#include "../source/SourceProcessor.h"

#include <rk/bridge.hpp>
#include <rk_ui/Scene.h>

namespace {

void save(juce::Component& c, const juce::File& file) {
    const auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 2.0f);
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat().writeImageToStream(img, out);
    std::printf("wrote %s\n", file.getFullPathName().toRawUTF8());
}

void pump(int ms) {
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32)ms;
    while (juce::Time::getMillisecondCounter() < end) juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}

/// Feeds a few blocks of a steady tone, so the meters show something.
void play(juce::AudioProcessor& p, int blocks) {
    p.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> buf(std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b) {
        buf.clear();
        for (int c = 0; c < p.getTotalNumInputChannels(); ++c)
            for (int i = 0; i < 512; ++i) buf.setSample(c, i, 0.35f * std::sin(0.05f * (float)(b * 512 + i)));
        p.processBlock(buf, midi);
    }
}

void setParam(juce::AudioProcessorValueTreeState& s, const char* id, float v) {
    if (auto* prm = s.getParameter(id)) prm->setValueNotifyingHost(prm->convertTo0to1(v));
}

} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]) : juce::File::getCurrentWorkingDirectory();
    dir.createDirectory();

    // The donut from the README: 4 speakers inside facing out, 8 outside facing in.
    rkui::Scene room;
    room.presetId = "donut";
    room.innerCount = 4;
    room.innerRadius = 1.0f;
    room.outerCount = 8;
    room.outerRadius = 5.0f;
    rkui::regenerateLayout(room);
    rkui::writeSharedSetup(room);

    // Three other tracks, as other plugin instances would announce them.
    struct Other { const char* name; float az, el, dist; };
    const Other others[] = {{"Bass", -10.0f, 0.0f, 2.2f}, {"Gitarre", -55.0f, 5.0f, 3.0f}, {"Synth", 165.0f, 20.0f, 4.0f}};
    std::vector<std::unique_ptr<rk::bridge::Sender>> senders;
    for (const auto& o : others) {
        auto s = std::make_unique<rk::bridge::Sender>();
        if (!s->open() || s->claim() < 0) continue;
        s->heartbeat(o.name);
        float silence[64] = {};
        const float* ch[] = {silence};
        rk::bridge::TrackParams tp;
        tp.azimuthDeg = o.az;
        tp.elevationDeg = o.el;
        tp.distance = o.dist;
        s->write(ch, 1, 64, 48000.0, tp);
        senders.push_back(std::move(s));
    }

    rkui::setLanguage(rkui::Language::German);
    for (int mode : {2, 3, 0}) {
        rkplug::SourceProcessor proc;
        proc.setCustomName("Gesang");
        setParam(proc.params, "azimuth", 50.0f);
        setParam(proc.params, "elevation", 20.0f);
        setParam(proc.params, "distance", 3.2f);
        setParam(proc.params, "reverbSend", -10.0f);
        proc.params.state.setProperty("view", mode, nullptr);
        play(proc, 20);
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        ed->setSize(1100, 680);
        juce::Component::SafePointer<juce::Component> safe(ed.get());
        for (int k = 0; k < 40; ++k) { play(proc, 2); pump(20); }
        const char* names[] = {"quelle-oben.png", "", "quelle-geteilt.png", "quelle-3d.png"};
        save(*ed, dir.getChildFile(names[mode]));
        ed.reset();
    }
    for (auto& s : senders) s->release();
    return 0;
}
