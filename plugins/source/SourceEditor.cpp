#include "SourceEditor.h"

using rkui::tr;
using rkui::u8;

namespace rkplug {

juce::AudioProcessorEditor* SourceProcessor::createEditor() { return new SourceEditor(*this); }

SourceEditor::SourceEditor(SourceProcessor& p) : AudioProcessorEditor(p), proc_(p) {
    rkui::setDarkMode(true);
    lnf_.refreshColours();
    setLookAndFeel(&lnf_);
    stage_.setEditTarget(rkui::StageView::EditTarget::Sources);
    stage_.setSelectedSource(0);
    stage_.onSourceMoved = [this](int, rk::Vec3 pos) {
        if (!dragGesture_) {
            for (auto* id : {"azimuth", "elevation", "distance"})
                if (auto* prm = proc_.params.getParameter(id)) prm->beginChangeGesture();
            dragGesture_ = true;
        }
        proc_.setSourcePosition({pos.x, pos.y, proc_.sourcePosition().z});
    };
    stage_.onDragEnd = [this] {
        if (!dragGesture_) return;
        for (auto* id : {"azimuth", "elevation", "distance"})
            if (auto* prm = proc_.params.getParameter(id)) prm->endChangeGesture();
        dragGesture_ = false;
    };
    addAndMakeVisible(stage_);
    addAndMakeVisible(card_);

    addSlider("azimuth", tr("Azimut"), juce::String(juce::CharPointer_UTF8("\xc2\xb0")), 1, 0);
    addSlider("elevation", tr("Elevation"), juce::String(juce::CharPointer_UTF8("\xc2\xb0")), 1, 0);
    addSlider("distance", tr("Abstand"), "m", 2, 0);
    addSlider("gain", tr("Pegel"), "dB", 1, 0);
    addSlider("spread", tr("Breite"), "%", 0, 0);
    addSlider("stereoWidth", u8("Stereobreite"), juce::String(juce::CharPointer_UTF8("\xc2\xb0")), 0, 0);
    addSlider("reverbSend", tr("Hallanteil"), "dB", 1, -80);
    addSlider("distanceAmount", tr("Entfernung"), "%", 0, 0);

    setResizable(true, true);
    setResizeLimits(720, 460, 2000, 1400);
    setSize(940, 560);
    refreshStage();
    startTimerHz(30);
}

SourceEditor::~SourceEditor() {
    stopTimer();
    bindings_.clear();
    setLookAndFeel(nullptr);
}

rkui::ParamSlider& SourceEditor::addSlider(const char* id, const juce::String& label, const juce::String& unit, int decimals, double origin) {
    auto* prm = proc_.params.getParameter(id);
    const auto range = prm->getNormalisableRange();
    auto s = std::make_unique<rkui::ParamSlider>(label, range.start, range.end, range.interval, unit, decimals, origin);
    if (std::string(id) == "distance") s->setSkewForCentre(5.0);
    addAndMakeVisible(*s);
    bindings_.push_back(std::make_unique<ParamBinding>(*s, *prm));
    sliders_.push_back(std::move(s));
    return *sliders_.back();
}

void SourceEditor::refreshStage() {
    const rkui::Scene setup = proc_.setupSnapshot();
    const juce::String json = rkui::sceneToJson(setup);
    if (json != shownSetupJson_) {
        shownSetupJson_ = json;
        shown_ = setup;
        const int need = (int)setup.layout.speakers.size();
        const int have = proc_.outputChannels();
        if (have < need)
            card_.set(rkui::StatusCard::Kind::Warn, tr("Bitte prüfen"),
                      juce::String(have) + " / " + juce::String(need) + " " + tr("Ausgang") + ". " +
                          (rkui::language() == rkui::Language::German
                               ? u8("Stelle die Spur in der DAW auf ") + juce::String(need) + u8(" Kan\xc3\xa4le, sonst bleiben Lautsprecher stumm.")
                               : "Set the DAW track to " + juce::String(need) + " channels, otherwise speakers stay silent."));
        else
            card_.set(rkui::StatusCard::Kind::Ok, tr("Sitzt gut") + u8(" \xc2\xb7 ") + juce::String(setup.layout.name),
                      juce::String(need) + " " + tr("Lautsprecher") + u8(" \xc2\xb7 ") + juce::String(rk::pannerId(setup.effectivePanner())).toUpperCase() +
                          (rkui::language() == rkui::Language::German ? u8(". Das Layout kommt aus der Raumklang-App oder dem Plugin \xe2\x80\x9eRaumklang Raum\xe2\x80\x9c.")
                                                                      : juce::String(". The layout comes from the Raumklang app or the Raumklang Room plugin.")));
    }
    shown_.sources.resize(1);
    shown_.sources[0].name = "Quelle";
    shown_.sources[0].position = proc_.sourcePosition();
    shown_.sources[0].spread = proc_.params.getRawParameterValue("spread")->load() * 0.01f;
    stage_.setScene(shown_);
}

void SourceEditor::timerCallback() {
    for (auto& b : bindings_) b->refresh();
    refreshStage();
    levels_.resize(shown_.layout.speakers.size());
    for (size_t c = 0; c < levels_.size(); ++c) {
        const float db = rk::gainToDb(proc_.takeOutputPeak((int)c));
        levels_[c] = std::max(juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f), levels_[c] * 0.85f);
    }
    stage_.setSpeakerLevels(levels_);
}

void SourceEditor::paint(juce::Graphics& g) {
    const auto& p = rkui::palette();
    g.fillAll(p.side);
    g.setColour(p.bar);
    g.fillRect(0, 0, getWidth(), 44);
    g.setColour(p.text);
    g.setFont(rkui::uiFont(15.0f, true));
    g.drawText("Raumklang Quelle", 16, 0, 300, 44, juce::Justification::centredLeft);
    g.setColour(p.text3);
    g.setFont(rkui::uiFont(12.0f));
    g.drawText(rkui::language() == rkui::Language::German ? u8("Quelle ziehen oder Regler nutzen. Alles ist automatisierbar.")
                                                          : u8("Drag the source or use the controls. Everything can be automated."),
               getLocalBounds().removeFromTop(44).reduced(16, 0), juce::Justification::centredRight);
}

void SourceEditor::resized() {
    auto r = getLocalBounds();
    r.removeFromTop(44);
    auto right = r.removeFromRight(juce::jmax(320, getWidth() / 3)).reduced(12, 10);
    stage_.setBounds(r);
    card_.setBounds(right.removeFromTop(card_.idealHeight(right.getWidth())));
    right.removeFromTop(8);
    for (auto& s : sliders_) s->setBounds(right.removeFromTop(34));
}

} // namespace rkplug
