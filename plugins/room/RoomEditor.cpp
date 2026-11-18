#include "RoomEditor.h"

using rkui::tr;
using rkui::u8;

namespace rkplug {

juce::AudioProcessorEditor* RoomProcessor::createEditor() { return new RoomEditor(*this); }

RoomEditor::RoomEditor(RoomProcessor& p)
    : AudioProcessorEditor(p), proc_(p), tabs_(juce::StringArray{tr("Lautsprecher"), tr("Raum"), tr("Ausgabe")}, true) {
    rkui::setDarkMode(true);
    lnf_.refreshColours();
    setLookAndFeel(&lnf_);
    tabs_.onChange = [this](int i) { tab_ = i; rebuild(); };
    addAndMakeVisible(tabs_);
    stage_.setEditTarget(rkui::StageView::EditTarget::Speakers);
    stage_.onSpeakerSelected = [this](int i) { selSpeaker_ = i; rebuild(); };
    stage_.onSpeakerMoved = [this](int i, rk::Vec3 pos) {
        auto& sp = proc_.scene().layout.speakers;
        if (i < (int)sp.size()) {
            sp[(size_t)i].position = pos;
            proc_.scene().presetId = "custom";
            proc_.sceneChanged(false);
        }
    };
    stage_.onDragEnd = [this] { proc_.sceneChanged(true); rebuild(); };
    addAndMakeVisible(stage_);
    addAndMakeVisible(page_);
    setResizable(true, true);
    setResizeLimits(760, 480, 2400, 1600);
    setSize(1000, 620);
    rebuild();
    startTimerHz(30);
}

RoomEditor::~RoomEditor() {
    stopTimer();
    proc_.speakers.speakerTest = -1;
    setLookAndFeel(nullptr);
}

rkui::PageContext RoomEditor::context() {
    rkui::PageContext c;
    c.scene = &proc_.scene();
    c.changed = [this](bool) {
        proc_.sceneChanged(false);
        stage_.setScene(proc_.scene());
    };
    c.commit = [this] { proc_.sceneChanged(true); };
    c.rebuild = [this] {
        juce::MessageManager::callAsync([sp = juce::Component::SafePointer<RoomEditor>(this)] { if (sp) sp->rebuild(); });
    };
    return c;
}

void RoomEditor::rebuild() {
    stage_.setScene(proc_.scene());
    stage_.setSelectedSpeaker(selSpeaker_);
    if (tab_ == 0)
        rkui::buildLayoutPage(page_, context(), selSpeaker_, [this](int i) {
            juce::MessageManager::callAsync([sp = juce::Component::SafePointer<RoomEditor>(this), i] { if (sp) { sp->selSpeaker_ = i; sp->rebuild(); } });
        });
    else if (tab_ == 1)
        rkui::buildRoomPage(page_, context());
    else
        buildOutputPage();
}

void RoomEditor::buildOutputPage() {
    page_.clear();
    const int need = (int)proc_.scene().layout.speakers.size();
    const int have = proc_.channels();
    auto card = std::make_unique<rkui::StatusCard>();
    if (have < need)
        card->set(rkui::StatusCard::Kind::Warn, tr("Bitte prüfen"),
                  juce::String(have) + " / " + juce::String(need) + " " + tr("Ausgang") + ". " +
                      (rkui::language() == rkui::Language::German ? u8("Stelle den Master-Bus auf ") + juce::String(need) + u8(" Kan\xc3\xa4le.")
                                                                  : "Set the master bus to " + juce::String(need) + " channels."));
    else
        card->set(rkui::StatusCard::Kind::Ok, tr("Sitzt gut"), juce::String(need) + " / " + juce::String(have) + " " + tr("Ausgang"));
    page_.add(std::move(card), 72);
    page_.add(std::make_unique<rkui::SectionHeader>(tr("Ausgabe")), 26);
    auto* master = page_.add(std::make_unique<rkui::ParamSlider>(tr("Gesamtpegel"), -60, 12, 0.1, "dB", 1, 0), 32);
    auto* prm = proc_.params.getParameter("master");
    master->setValue(prm->convertFrom0to1(prm->getValue()));
    master->onChange = [prm](double v) { prm->setValueNotifyingHost(prm->convertTo0to1((float)v)); };
    auto addToggle = [this](const char* id, const juce::String& label) {
        auto* t = page_.add(std::make_unique<juce::ToggleButton>(label), 28);
        auto* p = proc_.params.getParameter(id);
        t->setToggleState(p->getValue() > 0.5f, juce::dontSendNotification);
        t->onClick = [t, p] { p->setValueNotifyingHost(t->getToggleState() ? 1.0f : 0.0f); };
    };
    addToggle("headphones", tr("Kopfhörer-Vorschau"));
    addToggle("limiter", tr("Schutzbegrenzer"));
    auto* test = page_.add(rkui::makeButton(testRunning_ ? tr("Test beenden") : tr("Lautsprecher einzeln anspielen"), testRunning_), 32);
    test->onClick = [this] {
        testRunning_ = !testRunning_;
        testIndex_ = juce::jmax(0, selSpeaker_);
        testNext_ = 0.0;
        if (!testRunning_) proc_.speakers.speakerTest = -1;
        rebuild();
    };
}

void RoomEditor::timerCallback() {
    const int n = (int)proc_.scene().layout.speakers.size();
    levels_.resize((size_t)n);
    for (int c = 0; c < n; ++c) {
        const float db = rk::gainToDb(proc_.speakers.takePeak(c));
        levels_[(size_t)c] = std::max(juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f), levels_[(size_t)c] * 0.85f);
    }
    stage_.setSpeakerLevels(levels_);
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (testRunning_ && n > 0 && now >= testNext_) {
        testIndex_ = testNext_ == 0.0 ? testIndex_ % n : (testIndex_ + 1) % n;
        proc_.speakers.speakerTest = testIndex_;
        selSpeaker_ = testIndex_;
        stage_.setSelectedSpeaker(selSpeaker_);
        testNext_ = now + 1500.0;
    }
}

void RoomEditor::paint(juce::Graphics& g) {
    const auto& p = rkui::palette();
    g.fillAll(p.side);
    g.setColour(p.bar);
    g.fillRect(0, 0, getWidth(), 52);
    g.setColour(p.text);
    g.setFont(rkui::uiFont(15.0f, true));
    g.drawText("Raumklang Raum", 16, 0, 220, 52, juce::Justification::centredLeft);
}

void RoomEditor::resized() {
    auto r = getLocalBounds();
    auto top = r.removeFromTop(52);
    tabs_.setBounds(top.withSizeKeepingCentre(juce::jmin(tabs_.idealWidth(), top.getWidth() - 300), 34));
    page_.setBounds(r.removeFromRight(juce::jmax(330, getWidth() / 3)).reduced(0, 8));
    stage_.setBounds(r);
}

} // namespace rkplug
