#include "MainComponent.h"
#include "MicPermission.h"

using rkui::tr;
using rkui::u8;

namespace rkapp {

// ---------------------------------------------------------------------------
// First-run introduction, offers the demo (Projection Studio does the same).
// ---------------------------------------------------------------------------
class IntroOverlay : public juce::Component {
public:
    std::function<void()> onDemo, onEmpty;

    IntroOverlay() {
        skip_ = rkui::makeButton(tr("Überspringen"), false, true);
        next_ = rkui::makeButton(tr("Weiter"), true);
        demo_ = rkui::makeButton({}, false, true);
        empty_ = rkui::makeButton({}, false, true);
        back_ = rkui::makeButton(tr("Zurück"), false, true);
        back_->onClick = [this] { setPage(0); };
        demo_->addMouseListener(this, false);
        empty_->addMouseListener(this, false);
        addAndMakeVisible(*back_);
        skip_->onClick = [this] { if (onEmpty) onEmpty(); };
        next_->onClick = [this] { setPage(1); };
        demo_->onClick = [this] { if (onDemo) onDemo(); };
        empty_->onClick = [this] { if (onEmpty) onEmpty(); };
        for (auto* b : {skip_.get(), next_.get(), demo_.get(), empty_.get()}) addAndMakeVisible(*b);
        setPage(0);
        setWantsKeyboardFocus(true);
    }

    void setPage(int p) {
        page_ = p;
        skip_->setVisible(p == 0);
        next_->setVisible(p == 0);
        demo_->setVisible(p == 1);
        empty_->setVisible(p == 1);
        back_->setVisible(p == 1);
        resized();
        repaint();
    }

    juce::Rectangle<int> card() const { return getLocalBounds().withSizeKeepingCentre(620, 520); }

    void paint(juce::Graphics& g) override {
        const auto& p = rkui::palette();
        g.fillAll(juce::Colours::black.withAlpha(0.5f));
        auto c = card().toFloat();
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRoundedRectangle(c.translated(0, 12).expanded(6), 18.0f);
        g.setColour(p.bar);
        g.fillRoundedRectangle(c, 14.0f);
        g.setColour(p.lineStrong);
        g.drawRoundedRectangle(c.reduced(0.25f), 14.0f, 0.75f);
        // Footer strip with page dots.
        auto foot = c.removeFromBottom(56.0f);
        g.setColour(p.line);
        g.drawHorizontalLine((int)foot.getY(), foot.getX(), foot.getRight());
        for (int i = 0; i < 2; ++i) {
            const bool on = i == page_;
            g.setColour(on ? p.accent : p.lineStrong);
            g.fillRoundedRectangle(foot.getX() + 24.0f + i * 12.0f + (i > page_ ? 10.0f : 0.0f), foot.getCentreY() - 2.5f, on ? 18.0f : 5.0f, 5.0f, 2.5f);
        }
        auto r = c.reduced(40.0f, 28.0f);
        paintLogo(g, r.removeFromTop(78.0f).withSizeKeepingCentre(150.0f, 72.0f));
        r.removeFromTop(8.0f);
        g.setColour(p.text);
        g.setFont(rkui::uiFont(24.0f, true));
        g.drawText(page_ == 0 ? tr("Willkommen") : tr("Wie möchtest du beginnen?"), r.removeFromTop(32.0f), juce::Justification::centred);
        g.setColour(p.text2);
        g.setFont(rkui::uiFont(14.0f));
        const juce::String sub = page_ == 0 ? tr("Verteile Klang frei im Raum, auf beliebig viele Lautsprecher, direkt über dein Audio-Interface.")
                                            : tr("Die Demo spielt drei Testklänge, die um dich kreisen. Ohne Mehrkanal-Interface hörst du sie über Kopfhörer.");
        g.drawFittedText(sub, r.removeFromTop(44.0f).toNearestInt(), juce::Justification::centred, 2);
        r.removeFromTop(16.0f);
        if (page_ != 0) {
            const struct { juce::Component* b; rkui::Icon icon; const char* title; const char* text; } tiles[] = {
                {demo_.get(), rkui::Icon::Source, "Mit Demo beginnen", "Ring aus 8 Lautsprechern, drei Testklänge kreisen. Sofort etwas zu hören."},
                {empty_.get(), rkui::Icon::Speaker, "Eigenen Aufbau einrichten", "Mit den Lautsprechern beginnen: Vorlage wählen oder frei aufstellen."},
            };
            for (const auto& t : tiles) {
                auto tr0 = t.b->getBounds().toFloat();
                g.setColour(p.group);
                g.fillRoundedRectangle(tr0, 12.0f);
                g.setColour(t.b->isMouseOver() ? p.accent : p.line);
                g.drawRoundedRectangle(tr0.reduced(0.5f), 12.0f, t.b->isMouseOver() ? 1.5f : 0.75f);
                auto in = tr0.reduced(18.0f);
                auto ic = in.removeFromTop(40.0f).removeFromLeft(40.0f);
                g.setColour(p.accentSoft);
                g.fillRoundedRectangle(ic, 10.0f);
                rkui::drawIcon(g, t.icon, ic.withSizeKeepingCentre(20.0f, 20.0f), p.accent, 1.5f);
                in.removeFromTop(12.0f);
                g.setColour(p.text);
                g.setFont(rkui::uiFont(15.0f, true));
                g.drawText(tr(t.title), in.removeFromTop(22.0f), juce::Justification::centredLeft);
                g.setColour(p.text2);
                g.setFont(rkui::uiFont(13.0f));
                g.drawFittedText(tr(t.text), in.removeFromTop(54.0f).toNearestInt(), juce::Justification::topLeft, 3);
            }
            return;
        }
        const struct { rkui::Icon icon; const char* title; const char* text; } steps[] = {
            {rkui::Icon::Speaker, "Lautsprecher", "Vorlage wählen, vom Stereopaar bis zum Donut, oder frei aufstellen."},
            {rkui::Icon::Source, "Quellen", "Eingänge des Interfaces oder Testklänge in der Ansicht platzieren."},
            {rkui::Icon::Room, "Raum", "Hall wählen, von trocken bis Kirche."},
            {rkui::Icon::Output, "Ausgabe", "Audiogerät wählen, Lautsprecher prüfen, fertig."},
        };
        for (const auto& st : steps) {
            auto row = r.removeFromTop(54.0f);
            auto ic = row.removeFromLeft(36.0f).withSizeKeepingCentre(36.0f, 36.0f).withY(row.getY() + 2.0f);
            g.setColour(p.accentSoft);
            g.fillRoundedRectangle(ic, 9.0f);
            rkui::drawIcon(g, st.icon, ic.withSizeKeepingCentre(18.0f, 18.0f), p.accent, 1.4f);
            row.removeFromLeft(14.0f);
            g.setColour(p.text);
            g.setFont(rkui::uiFont(14.0f, true));
            g.drawText(tr(st.title), row.removeFromTop(20.0f), juce::Justification::centredLeft);
            g.setColour(p.text2);
            g.setFont(rkui::uiFont(13.0f));
            g.drawText(tr(st.text), row.removeFromTop(20.0f), juce::Justification::centredLeft, true);
        }
    }

    void resized() override {
        auto foot = card().removeFromBottom(56).reduced(16, 11);
        next_->setBounds(foot.removeFromRight(110));
        foot.removeFromRight(8);
        skip_->setBounds(foot.removeFromRight(130));
        back_->setBounds(card().removeFromBottom(56).reduced(16, 11).removeFromRight(110));
        auto mid = card().withTrimmedTop(252).withTrimmedBottom(80).reduced(40, 0);
        const int tw = (mid.getWidth() - 16) / 2;
        demo_->setBounds(mid.removeFromLeft(tw).withHeight(150));
        mid.removeFromLeft(16);
        empty_->setBounds(mid.removeFromLeft(tw).withHeight(150));
    }

    bool keyPressed(const juce::KeyPress& k) override {
        if (k == juce::KeyPress::returnKey) { if (page_ == 0) setPage(1); else if (onDemo) onDemo(); return true; }
        if (k == juce::KeyPress::escapeKey) { if (onEmpty) onEmpty(); return true; }
        return true;
    }

    void mouseDown(const juce::MouseEvent&) override {} // block clicks to the UI below
    void mouseMove(const juce::MouseEvent&) override { repaint(); }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    static void paintLogo(juce::Graphics& g, juce::Rectangle<float> r) {
        // Two facing speaker rings with sound arcs: the donut, the thing that makes Raumklang special.
        const auto c = r.getCentre();
        for (int i = 0; i < 3; ++i) {
            const float rad = 14.0f + i * 11.0f;
            juce::Path arc;
            arc.addCentredArc(c.x, c.y + 10.0f, rad, rad, 0.0f, -1.1f, 1.1f, true);
            g.setColour(rkui::itemColour(i == 0 ? 3 : (i == 1 ? 2 : 0)).withAlpha(0.9f - i * 0.2f));
            g.strokePath(arc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour(rkui::itemColour(3));
        g.fillEllipse(c.x - 5.0f, c.y + 5.0f, 10.0f, 10.0f);
        for (int k = 0; k < 2; ++k) {
            const float x = k == 0 ? r.getX() + 8.0f : r.getRight() - 24.0f;
            g.setColour(rkui::itemColour(4));
            g.fillRoundedRectangle(x, c.y - 4.0f, 16.0f, 13.0f, 3.5f);
        }
    }

    int page_ = 0;
    std::unique_ptr<juce::TextButton> skip_, next_, demo_, empty_, back_;
};

// ---------------------------------------------------------------------------

namespace {
constexpr int kTopBar = 52;
constexpr int kSidebar = 240;
constexpr int kInspector = 350;

float levelToUnit(float peak) {
    const float db = rk::gainToDb(peak);
    return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

juce::String degree() { return juce::String(juce::CharPointer_UTF8("\xc2\xb0")); }
} // namespace

MainComponent::MainComponent()
    : steps_(juce::StringArray{tr("Lautsprecher"), tr("Quellen"), tr("Raum"), tr("Ausgabe")}, true) {
    juce::PropertiesFile::Options opt;
    opt.applicationName = "Raumklang";
    opt.filenameSuffix = ".settings";
    opt.folderName = rkui::appDataFolder().getFullPathName();
    opt.osxLibrarySubFolder = "Application Support";
    settings_ = std::make_unique<juce::PropertiesFile>(rkui::appDataFolder().getChildFile("Einstellungen.settings"), opt);
    rkui::setLanguage(settings_->getValue("language", "de") == "en" ? rkui::Language::English : rkui::Language::German);
    rkui::setDarkMode(settings_->getValue("theme", "dark") != "light"); // dark is the family default
    lnf_.refreshColours();
    setLookAndFeel(&lnf_);
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf_);

    steps_.onChange = [this](int i) { setStep(i); };
    addAndMakeVisible(steps_);
    addBtn_ = rkui::makeButton("+ " + tr("Quelle hinzufügen"));
    addBtn_->getProperties().set("block", true);
    updateTooltips();
    undoBtn_.onClick = [this] { undo(); };
    redoBtn_.onClick = [this] { redo(); };
    openBtn_.onClick = [this] { openProject(); };
    saveBtn_.onClick = [this] { saveProjectAs(); };
    helpBtn_.onClick = [this] { showHelpMenu(); };
    addBtn_->onClick = [this] { addSource(); };
    for (juce::Component* b : {(juce::Component*)&undoBtn_, (juce::Component*)&redoBtn_, (juce::Component*)&openBtn_,
                               (juce::Component*)&saveBtn_, (juce::Component*)&helpBtn_, (juce::Component*)addBtn_.get()})
        addAndMakeVisible(*b);

    sidebar_.onSelect = [this](int row) {
        if (step_ == StepSpeakers || step_ == StepOutput) selectSpeaker(row);
        else if (step_ == StepRoom) selectRoomPreset(row);
        else selectSource(row);
    };
    sidebar_.onToggle = [this](int row, int which) {
        if (step_ != StepSources || row < 0 || row >= (int)scene_.sources.size()) return;
        auto& s = scene_.sources[(size_t)row];
        if (which == 0) s.mute = !s.mute;
        else s.solo = !s.solo;
        applyScene(false);
        commit();
        refreshSidebar();
        showStage();
        if (row == selSource_) rebuildInspector();
    };
    addAndMakeVisible(outputStrip_);
    outputStrip_.setTooltip(tr("Master-Pegel aller Ausgänge. Doppelklick setzt auf 0 dB."));
    outputStrip_.onChange = [this](float db) {
        scene_.masterGainDb = db;
        applyScene(false);
    };
    outputStrip_.onCommit = [this] {
        commit();
        if (step_ == StepOutput) rebuildInspector();
    };
    sidebarView_.setViewedComponent(&sidebar_, false);
    sidebarView_.setScrollBarsShown(true, false);
    sidebarView_.setScrollBarThickness(8);
    addAndMakeVisible(sidebarView_);

    stage_.setLegendAtBottom(false);
    stage_.onSourceMoved = [this](int i, rk::Vec3 p) {
        if (i < (int)scene_.sources.size()) {
            scene_.sources[(size_t)i].position = p;
            applyScene(false);
            refreshSidebar();
            // Only the three position sliders change while dragging: update them in place
            // instead of rebuilding the whole inspector on every mouse move.
            if (i == selSource_ && step_ == StepSources && posSliders_[0] != nullptr) {
                const rk::Polar pol = rk::toPolar(p);
                posSliders_[0]->setValue(pol.azimuthDeg);
                posSliders_[1]->setValue(pol.elevationDeg);
                posSliders_[2]->setValue(pol.distance);
            }
        } else {
            const rk::Polar pol = rk::toPolar(p);
            moveDawTrack(i - (int)scene_.sources.size(), pol.azimuthDeg, pol.elevationDeg, pol.distance);
        }
    };
    stage_.onSpeakerMoved = [this](int i, rk::Vec3 p) {
        if (i < (int)scene_.layout.speakers.size()) {
            scene_.layout.speakers[(size_t)i].position = p;
            scene_.presetId = "custom";
            applyScene(true);
            refreshSidebar();
        }
    };
    stage_.onSourceSelected = [this](int i) { selectSource(i); };
    stage_.onSpeakerSelected = [this](int i) { selectSpeaker(i); };
    stage_.onDragEnd = [this] { commit(); rebuildInspector(); };
    addAndMakeVisible(stage_);
    addChildComponent(reverbPanel_);
    addAndMakeVisible(snapBar_);
    snapBar_.onStore = [this] { storeSnapshot(); };
    snapBar_.onRecall = [this](int i) { recallSnapshot(i); };
    snapBar_.onMenu = [this](int i) { snapshotMenu(i); };
    snapBar_.onUpdate = [this] {
        if (activeSnapshot_ < 0 || activeSnapshot_ >= (int)scene_.snapshots.size()) return;
        auto& sn = scene_.snapshots[(size_t)activeSnapshot_];
        sn = rkui::captureSnapshot(scene_, currentDawStates(), sn.name, sn.fadeSec);
        commit();
    };
    reverbPanel_.onChange = [this](const rk::ReverbParams& p) {
        scene_.reverb = p;
        applyScene(false);
    };
    reverbPanel_.onCommit = [this] {
        commit();
        rebuildInspector();
        refreshSidebar();
    };
    addAndMakeVisible(inspector_);

    // Audio: open the device stored last time, otherwise the system default.
    const bool testRoom = juce::SystemStats::getEnvironmentVariable("RAUMKLANG_TESTRAUM", {}).isNotEmpty();
    if (!testRoom) {
        std::unique_ptr<juce::XmlElement> audioState(settings_->getXmlValue("audioDevice"));
        // Inputs only when access is already granted: no microphone prompt for test signals or DAW tracks.
        const juce::String err = host_.start(audioState.get(), micAccess() == MicAccess::Granted);
        if (err.isNotEmpty()) statusText_ = err;
    }

    // Restore the autosave, or show the introduction.
    const juce::File autosave = rkui::appDataFolder().getChildFile("Autosave.raumklang");
    rkui::Scene restored;
    if (autosave.existsAsFile() && rkui::sceneFromJson(autosave.loadFileAsString(), restored)) {
        loadScene(restored, true);
    } else {
        rkui::Scene s;
        loadScene(s, true);
        dirty_ = false; // nothing chosen yet: keep showing the introduction next time
        showIntro();
    }

    if (testRoom) {
        // A room that matches the layout only roughly, like a real one.
        rk::SpeakerLayout real = scene_.layout;
        juce::Random rnd(42);
        for (auto& sp : real.speakers)
            sp.position = sp.position + rk::Vec3{(rnd.nextFloat() - 0.5f) * 0.6f, (rnd.nextFloat() - 0.5f) * 0.6f, (rnd.nextFloat() - 0.5f) * 0.3f};
        host_.startSimulation(real);
        statusText_ = tr("Testraum ohne Audiogerät");
    }

    updater_.checkInBackgroundIfDue(this, *settings_);

    setWantsKeyboardFocus(true);
    setSize(1320, 820);
    startTimerHz(30);
}

MainComponent::~MainComponent() {
    stopTimer();
    saveNow();
    if (!host_.simulating())
        if (auto xml = host_.devices.createStateXml()) settings_->setValue("audioDevice", xml.get());
    settings_->saveIfNeeded();
    host_.stop();
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

// ---------------------------------------------------------------------------

rkui::PageContext MainComponent::pageContext() {
    rkui::PageContext c;
    c.scene = &scene_;
    c.changed = [this](bool structural) {
        applyScene(structural);
        showStage();
        refreshSidebar();
    };
    c.commit = [this] { commit(); };
    c.calibrate = [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) sp->startCalibration(); }); };
    c.importSurvey = [this] { chooseSurveyFile(); };
    c.rebuild = [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) { sp->rebuildInspector(); sp->refreshSidebar(); } }); };
    return c;
}

void MainComponent::ensureInputs() {
    if (host_.inputsEnabled() || micRequested_) return;
    if (host_.devices.getCurrentAudioDevice() == nullptr) return;
    micRequested_ = true; // ask once per run; a "no" is respected until the next start
    auto open = [sp = juce::Component::SafePointer<MainComponent>(this)](bool granted) {
        if (sp == nullptr) return;
        if (!granted) {
            sp->statusText_ = tr("Kein Zugriff auf die Eingänge. Freigabe unter Systemeinstellungen → Datenschutz & Sicherheit → Mikrofon.");
            sp->repaint();
            return;
        }
        const juce::String err = sp->host_.setInputsEnabled(true);
        if (err.isNotEmpty()) sp->statusText_ = err;
        sp->rebuildInspector();
    };
    if (micAccess() == MicAccess::Denied) open(false);
    else requestMicAccess(open);
}

void MainComponent::applyScene(bool structural) {
    for (const auto& s : scene_.sources)
        if (s.test == rkui::TestSignal::Off) {
            juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) sp->ensureInputs(); });
            break;
        }
    if (structural) host_.engine.setConfig(scene_.engineConfig());
    for (int i = 0; i < rkui::kMaxSceneSources; ++i) {
        auto& c = host_.engine.source(i);
        if (i < (int)scene_.sources.size()) {
            const auto& s = scene_.sources[(size_t)i];
            const bool test = s.test != rkui::TestSignal::Off;
            host_.testSignal[(size_t)i].store(static_cast<int>(s.test));
            c.inputChannel = test ? AudioHost::kGeneratorBase + i : s.inputChannel;
            c.gainDb = s.gainDb;
            c.spread = s.spread;
            c.reverbSendDb = s.reverbSendDb;
            c.distanceAmount = s.distanceAmount;
            c.yawDeg = s.yawDeg;
            c.directivity = s.directivity;
            c.doppler = s.doppler;
            c.mute = s.mute;
            c.solo = s.solo;
            c.active = true;
            c.setPosition(s.position);
        } else {
            c.active = false;
            host_.testSignal[(size_t)i].store(0);
        }
    }
    host_.engine.setReverb(scene_.reverb);
    host_.engine.reverbWetDb = scene_.reverbWetDb;
    host_.engine.masterGainDb = scene_.masterGainDb;
    host_.engine.limiterEnabled = scene_.limiter;
    dirty_ = true;
    lastChange_ = juce::Time::getMillisecondCounterHiRes();
}

void MainComponent::commit() {
    const juce::String now = rkui::sceneToJson(scene_);
    if (now == committed_) return;
    undo_.push_back(committed_);
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
    committed_ = now;
    dirty_ = true;
    rkui::writeSharedSetup(scene_);
    undoBtn_.setEnabled(true);
    redoBtn_.setEnabled(false);
}

void MainComponent::undo() {
    if (undo_.empty()) return;
    redo_.push_back(committed_);
    committed_ = undo_.back();
    undo_.pop_back();
    rkui::Scene s;
    if (rkui::sceneFromJson(committed_, s)) loadScene(s, false);
}

void MainComponent::redo() {
    if (redo_.empty()) return;
    undo_.push_back(committed_);
    committed_ = redo_.back();
    redo_.pop_back();
    rkui::Scene s;
    if (rkui::sceneFromJson(committed_, s)) loadScene(s, false);
}

void MainComponent::loadScene(const rkui::Scene& s, bool resetHistory) {
    scene_ = s;
    if (resetHistory) {
        undo_.clear();
        redo_.clear();
        committed_ = rkui::sceneToJson(scene_);
    }
    selSource_ = scene_.sources.empty() ? -1 : juce::jlimit(0, (int)scene_.sources.size() - 1, juce::jmax(0, selSource_));
    selSpeaker_ = juce::jmin(selSpeaker_, (int)scene_.layout.speakers.size() - 1);
    applyScene(true);
    rkui::writeSharedSetup(scene_);
    refreshAll();
}

void MainComponent::refreshAll() {
    steps_.setSelected(step_);
    showStage();
    stage_.setSelectedSource(selSource_);
    stage_.setSelectedSpeaker(selSpeaker_);
    stage_.setEditTarget(step_ == StepSpeakers ? rkui::StageView::EditTarget::Speakers : rkui::StageView::EditTarget::Sources);
    updateCoach();
    undoBtn_.setEnabled(!undo_.empty());
    redoBtn_.setEnabled(!redo_.empty());
    addBtn_->setVisible(step_ == StepSources);
    stage_.setVisible(step_ != StepRoom || calibrating_);
    reverbPanel_.setVisible(step_ == StepRoom && !calibrating_);
    reverbPanel_.setParams(scene_.reverb, scene_.reverbWetDb);
    snapBar_.setSnapshots(scene_.snapshots);
    snapBar_.setVisible(!calibrating_);
    refreshSidebar();
    rebuildInspector();
    resized();
    repaint();
}

juce::String MainComponent::sourceSubtitle(int i) const {
    const auto& s = scene_.sources[(size_t)i];
    juce::String in;
    switch (s.test) {
    case rkui::TestSignal::Pink: in = tr("Rosa Rauschen"); break;
    case rkui::TestSignal::Sine: in = tr("Sinuston"); break;
    case rkui::TestSignal::Clicks: in = tr("Klicks"); break;
    default: in = tr("Eingang") + " " + juce::String(s.inputChannel + 1); break;
    }
    const rk::Polar p = rk::toPolar(s.position);
    return in + ", " + rkui::num(p.azimuthDeg, 0) + degree() + ", " + rkui::num(p.distance, 1) + " m";
}

rkui::Scene MainComponent::stageScene() const {
    if (dawTracks_.empty()) return scene_;
    rkui::Scene s = scene_;
    for (const auto& t : dawTracks_) {
        const auto p = host_.bridgeParams(t.slot);
        rkui::SourceInfo info;
        info.name = juce::String::fromUTF8(t.name.c_str());
        info.position = rk::toCartesian({p.azimuthDeg, p.elevationDeg, p.distance});
        const auto& mv = dawMoves_[(size_t)juce::jlimit(0, rk::bridge::kSlots - 1, t.slot)];
        if (juce::Time::getMillisecondCounterHiRes() < mv.until) info.position = mv.position;
        info.spread = juce::jlimit(0.0f, 1.0f, p.spread + (t.channels == 2 ? p.stereoWidthDeg / 360.0f : 0.0f));
        info.mute = !t.sending || t.sampleRateMismatch;
        s.sources.push_back(info);
    }
    return s;
}

void MainComponent::showStage() {
    // DAW tracks can be dragged too: the position goes back to the track's plugin.
    stage_.setFirstLockedSource((int)scene_.sources.size() + (int)dawTracks_.size(), (int)scene_.sources.size());
    rkui::Scene s = stageScene();
    if (calibrating_ && proposal_) s.layout = rkui::applyProposal(scene_.layout, *proposal_, proposalOpts_);
    stage_.setScene(s);
    updateCalibrationOverlay();
}

int MainComponent::dawTrackLevelIndex(int track) const {
    if (track < 0 || track >= (int)dawTracks_.size()) return -1;
    return AudioHost::kBridgeSourceBase + 2 * dawTracks_[(size_t)track].slot;
}

void MainComponent::scanDaw() {
    auto tracks = host_.scanBridge();
    // Some hosts send no track name or a placeholder like "void". Such tracks get a
    // numbered name instead, so they stay distinguishable until they are renamed.
    for (auto& t : tracks) {
        const juce::String n = juce::String::fromUTF8(t.name.c_str()).trim();
        if (rkui::isPlaceholderTrackName(n)) t.name = (tr("DAW-Spur") + " " + juce::String(t.slot + 1)).toStdString();
    }
    juce::String key;
    for (const auto& t : tracks)
        key << t.slot << ':' << juce::String::fromUTF8(t.name.c_str()) << ':' << t.channels << ':' << (int)t.sending << ':'
            << (int)t.sampleRateMismatch << ';';
    if (key == dawKey_) return;
    const bool hadSelection = selSource_ >= (int)scene_.sources.size();
    int selectedSlot = -1;
    if (hadSelection && selSource_ - (int)scene_.sources.size() < (int)dawTracks_.size())
        selectedSlot = dawTracks_[(size_t)(selSource_ - (int)scene_.sources.size())].slot;
    dawKey_ = key;
    dawTracks_ = std::move(tracks);
    if (hadSelection) {
        // Keep the same track selected when others come or go.
        int found = -1;
        for (int j = 0; j < (int)dawTracks_.size(); ++j)
            if (dawTracks_[(size_t)j].slot == selectedSlot) found = j;
        selSource_ = found >= 0 ? (int)scene_.sources.size() + found : (scene_.sources.empty() ? -1 : 0);
        stage_.setSelectedSource(selSource_);
    }
    if (step_ == StepSources || step_ == StepRoom) refreshSidebar();
    showStage();
    if (step_ == StepSources) rebuildInspector();
    repaint(0, getHeight() - 52, kSidebar, 52);
}

void MainComponent::buildDawTrackPage(int j) {
    const auto& t = dawTracks_[(size_t)j];
    dawPageTrack_ = j;
    juce::String name = juce::String::fromUTF8(t.name.c_str());
    if (name.isEmpty()) name = tr("Spur") + " " + juce::String(j + 1);
    inspector_.add(std::make_unique<rkui::SectionHeader>(name, tr("Diese Spur kommt über das Plugin „Raumklang Quelle“ aus der DAW. Die Position lässt sich hier und in der Ansicht ziehen, das Plugin übernimmt sie. Liest die DAW gerade Automation, gewinnt die Automation.")), 26);
    // Only a problem gets a card. A connected track needs no message, the list shows it playing.
    if (t.sampleRateMismatch) {
        dawCard_ = inspector_.add(std::make_unique<rkui::StatusCard>(), 72);
        dawCard_->set(rkui::StatusCard::Kind::Warn, tr("Abtastrate passt nicht"),
                      tr("Die DAW läuft mit") + " " + rkui::num(t.sampleRate / 1000.0, 1) + " kHz, " + tr("die App mit") + " " +
                          rkui::num(host_.sampleRate() / 1000.0, 1) + " kHz. " + tr("Stelle beide gleich ein."));
        inspector_.setRowHeight(dawCard_, dawCard_->idealHeight(juce::jmax(200, inspector_.getWidth() - 24)));
    }

    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Position")), 26);
    auto add = [this, j](const juce::String& label, double lo, double hi, const juce::String& unit, int dec) {
        auto* sl = inspector_.add(std::make_unique<rkui::ParamSlider>(label, lo, hi, 0.01, unit, dec, 0.0), 32);
        const bool position = dawSliders_.size() < 3;
        if (position) {
            // Position can be changed here as well; the plugin takes it over (and the DAW records it).
            sl->setTooltip(tr("Ändert die Position im Plugin. Steht die Spur in der DAW auf Automation schreiben, wird die Bewegung aufgenommen."));
            sl->onChange = [this, j](double) {
                if (dawSliders_.size() < 3) return;
                moveDawTrack(j, (float)dawSliders_[0]->getValue(), (float)dawSliders_[1]->getValue(), (float)dawSliders_[2]->getValue());
            };
        } else {
            sl->setEnabledState(false);
            sl->setTooltip(tr("Im Plugin in der DAW einstellen."));
        }
        dawSliders_.push_back(sl);
    };
    add(tr("Azimut"), -180, 180, degree(), 1);
    add(tr("Elevation"), -90, 90, degree(), 1);
    add(tr("Abstand"), 0, 50, "m", 2);
    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Klang")), 26);
    add(tr("Pegel"), -60, 12, "dB", 1);
    add(tr("Breite"), 0, 100, "%", 0);
    add(tr("Hall-Send"), -80, 6, "dB", 1);
    if (dawSliders_.size() > 2) dawSliders_[2]->setSkewForCentre(5.0);
    refreshDawValues();
    inspector_.addDetails(tr("Ein Plugin „Raumklang Quelle“ pro Spur in der DAW. Steht sein Ziel auf „Automatisch“ oder „An die App“, spielt die App die Spur über ihr Audiogerät, die Spur in der DAW bleibt still.\n\n"
                             "DAW und App müssen mit derselben Abtastrate laufen. Bis zu 32 Spuren gleichzeitig."));
}

void MainComponent::refreshDawValues() {
    if (dawPageTrack_ < 0 || dawPageTrack_ >= (int)dawTracks_.size() || dawSliders_.size() < 6) return;
    const int slot = dawTracks_[(size_t)dawPageTrack_].slot;
    const auto p = host_.bridgeParams(slot);
    const double v[6] = {p.azimuthDeg, p.elevationDeg, p.distance, p.gainDb, p.spread * 100.0, p.reverbSendDb};
    // While the user moves the track here, the plugin's values lag behind: leave position alone.
    const bool moving = slot >= 0 && slot < rk::bridge::kSlots && juce::Time::getMillisecondCounterHiRes() < dawMoves_[(size_t)slot].until;
    for (int i = moving ? 3 : 0; i < 6; ++i)
        if (std::abs(dawSliders_[(size_t)i]->getValue() - v[i]) > 1e-4) dawSliders_[(size_t)i]->setValue(v[i]);
}

void MainComponent::refreshSidebar() {
    std::vector<rkui::SidebarList::Row> rows;
    if (step_ == StepSpeakers || step_ == StepOutput) {
        const int outs = host_.numDeviceOutputs();
        for (int m = 0; m < (int)scene_.layout.speakers.size(); ++m) {
            const auto& sp = scene_.layout.speakers[(size_t)m];
            rkui::SidebarList::Row r;
            r.group = sp.group < (int)scene_.layout.groups.size() ? juce::String(scene_.layout.groups[(size_t)sp.group].name) : juce::String();
            r.number = m + 1;
            r.colour = rkui::itemColour(sp.group + 3);
            const juce::String nm = juce::String::fromUTF8(sp.name.c_str());
            r.title = nm.isEmpty() || nm == juce::String(m + 1) ? tr("Lautsprecher") + " " + juce::String(m + 1) : nm;
            const rk::Polar p = rk::toPolar(sp.position);
            r.subtitle = tr("Ausgang") + " " + juce::String(m + 1) + ", " + rkui::num(p.azimuthDeg, 0) + degree() + ", " + rkui::num(p.distance, 1) + " m";
            r.trailing = sp.isSubwoofer ? "Sub" : (sp.gainDb != 0.0f ? rkui::num(sp.gainDb, 1) + " dB" : juce::String());
            r.dim = !sp.enabled || (!scene_.headphones && m >= outs);
            r.check = !r.dim && r.trailing.isEmpty();
            if (!scene_.headphones && m >= outs) r.subtitle = tr("Kein Ausgang") + ", " + rkui::num(p.azimuthDeg, 0) + degree();
            if (calibrating_) {
                r.subtitle = calibrationSubtitle(m);
                if (proposal_ && m < (int)proposal_->speakers.size()) {
                    const auto& q = proposal_->speakers[(size_t)m];
                    r.check = q.measured && !q.warn;
                    r.trailing = q.warn ? juce::String("!") : (q.hasDelay ? rkui::num(q.delayMs, 1) + " ms" : juce::String());
                } else {
                    r.check = false;
                    r.trailing = {};
                }
            }
            rows.push_back(r);
        }
        sidebar_.setRows(rows);
        sidebar_.setSelected(selSpeaker_);
    } else if (step_ == StepRoom) {
        // Room types. Any change to a type turns it into "Eigener" (custom).
        const auto& presets = rkui::roomPresets();
        const int match = rkui::matchRoomPreset(scene_.reverb, scene_.reverbWetDb);
        for (int i = 0; i < (int)presets.size(); ++i) {
            rkui::SidebarList::Row r;
            r.group = tr("Raumtyp");
            r.number = i + 1;
            r.colour = rkui::itemColour(i + 2);
            r.title = tr(presets[(size_t)i].name);
            r.subtitle = rkui::num(rkui::reverbBandRt(presets[(size_t)i].params, 1), 1) + " s";
            rows.push_back(r);
        }
        if (match < 0) {
            rkui::SidebarList::Row r;
            r.group = tr("Raumtyp");
            r.number = (int)presets.size() + 1;
            r.colour = rkui::itemColour((int)presets.size() + 2);
            r.title = tr("Eigener");
            r.subtitle = rkui::num(rkui::reverbBandRt(scene_.reverb, 1), 1) + " s";
            rows.push_back(r);
        }
        sidebar_.setRows(rows);
        sidebar_.setSelected(match >= 0 ? match : (int)presets.size());
    } else {
        const bool anySolo = std::any_of(scene_.sources.begin(), scene_.sources.end(), [](const rkui::SourceInfo& x) { return x.solo; });
        for (int i = 0; i < (int)scene_.sources.size(); ++i) {
            const auto& s = scene_.sources[(size_t)i];
            rkui::SidebarList::Row r;
            r.number = i + 1;
            r.colour = rkui::itemColour(i);
            r.title = s.name;
            r.subtitle = sourceSubtitle(i);
            r.trailing = s.orbit ? u8("\xe2\x9f\xb3") : juce::String();
            r.toggles = step_ == StepSources;
            r.mute = s.mute;
            r.solo = s.solo;
            r.dim = s.mute || (anySolo && !s.solo);
            r.group = dawTracks_.empty() ? juce::String() : tr("Im Projekt");
            rows.push_back(r);
        }
        const int n = (int)scene_.sources.size();
        for (int j = 0; j < (int)dawTracks_.size(); ++j) {
            const auto& t = dawTracks_[(size_t)j];
            const auto p = host_.bridgeParams(t.slot);
            rkui::SidebarList::Row r;
            r.group = tr("Aus der DAW");
            r.number = n + j + 1;
            r.colour = rkui::itemColour(n + j);
            r.title = juce::String::fromUTF8(t.name.c_str());
            if (r.title.isEmpty()) r.title = tr("Spur") + " " + juce::String(j + 1);
            const juce::String kind = t.channels == 2 ? "Stereo" : "Mono";
            if (t.sampleRateMismatch) r.subtitle = tr("Abtastrate passt nicht");
            else if (!t.sending) r.subtitle = kind + ", " + tr("wartet auf Wiedergabe");
            else r.subtitle = kind + ", " + rkui::num(p.azimuthDeg, 0) + degree() + ", " + rkui::num(p.distance, 1) + " m";
            r.trailing = "DAW";
            r.dim = !t.sending || t.sampleRateMismatch;
            rows.push_back(r);
        }
        sidebar_.setRows(rows);
        sidebar_.setSelected(selSource_);
    }
    sidebar_.setSize(sidebarView_.getMaximumVisibleWidth() > 0 ? sidebarView_.getMaximumVisibleWidth() : kSidebar, sidebar_.contentHeight());
}

void MainComponent::selectSource(int i) {
    selSource_ = i;
    stage_.setSelectedSource(i);
    sidebar_.setSelected(i);
    rebuildInspector();
}

void MainComponent::moveDawTrack(int j, float azimuthDeg, float elevationDeg, float distance) {
    if (j < 0 || j >= (int)dawTracks_.size()) return;
    const int slot = dawTracks_[(size_t)j].slot;
    if (slot < 0 || slot >= rk::bridge::kSlots) return;
    if (!host_.requestBridgeMove(slot, azimuthDeg, elevationDeg, distance)) return;
    // The plugin applies it within a few tens of milliseconds; until its parameters come
    // back through the bridge, show the dragged position so the track does not jump.
    dawMoves_[(size_t)slot] = {rk::toCartesian({azimuthDeg, elevationDeg, distance}), juce::Time::getMillisecondCounterHiRes() + 600.0};
    showStage();
    refreshSidebar();
}

std::vector<rkui::DawTrackState> MainComponent::currentDawStates() const {
    std::vector<rkui::DawTrackState> out;
    for (const auto& t : dawTracks_) {
        const auto p = host_.bridgeParams(t.slot);
        rkui::DawTrackState d;
        d.name = juce::String::fromUTF8(t.name.c_str());
        d.azimuthDeg = p.azimuthDeg;
        d.elevationDeg = p.elevationDeg;
        d.distance = p.distance;
        if (d.name.isNotEmpty()) out.push_back(d);
    }
    return out;
}

void MainComponent::storeSnapshot() {
    if ((int)scene_.snapshots.size() >= rkui::kMaxSnapshots) return;
    const juce::String name = tr("Szene") + " " + juce::String((int)scene_.snapshots.size() + 1);
    scene_.snapshots.push_back(rkui::captureSnapshot(scene_, currentDawStates(), name, 2.0f));
    activeSnapshot_ = (int)scene_.snapshots.size() - 1;
    commit();
    snapBar_.setSnapshots(scene_.snapshots);
    snapBar_.setActive(activeSnapshot_, 1.0f);
}

void MainComponent::recallSnapshot(int i) {
    if (i < 0 || i >= (int)scene_.snapshots.size()) return;
    activeSnapshot_ = i;
    fadeFrom_ = scene_;
    fadeDawFrom_ = currentDawStates();
    fadeStart_ = juce::Time::getMillisecondCounterHiRes();
    fadeMs_ = scene_.snapshots[(size_t)i].fadeSec * 1000.0;
    fading_ = true;
    stepSnapshotFade(fadeStart_); // a fade time of 0 lands at once
}

void MainComponent::stepSnapshotFade(double now) {
    if (activeSnapshot_ < 0 || activeSnapshot_ >= (int)scene_.snapshots.size()) {
        fading_ = false;
        return;
    }
    const rkui::Snapshot target = scene_.snapshots[(size_t)activeSnapshot_];
    const float raw = fadeMs_ <= 1.0 ? 1.0f : (float)juce::jlimit(0.0, 1.0, (now - fadeStart_) / fadeMs_);
    const float t = rkui::fadeCurve(raw);
    rkui::blendSnapshot(scene_, fadeFrom_, target, t);
    applyScene(false);
    // DAW tracks follow through their plugins (matched by name).
    for (const auto& to : target.daw)
        for (int j = 0; j < (int)dawTracks_.size(); ++j) {
            if (juce::String::fromUTF8(dawTracks_[(size_t)j].name.c_str()) != to.name) continue;
            rkui::DawTrackState from = to;
            for (const auto& f : fadeDawFrom_)
                if (f.name == to.name) from = f;
            const auto d = rkui::blendDaw(from, to, t);
            moveDawTrack(j, d.azimuthDeg, d.elevationDeg, d.distance);
        }
    showStage();
    reverbPanel_.setParams(scene_.reverb, scene_.reverbWetDb);
    snapBar_.setActive(activeSnapshot_, raw);
    if (raw >= 1.0f) {
        fading_ = false;
        commit();
        refreshSidebar();
        rebuildInspector();
    }
}

void MainComponent::snapshotMenu(int i) {
    if (i < 0 || i >= (int)scene_.snapshots.size()) return;
    juce::PopupMenu fade;
    const float cur = scene_.snapshots[(size_t)i].fadeSec;
    int id = 100;
    for (float sec : {0.0f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f})
        fade.addItem(id++, sec < 0.5f ? tr("Sofort") : rkui::num(sec, 0) + " s", true, std::abs(cur - sec) < 0.01f);
    juce::PopupMenu m;
    m.addItem(2, tr("Umbenennen …"));
    m.addSubMenu(tr("Überblendzeit"), fade);
    m.addSeparator();
    m.addItem(3, tr("Löschen"));
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&snapBar_).withMousePosition(), [this, i](int r) {
        if (i >= (int)scene_.snapshots.size()) return;
        auto& sn = scene_.snapshots[(size_t)i];
        if (r == 2) {
            auto* w = new juce::AlertWindow(tr("Szene umbenennen"), {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor("name", sn.name);
            w->addButton(tr("Umbenennen"), 1, juce::KeyPress(juce::KeyPress::returnKey));
            w->addButton(tr("Abbrechen"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
            w->enterModalState(true, juce::ModalCallbackFunction::create([this, i, w](int ok) {
                if (ok == 1 && i < (int)scene_.snapshots.size()) {
                    const juce::String n = w->getTextEditorContents("name").trim().substring(0, 64);
                    if (n.isNotEmpty()) scene_.snapshots[(size_t)i].name = n;
                    commit();
                    snapBar_.setSnapshots(scene_.snapshots);
                }
            }), true);
            return;
        } else if (r == 3) {
            scene_.snapshots.erase(scene_.snapshots.begin() + i);
            if (activeSnapshot_ == i) activeSnapshot_ = -1;
            else if (activeSnapshot_ > i) --activeSnapshot_;
            fading_ = false;
        } else if (r >= 100 && r < 106) {
            const float secs[] = {0.0f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f};
            sn.fadeSec = secs[r - 100];
        } else {
            return;
        }
        commit();
        snapBar_.setSnapshots(scene_.snapshots);
        snapBar_.setActive(activeSnapshot_, 1.0f);
    });
}

void MainComponent::selectRoomPreset(int i) {
    const auto& presets = rkui::roomPresets();
    if (i < 0 || i >= (int)presets.size()) return; // "Eigener" is the current state
    scene_.reverb = presets[(size_t)i].params;
    scene_.reverbWetDb = presets[(size_t)i].wetDb;
    applyScene(false);
    commit();
    refreshSidebar();
    rebuildInspector();
}

void MainComponent::selectSpeaker(int i) {
    selSpeaker_ = i;
    stage_.setSelectedSpeaker(i);
    showStage();
    refreshSidebar();
    rebuildInspector();
}

void MainComponent::setStep(int step) {
    if (calibrating_ && juce::jlimit(0, 3, step) != StepSpeakers) endCalibration();
    step_ = juce::jlimit(0, 3, step);
    if (step_ != StepOutput && speakerTestRunning_) {
        speakerTestRunning_ = false;
        host_.engine.speakerTest = -1;
    }
    refreshAll();
}

void MainComponent::rebuildInspector() {
    posSliders_ = {};
    dawPageTrack_ = -1;
    dawSliders_.clear();
    dawCard_ = nullptr;
    if (calibrating_) {
        if (phoneMode_) buildPhonePage();
        else buildCalibrationPage();
        return;
    }
    switch (step_) {
    case StepSpeakers:
        rkui::buildLayoutPage(inspector_, pageContext(), selSpeaker_, [this](int i) {
            juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this), i] { if (sp) sp->selectSpeaker(i); });
        });
        break;
    case StepRoom: {
        auto ctx = pageContext();
        rkui::buildRoomPage(inspector_, ctx);
        break;
    }
    case StepOutput: buildOutputPage(); break;
    default: buildSourcesPage(); break;
    }
}

void MainComponent::buildSourcesPage() {
    inspector_.clear();
    const int nScene = (int)scene_.sources.size();
    if (selSource_ >= nScene && selSource_ < nScene + (int)dawTracks_.size()) {
        buildDawTrackPage(selSource_ - nScene);
        return;
    }
    auto ctx = pageContext();
    if (selSource_ < 0 || selSource_ >= (int)scene_.sources.size()) {
        auto card = std::make_unique<rkui::StatusCard>();
        card->set(rkui::StatusCard::Kind::Info, tr("Noch keine Quelle"), tr("Ein Eingang des Interfaces oder ein Testklang."));
        inspector_.add(std::move(card), 64);
        auto* add = inspector_.add(rkui::makeButton("+ " + tr("Quelle hinzufügen"), true), 32);
        add->onClick = [this] { addSource(); };
        return;
    }
    const int i = selSource_;
    rkui::SourceInfo& s = scene_.sources[(size_t)i];
    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Quelle") + " " + juce::String(i + 1),
                                                         tr("Eine Quelle ist ein Eingang deines Audio-Interfaces oder ein eingebauter Testklang zum Ausprobieren.")), 26);
    auto* name = inspector_.add(std::make_unique<rkui::TextRow>(tr("Name")), 34);
    name->editor().setText(s.name, false);
    name->onCommit = [this, i](const juce::String& t) {
        if (i >= (int)scene_.sources.size() || scene_.sources[(size_t)i].name == t) return;
        scene_.sources[(size_t)i].name = t.substring(0, 64);
        commit();
        refreshSidebar();
    };

    juce::StringArray inputs;
    const int numIn = juce::jmax(2, host_.numDeviceInputs());
    for (int k = 0; k < numIn; ++k) inputs.add(tr("Eingang") + " " + juce::String(k + 1));
    inputs.add(tr("Testsignal") + ": " + tr("Rosa Rauschen"));
    inputs.add(tr("Testsignal") + ": " + tr("Sinuston"));
    inputs.add(tr("Testsignal") + ": " + tr("Klicks"));
    auto* input = inspector_.add(std::make_unique<rkui::ChoiceRow>(tr("Eingang"), inputs), 36);
    input->box().setSelectedItemIndex(s.test != rkui::TestSignal::Off ? numIn + (int)s.test - 1 : juce::jmin(s.inputChannel, numIn - 1), juce::dontSendNotification);
    input->onChange = [this, i, numIn](int idx) {
        if (i >= (int)scene_.sources.size()) return;
        auto& src = scene_.sources[(size_t)i];
        if (idx < numIn) { src.test = rkui::TestSignal::Off; src.inputChannel = idx; }
        else src.test = static_cast<rkui::TestSignal>(idx - numIn + 1);
        applyScene(false);
        commit();
        refreshSidebar();
    };

    auto setPolar = [this, i](int which, double v) {
        if (i >= (int)scene_.sources.size()) return;
        auto& src = scene_.sources[(size_t)i];
        rk::Polar p = rk::toPolar(src.position);
        if (p.distance < 0.001f) p.distance = 0.001f;
        if (which == 0) p.azimuthDeg = (float)v;
        if (which == 1) p.elevationDeg = (float)v;
        if (which == 2) p.distance = (float)v;
        src.position = rk::toCartesian(p);
        applyScene(false);
        showStage();
        refreshSidebar();
    };
    const rk::Polar pol = rk::toPolar(s.position);
    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Position"), tr("Vom Hörplatz aus: 0° ist vorne, positive Winkel liegen links. Am einfachsten in der Ansicht ziehen.")), 26);
    auto* az = inspector_.add(std::make_unique<rkui::ParamSlider>(tr("Azimut"), -180, 180, 0.5, degree(), 1, 0), 32);
    az->setValue(pol.azimuthDeg);
    az->onChange = [setPolar](double v) { setPolar(0, v); };
    az->onCommit = [this] { commit(); };
    auto* el = inspector_.add(std::make_unique<rkui::ParamSlider>(tr("Elevation"), -90, 90, 0.5, degree(), 1, 0), 32);
    el->setValue(pol.elevationDeg);
    el->onChange = [setPolar](double v) { setPolar(1, v); };
    el->onCommit = [this] { commit(); };
    auto* di = inspector_.add(std::make_unique<rkui::ParamSlider>(tr("Abstand"), 0, 50, 0.05, "m", 2, 0), 32);
    di->setSkewForCentre(5.0);
    di->setValue(pol.distance);
    di->onChange = [setPolar](double v) { setPolar(2, v); };
    di->onCommit = [this] { commit(); };
    posSliders_ = {az, el, di};

    auto addParam = [this, i](const juce::String& label, double lo, double hi, double step, const juce::String& unit, int dec,
                              double origin, double value, std::function<void(rkui::SourceInfo&, double)> set) {
        auto* p = inspector_.add(std::make_unique<rkui::ParamSlider>(label, lo, hi, step, unit, dec, origin), 32);
        p->setValue(value);
        p->onChange = [this, i, set](double v) {
            if (i >= (int)scene_.sources.size()) return;
            set(scene_.sources[(size_t)i], v);
            applyScene(false);
            showStage();
        };
        p->onCommit = [this] { commit(); };
        return p;
    };
    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Klang")), 26);
    addParam(tr("Pegel"), -60, 12, 0.1, "dB", 1, 0, s.gainDb, [](rkui::SourceInfo& x, double v) { x.gainDb = (float)v; });
    addParam(tr("Breite"), 0, 100, 1, "%", 0, 0, s.spread * 100.0, [](rkui::SourceInfo& x, double v) { x.spread = (float)v / 100.0f; })
        ->setTooltip(tr("0 % klingt punktförmig, 100 % verteilt die Quelle auf mehrere Lautsprecher."));
    auto* send = addParam(tr("Hall-Send"), -80, 6, 0.5, "dB", 1, -80, s.reverbSendDb, [](rkui::SourceInfo& x, double v) { x.reverbSendDb = (float)v; });
    send->formatter = [](double v) { return v <= -79.9 ? u8("\xe2\x88\x92\xe2\x88\x9e dB") : rkui::num(v, 1) + " dB"; };
    send->setValue(s.reverbSendDb);
    addParam(tr("Distanzeffekt"), 0, 200, 1, "%", 0, 0, s.distanceAmount * 100.0, [](rkui::SourceInfo& x, double v) { x.distanceAmount = (float)v / 100.0f; })
        ->setTooltip(tr("Wie stark Abstand hörbar wird: leiser, dumpfer und später. 0 % schaltet das aus."));

    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Abstrahlverhalten"), tr("Ein Sänger klingt von vorne hell und direkt, von hinten leiser und dumpfer. Der Hall-Send bleibt dabei gleich.")), 26);
    auto* bundle = addParam(tr("Richtwirkung"), 0, 100, 1, "%", 0, 0, s.directivity * 100.0, [](rkui::SourceInfo& x, double v) { x.directivity = (float)v / 100.0f; });
    bundle->setTooltip(tr("0 % strahlt in alle Richtungen gleich ab. Bei 100 % ist der Direktschall nach hinten 12 dB leiser und dumpfer."));
    auto* yaw = addParam(tr("Ausrichtung"), -180, 180, 1, degree(), 0, 0, s.yawDeg, [](rkui::SourceInfo& x, double v) { x.yawDeg = (float)v; });
    yaw->setTooltip(tr("0° schaut zum Hörplatz, 180° abgewandt. Wirkt nur mit Richtwirkung."));

    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Bewegung")), 26);
    auto* motion = inspector_.add(std::make_unique<rkui::Segmented>(juce::StringArray{tr("Aus"), tr("Kreisbahn")}, false), 32);
    motion->setSelected(s.orbit ? 1 : 0);
    motion->onChange = [this, i](int m) {
        if (i >= (int)scene_.sources.size()) return;
        scene_.sources[(size_t)i].orbit = m == 1;
        commit();
        refreshSidebar();
    };
    auto* speed = addParam(tr("Tempo"), -30, 30, 0.5, "U/min", 1, 0, s.orbitSpeed * 60.0, [](rkui::SourceInfo& x, double v) { x.orbitSpeed = (float)v / 60.0f; });
    speed->setTooltip(tr("Umdrehungen pro Minute, negativ = im Uhrzeigersinn"));
    auto* dop = inspector_.add(std::make_unique<juce::ToggleButton>(tr("Doppler-Effekt")), 28);
    dop->setToggleState(s.doppler, juce::dontSendNotification);
    dop->setTooltip(tr("Verzögert die Quelle um die Schalllaufzeit ihres Abstands (2,9 ms pro Meter). Bewegt sich die Quelle, ändert sich dadurch die Tonhöhe, höchstens um 10 %."));
    dop->onClick = [this, i, dop] {
        if (i >= (int)scene_.sources.size()) return;
        scene_.sources[(size_t)i].doppler = dop->getToggleState();
        applyScene(false);
        commit();
    };

    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Wiedergabe")), 26);
    auto* mute = inspector_.add(std::make_unique<juce::ToggleButton>(tr("Stumm")), 28);
    mute->setToggleState(s.mute, juce::dontSendNotification);
    mute->onClick = [this, i, mute] {
        if (i >= (int)scene_.sources.size()) return;
        scene_.sources[(size_t)i].mute = mute->getToggleState();
        applyScene(false);
        commit();
        refreshSidebar();
        showStage();
    };
    auto* solo = inspector_.add(std::make_unique<juce::ToggleButton>(tr("Solo")), 28);
    solo->setToggleState(s.solo, juce::dontSendNotification);
    solo->onClick = [this, i, solo] {
        if (i >= (int)scene_.sources.size()) return;
        scene_.sources[(size_t)i].solo = solo->getToggleState();
        applyScene(false);
        commit();
        refreshSidebar();
    };
    auto* rm = inspector_.add(rkui::makeButton(tr("Entfernen"), false, true), 30);
    rm->getProperties().set("danger", true);
    rm->onClick = [this, i] {
        if (i >= (int)scene_.sources.size()) return;
        scene_.sources.erase(scene_.sources.begin() + i);
        selSource_ = juce::jmin(i, (int)scene_.sources.size() - 1);
        applyScene(false);
        commit();
        juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) sp->refreshAll(); });
    };
    inspector_.addDetails(tr("Pegel, Breite und Hall-Send lassen sich mit einem Doppelklick auf den Regler zurücksetzen, ein Doppelklick auf den Wert erlaubt die Eingabe von Zahlen.\n\n"
                             "„Kreisbahn“ bewegt die Quelle von selbst um den Hörplatz, gut zum Prüfen eines Aufbaus.\n\n"
                             "Aus der DAW kommen Spuren über das Plugin „Raumklang Quelle“ dazu, ohne zusätzliche Programme für das Routing."));
}

void MainComponent::addDeviceRows() {
    auto& dm = host_.devices;
    auto later = [this] {
        juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) sp->rebuildInspector(); });
    };
    // Open every channel: one output per speaker, one input per source.
    auto apply = [this, later](juce::AudioDeviceManager::AudioDeviceSetup setup) {
        setup.useDefaultOutputChannels = false;
        setup.useDefaultInputChannels = false;
        setup.outputChannels.clear();
        setup.outputChannels.setRange(0, rk::kMaxSpeakers, true);
        setup.inputChannels.clear();
        if (!host_.inputsEnabled()) setup.inputDeviceName = {};
        if (setup.inputDeviceName.isNotEmpty()) setup.inputChannels.setRange(0, AudioHost::kMaxDeviceInputs, true);
        deviceError_ = host_.devices.setAudioDeviceSetup(setup, true);
        if (auto xml = host_.devices.createStateXml()) settings_->setValue("audioDevice", xml.get());
        later();
    };

    const auto& types = dm.getAvailableDeviceTypes();
    if (types.size() > 1) {
        juce::StringArray names;
        for (auto* t : types) names.add(t->getTypeName());
        auto* row = inspector_.add(std::make_unique<rkui::ChoiceRow>(tr("Treiber"), names), 26);
        row->box().setSelectedItemIndex(names.indexOf(dm.getCurrentAudioDeviceType()), juce::dontSendNotification);
        row->onChange = [this, names, later](int i) {
            host_.devices.setCurrentAudioDeviceType(names[i], true);
            deviceError_.clear();
            later();
        };
    }
    auto* type = dm.getCurrentDeviceTypeObject();
    if (type == nullptr) return;
    const auto setup = dm.getAudioDeviceSetup();
    const juce::String none = tr("Keines");

    juce::StringArray outs = type->getDeviceNames(false);
    auto* out = inspector_.add(std::make_unique<rkui::ChoiceRow>(type->hasSeparateInputsAndOutputs() ? tr("Ausgang") : tr("Gerät"),
                                                                 outs.isEmpty() ? juce::StringArray{none} : outs), 26);
    out->box().setSelectedItemIndex(juce::jmax(0, outs.indexOf(setup.outputDeviceName)), juce::dontSendNotification);
    out->box().setEnabled(!outs.isEmpty());
    out->onChange = [this, outs, apply](int i) {
        auto s = host_.devices.getAudioDeviceSetup();
        s.outputDeviceName = outs[i];
        if (!host_.devices.getCurrentDeviceTypeObject()->hasSeparateInputsAndOutputs()) s.inputDeviceName = outs[i];
        apply(s);
    };

    if (type->hasSeparateInputsAndOutputs()) {
        juce::StringArray ins = type->getDeviceNames(true);
        juce::StringArray items{none};
        items.addArray(ins);
        auto* in = inspector_.add(std::make_unique<rkui::ChoiceRow>(tr("Eingang"), items), 26);
        in->box().setSelectedItemIndex(juce::jmax(0, items.indexOf(setup.inputDeviceName)), juce::dontSendNotification);
        in->setTooltip(tr("Für Quellen, die einen Eingang des Interfaces nutzen. Testklänge brauchen keinen."));
        in->onChange = [this, items, apply](int i) {
            auto s = host_.devices.getAudioDeviceSetup();
            s.inputDeviceName = i == 0 ? juce::String() : items[i];
            apply(s);
        };
    }

    auto* dev = dm.getCurrentAudioDevice();
    if (dev == nullptr) return;
    juce::StringArray rateNames;
    const auto rates = dev->getAvailableSampleRates();
    for (double r : rates) rateNames.add(rkui::num(r / 1000.0, std::abs(r - std::round(r / 1000.0) * 1000.0) < 1.0 ? 0 : 1) + " kHz");
    if (!rates.isEmpty()) {
        auto* rate = inspector_.add(std::make_unique<rkui::ChoiceRow>(tr("Abtastrate"), rateNames), 26);
        rate->box().setSelectedItemIndex(juce::jmax(0, rates.indexOf(dev->getCurrentSampleRate())), juce::dontSendNotification);
        rate->setTooltip(tr("Plugins in der DAW brauchen dieselbe Abtastrate wie die App."));
        rate->onChange = [this, rates, apply](int i) {
            auto s = host_.devices.getAudioDeviceSetup();
            s.sampleRate = rates[i];
            apply(s);
        };
    }
    const auto sizes = dev->getAvailableBufferSizes();
    if (!sizes.isEmpty()) {
        juce::StringArray sizeNames;
        const double sr = juce::jmax(1.0, dev->getCurrentSampleRate());
        for (int b : sizes) sizeNames.add(juce::String(b) + " " + tr("Samples") + " (" + rkui::num(1000.0 * b / sr, 1) + " ms)");
        auto* buf = inspector_.add(std::make_unique<rkui::ChoiceRow>(tr("Puffer"), sizeNames), 26);
        buf->box().setSelectedItemIndex(juce::jmax(0, sizes.indexOf(dev->getCurrentBufferSizeSamples())), juce::dontSendNotification);
        buf->setTooltip(tr("Kleinere Puffer verringern die Latenz, größere schützen vor Aussetzern."));
        buf->onChange = [this, sizes, apply](int i) {
            auto s = host_.devices.getAudioDeviceSetup();
            s.bufferSize = sizes[i];
            apply(s);
        };
    }
}

void MainComponent::buildOutputPage() {
    inspector_.clear();
    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Audiogerät"), tr("Raumklang spielt direkt auf das Audio-Interface, ohne virtuelle Audiogeräte oder Routing-Programme. Ausgang 1 gehört zu Lautsprecher 1 und so weiter.")), 26);
    addDeviceRows();

    const int outs = host_.numDeviceOutputs();
    const int need = scene_.headphones ? 2 : (int)scene_.layout.speakers.size();
    const juce::String dot = ", ";
    auto card = std::make_unique<rkui::StatusCard>();
    bool problem = true;
    if (deviceError_.isNotEmpty()) card->set(rkui::StatusCard::Kind::Warn, tr("Gerät lässt sich nicht öffnen"), deviceError_);
    else if (outs == 0) card->set(rkui::StatusCard::Kind::Warn, tr("Kein Audiogerät"), tr("Oben ein Gerät wählen."));
    else if (outs < need) {
        card->set(rkui::StatusCard::Kind::Warn, tr("Zu wenige Ausgänge"),
                  juce::String(outs) + " " + tr("von") + " " + juce::String(need) + dot + tr("die übrigen Lautsprecher bleiben stumm"));
        card->setProgress(outs, need);
    } else problem = false;
    if (problem) {
        const int cardH = card->idealHeight(juce::jmax(200, inspector_.getWidth() - 24));
        inspector_.add(std::move(card), cardH);
    }

    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Ausgabe")), 26);
    auto* master = inspector_.add(std::make_unique<rkui::ParamSlider>(tr("Master-Pegel"), -60, 12, 0.1, "dB", 1, 0), 32);
    master->setValue(scene_.masterGainDb);
    master->onChange = [this](double v) { scene_.masterGainDb = (float)v; applyScene(false); };
    master->onCommit = [this] { commit(); };
    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Hören über"),
                                                         tr("Lautsprecher: Jeder Lautsprecher bekommt seinen eigenen Ausgang am Audio-Interface.\n\n"
                                                            "Kopfhörer, Anlage: Die eingerichteten Lautsprecher werden binaural in den Kopfhörer gerechnet, jeder aus seiner Richtung. So lässt sich ein Mix für die Anlage ohne Lautsprecher vorbereiten.\n\n"
                                                            "Kopfhörer, frei: Die Quellen werden direkt binaural um den Kopf gerechnet, unabhängig vom Lautsprecher-Layout. Für Mischungen, die über Kopfhörer gehört werden sollen.")), 26);
    auto* hp = inspector_.add(std::make_unique<rkui::OptionCards>(std::vector<rkui::OptionCards::Option>{
                                  {tr("Lautsprecher"), tr("Ein Ausgang pro Lautsprecher")},
                                  {tr("Kopfhörer, Anlage"), tr("Die Lautsprecher im Kopfhörer nachgebildet")},
                                  {tr("Kopfhörer, frei"), tr("Quellen frei um den Kopf, ohne Layout")}}),
                              3 * rkui::OptionCards::kRowHeight);
    hp->setSelected(!scene_.headphones ? 0 : (scene_.binaural ? 2 : 1));
    hp->onChange = [this](int m) {
        scene_.headphones = m > 0;
        scene_.binaural = m == 2;
        applyScene(true);
        commit();
        juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) { sp->rebuildInspector(); sp->refreshAll(); } });
    };
    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Schutz"), tr("Der Limiter ist die letzte Stufe vor jedem Ausgang. Unterhalb des Thresholds verändert er das Signal nicht.")), 26);
    auto* lim = inspector_.add(std::make_unique<juce::ToggleButton>(tr("Limiter")), 28);
    lim->setToggleState(scene_.limiter, juce::dontSendNotification);
    lim->setTooltip(tr("Nur für Messungen ausschalten."));
    lim->onClick = [this, lim] { scene_.limiter = lim->getToggleState(); applyScene(false); commit(); };
    inspector_.add(std::make_unique<rkui::NoteRow>(tr("Begrenzt jeden Lautsprecher auf einen Höchstpegel. So kann ein falsch eingestellter Regler oder ein Fehler in der DAW keinen Lautsprecher beschädigen.")), 52);
    auto* test = inspector_.add(rkui::makeButton(speakerTestRunning_ ? tr("Test beenden") : tr("Lautsprecher einzeln anspielen"), speakerTestRunning_), 32);
    test->onClick = [this] {
        speakerTestRunning_ = !speakerTestRunning_;
        speakerTestIndex_ = juce::jmax(0, selSpeaker_);
        speakerTestNext_ = 0.0;
        if (!speakerTestRunning_) host_.engine.speakerTest = -1;
        rebuildInspector();
        updateCoach();
    };
    inspector_.addDetails(tr("Der Lautsprechertest spielt rosa Rauschen nacheinander auf jedem Lautsprecher, 1,5 Sekunden lang. Der gerade spielende ist links und in der Ansicht markiert.\n\n"
                             "Unter Windows stehen WASAPI und, wenn mit dem ASIO-SDK gebaut, ASIO zur Wahl. Raumklang öffnet das Gerät selbst, ASIO4ALL oder JACK sind nicht nötig."));
}

void MainComponent::addSource() {
    if ((int)scene_.sources.size() >= rkui::kMaxSceneSources) return;
    rkui::SourceInfo s;
    const int n = (int)scene_.sources.size();
    s.name = tr("Quelle") + " " + juce::String(n + 1);
    s.inputChannel = juce::jmin(n, juce::jmax(0, host_.numDeviceInputs() - 1));
    float r = 2.0f;
    for (const auto& sp : scene_.layout.speakers) r = std::max(r, rk::length(sp.position) * 0.7f);
    s.position = rk::toCartesian({std::fmod(30.0f + 47.0f * (float)n, 360.0f) - 180.0f, 0.0f, r});
    scene_.sources.push_back(s);
    selSource_ = n;
    if (step_ != StepSources) step_ = StepSources;
    applyScene(false);
    commit();
    refreshAll();
}

void MainComponent::startDemo() {
    rkui::Scene s;
    s.name = "Demo";
    s.presetId = "ring";
    s.ringCount = 8;
    s.radius = 3.0f;
    rkui::regenerateLayout(s);
    s.masterGainDb = -10.0f;
    const struct { const char* name; rkui::TestSignal t; float az; float speed; } demo[] = {
        {"Klicks", rkui::TestSignal::Clicks, 45.0f, 0.03f},
        {"Rauschen", rkui::TestSignal::Pink, -120.0f, -0.017f},
        {"Ton", rkui::TestSignal::Sine, 170.0f, 0.0f},
    };
    for (const auto& d : demo) {
        rkui::SourceInfo src;
        src.name = tr(d.name);
        src.test = d.t;
        src.position = rk::toCartesian({d.az, 0.0f, 2.4f});
        src.orbit = d.speed != 0.0f;
        src.orbitSpeed = d.speed;
        src.gainDb = d.t == rkui::TestSignal::Pink ? -8.0f : -3.0f;
        s.sources.push_back(src);
    }
    // No multichannel interface: listen on headphones right away.
    if (host_.numDeviceOutputs() < (int)s.layout.speakers.size()) s.headphones = true;
    intro_.reset();
    step_ = StepSources;
    selSource_ = 0;
    loadScene(s, true);
    dirty_ = true;
}

void MainComponent::startEmpty() {
    rkui::Scene s;
    s.presetId = "ring";
    rkui::regenerateLayout(s);
    intro_.reset();
    step_ = StepSpeakers;
    loadScene(s, true);
    dirty_ = true;
}

void MainComponent::saveNow() {
    if (!dirty_) return;
    rkui::writeFileAtomically(rkui::appDataFolder().getChildFile("Autosave.raumklang"), rkui::sceneToJson(scene_));
    if (projectFile_ != juce::File()) rkui::writeFileAtomically(projectFile_, rkui::sceneToJson(scene_));
    dirty_ = false;
}

void MainComponent::openProject() {
    chooser_ = std::make_unique<juce::FileChooser>(tr("Öffnen …"), rkui::appDataFolder(), "*.raumklang");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const juce::File f = fc.getResult();
        if (f == juce::File()) return;
        rkui::Scene s;
        if (f.getSize() < 16 * 1024 * 1024 && rkui::sceneFromJson(f.loadFileAsString(), s)) {
            if (s.name.isEmpty()) s.name = f.getFileNameWithoutExtension();
            projectFile_ = f;
            loadScene(s, true);
        } else {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Raumklang",
                                                   u8("Die Datei ist kein Raumklang-Projekt oder besch\xc3\xa4" "digt."));
        }
    });
}

void MainComponent::saveProjectAs() {
    chooser_ = std::make_unique<juce::FileChooser>(tr("Sichern unter …"), rkui::appDataFolder().getChildFile((scene_.name.isEmpty() ? "Projekt" : scene_.name) + ".raumklang"), "*.raumklang");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
        juce::File f = fc.getResult();
        if (f == juce::File()) return;
        if (!f.hasFileExtension("raumklang")) f = f.withFileExtension("raumklang");
        scene_.name = f.getFileNameWithoutExtension();
        projectFile_ = f;
        dirty_ = true;
        commit();
        saveNow();
        repaint();
    });
}

void MainComponent::applyTheme() {
    lnf_.refreshColours();
    sendLookAndFeelChange();
    refreshAll();
}

void MainComponent::showHelpMenu() {
    juce::PopupMenu m;
    m.addSectionHeader("Raumklang " + juce::String(JUCE_APPLICATION_VERSION_STRING));
    m.addItem(1, tr("Sprache: Deutsch"), true, rkui::language() == rkui::Language::German);
    m.addItem(2, tr("Sprache: Englisch"), true, rkui::language() == rkui::Language::English);
    m.addSeparator();
    m.addItem(3, rkui::language() == rkui::Language::German ? "Dunkel" : "Dark", true, rkui::isDark());
    m.addItem(4, rkui::language() == rkui::Language::German ? "Hell" : "Light", true, !rkui::isDark());
    m.addSeparator();
    m.addItem(5, rkui::language() == rkui::Language::German ? u8("Einf\xc3\xbchrung zeigen") : "Show introduction");
    m.addSeparator();
    m.addSectionHeader(tr("Tastenkürzel"));
    const juce::String cmd = (juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX) != 0 ? u8("\xe2\x8c\x98") : "Strg+";
    m.addItem(10, cmd + "1 … " + cmd + "4   " + tr("Schritt wechseln"), false);
    m.addItem(11, cmd + "Z / " + cmd + u8("\xe2\x87\xa7Z   ") + tr("Rückgängig / Wiederholen"), false);
    m.addItem(12, cmd + "O / " + cmd + "S   " + tr("Öffnen / Sichern unter"), false);
    m.addItem(13, u8("\xe2\x87\xa7 ") + tr("beim Ziehen   Position rastet ein"), false);
    m.addItem(6, rkui::language() == rkui::Language::German ? "Projektseite auf GitHub" : "Project page on GitHub");
    m.addItem(7, tr("Nach Updates suchen …"));
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&helpBtn_), [this](int r) {
        if (r == 1 || r == 2) {
            rkui::setLanguage(r == 1 ? rkui::Language::German : rkui::Language::English);
            settings_->setValue("language", r == 1 ? "de" : "en");
            settings_->saveIfNeeded();
            steps_.setItems(juce::StringArray{tr("Lautsprecher"), tr("Quellen"), tr("Raum"), tr("Ausgabe")});
            updateTooltips();
            addBtn_->setButtonText("+ " + tr("Quelle hinzufügen"));
            refreshAll();
        } else if (r == 3 || r == 4) {
            rkui::setDarkMode(r == 3);
            settings_->setValue("theme", r == 3 ? "dark" : "light");
            settings_->saveIfNeeded();
            applyTheme();
        } else if (r == 5) {
            showIntro();
        } else if (r == 7) {
            updater_.check(this, true);
        } else if (r == 6) {
            juce::URL("https://github.com/BenjaminJMueller/immersive-audio-suite").launchInDefaultBrowser();
        }
    });
}

// ---------------------------------------------------------------------------

void MainComponent::timerCallback() {
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (fading_) stepSnapshotFade(now);
    if (now - deviceSummaryAt_ > 1000.0) {
        deviceSummaryAt_ = now;
        const juce::String d = host_.deviceSummary();
        if (d != deviceSummary_) {
            deviceSummary_ = d;
            repaint(0, 0, kSidebar + 60, kTopBar);
        }
    }

    // Moving sources ("Kreisbahn").
    const double dt = lastOrbitTime_ > 0.0 ? juce::jlimit(0.0, 0.2, (now - lastOrbitTime_) * 0.001) : 0.0;
    lastOrbitTime_ = now;
    bool moved = false;
    for (int i = 0; i < (int)scene_.sources.size(); ++i) {
        auto& s = scene_.sources[(size_t)i];
        if (!s.orbit || s.orbitSpeed == 0.0f) continue;
        const float a = (float)(s.orbitSpeed * dt * juce::MathConstants<double>::twoPi);
        const float c = std::cos(a), sn = std::sin(a);
        s.position = {s.position.x * c - s.position.y * sn, s.position.x * sn + s.position.y * c, s.position.z};
        host_.engine.source(i).setPosition(s.position);
        moved = true;
    }
    if (now - lastDawScan_ > 250.0) {
        lastDawScan_ = now;
        scanDaw();
        if (!dawTracks_.empty() && (step_ == StepSources || step_ == StepRoom)) refreshSidebar();
    }
    if (!dawTracks_.empty()) {
        // DAW tracks: redraw only when one of them moved or changed, not on every frame.
        juce::String key;
        for (const auto& t : dawTracks_) {
            const auto p = host_.bridgeParams(t.slot);
            key << t.slot << ':' << p.azimuthDeg << ',' << p.elevationDeg << ',' << p.distance << ',' << p.spread << ',' << p.stereoWidthDeg << ';';
        }
        for (const auto& mv : dawMoves_) if (now < mv.until) key << 'm';
        if (key != dawStageKey_) { dawStageKey_ = key; moved = true; }
    }
    if (moved) showStage();
    if (moved && (step_ == StepSources || step_ == StepRoom)) refreshSidebar();
    if (moved && posSliders_[0] != nullptr && selSource_ >= 0 && selSource_ < (int)scene_.sources.size() && scene_.sources[(size_t)selSource_].orbit)
        posSliders_[0]->setValue(rk::toPolar(scene_.sources[(size_t)selSource_].position).azimuthDeg);
    refreshDawValues();

    // Meters.
    static std::vector<float> spk(rk::kMaxSpeakers, 0.0f), src(rk::kMaxSources, 0.0f);
    const int nOut = scene_.headphones ? 2 : (int)scene_.layout.speakers.size();
    for (int c = 0; c < rk::kMaxSpeakers; ++c) spk[(size_t)c] = std::max(levelToUnit(host_.engine.takeOutputPeak(c)), spk[(size_t)c] * 0.85f);
    for (int s = 0; s < rk::kMaxSources; ++s) {
        src[(size_t)s] = std::max(levelToUnit(host_.engine.takeSourcePeak(s)), src[(size_t)s] * 0.85f);
        if (src[(size_t)s] > 0.05f) signalSeen_[(size_t)s] = now;
        sourceHasSignal_[(size_t)s] = now - signalSeen_[(size_t)s] < 2000.0;
    }
    std::vector<float> spkView(spk.begin(), spk.begin() + juce::jmax(0, scene_.headphones ? 0 : nOut));
    stage_.setSpeakerLevels(spkView);
    if (reverbPanel_.isVisible()) {
        reverbPanel_.setParams(scene_.reverb, scene_.reverbWetDb);
        std::array<float, rkui::SpectrumAnalyzer::kSize> buf;
        host_.readScope(buf.data(), (int)buf.size());
        analyzer_.process(buf.data(), host_.sampleRate());
        reverbPanel_.setBands(analyzer_.bandsDb());
    }
    std::vector<float> srcView(src.begin(), src.begin() + (long)scene_.sources.size());
    for (int j = 0; j < (int)dawTracks_.size(); ++j) {
        const int e = dawTrackLevelIndex(j);
        srcView.push_back(std::max(src[(size_t)e], src[(size_t)e + 1]));
    }
    stage_.setSourceLevels(srcView);
    {
        float outPeak = 0.0f;
        for (int c = 0; c < juce::jmin(nOut, rk::kMaxSpeakers); ++c) outPeak = std::max(outPeak, spk[(size_t)c]);
        outputStrip_.setLevel(outPeak);
        outputStrip_.setGainDb(scene_.masterGainDb);
    }
    // Level bars only where the rows are speakers or sources; room types have no level.
    if (step_ == StepSpeakers || step_ == StepOutput) sidebar_.setLevels(spkView);
    else if (step_ == StepSources) sidebar_.setLevels(srcView);
    else sidebar_.setLevels({});

    // Speaker test: 1.5 s per speaker, in order.
    if (speakerTestRunning_ && now >= speakerTestNext_) {
        const int n = (int)scene_.layout.speakers.size();
        if (n > 0) {
            speakerTestIndex_ = speakerTestNext_ == 0.0 ? speakerTestIndex_ % n : (speakerTestIndex_ + 1) % n;
            host_.engine.speakerTest = speakerTestIndex_;
            selSpeaker_ = speakerTestIndex_;
            sidebar_.setSelected(selSpeaker_);
            stage_.setSelectedSpeaker(selSpeaker_);
        }
        speakerTestNext_ = now + 1500.0;
    }

    if (calib_ != nullptr && calibrating_ && !phoneMode_) {
        const bool changed = calib_->poll();
        const int seen = (int)calib_->phase() * 100000 + calib_->currentPoint() * 1000 + calib_->done() + (calib_->message().isEmpty() ? 0 : 500);
        if (seen != calibSeen_) {
            calibSeen_ = seen;
            rebuildInspector();
            refreshSidebar();
            updateCoach();
        }
        if (changed) updateCalibrationOverlay();
    }
    if (calibrating_ && stage_.hasCalibration()) stage_.repaint();

    host_.engine.collectGarbage();

    // Autosave one second after the last change.
    if (dirty_ && now - lastChange_ > 1000.0) saveNow();

    const juce::String err = host_.takeError();
    if (err.isNotEmpty()) statusText_ = err;
    static int slow = 0;
    if (++slow % 15 == 0) {
        repaint(0, getHeight() - 52, kSidebar, 52); // progress footer
        updateCoach();
    }
}

void MainComponent::paint(juce::Graphics& g) {
    const auto& p = rkui::palette();
    g.fillAll(p.bg);
    // Top bar.
    g.setColour(p.bar);
    g.fillRect(0, 0, getWidth(), kTopBar);
    g.setColour(p.line);
    g.drawHorizontalLine(kTopBar - 1, 0.0f, (float)getWidth());
    g.setColour(p.text);
    g.setFont(rkui::uiFont(14.0f, true));
    g.drawText(scene_.name.isEmpty() ? tr("Neues Projekt") : scene_.name, 16, 8, 220, 18, juce::Justification::left);
    g.setColour(p.text3);
    g.setFont(rkui::uiFont(11.5f));
    juce::String sub = tr("Automatisch gesichert");
    if (host_.simulating()) sub << ", " << tr("Testraum");
    else if (deviceSummary_.isNotEmpty()) sub << ", " << deviceSummary_;
    g.drawText(sub, 16, 26, kSidebar + 40, 16, juce::Justification::left, true);
    // Sidebar.
    g.setColour(p.side);
    g.fillRect(0, kTopBar, kSidebar, getHeight() - kTopBar);
    g.setColour(p.line);
    g.drawVerticalLine(kSidebar - 1, (float)kTopBar, (float)getHeight());
    g.drawVerticalLine(getWidth() - kInspector, (float)kTopBar, (float)getHeight());
    g.setColour(p.side);
    g.fillRect(getWidth() - kInspector + 1, kTopBar, kInspector, getHeight() - kTopBar);
    // Toolbar separator between history and file buttons.
    g.setColour(p.lineStrong);
    g.fillRect((float)openBtn_.getX() - 7.0f, (float)openBtn_.getY() + 7.0f, 0.75f, (float)openBtn_.getHeight() - 14.0f);
    // Sidebar footer: progress in plain words, like "4 von 8 kalibriert".
    auto footer = juce::Rectangle<int>(16, getHeight() - 44, kSidebar - 32, 32);
    int done = 0, total = 0;
    juce::String what;
    if (step_ == StepSpeakers || step_ == StepOutput) {
        total = (int)scene_.layout.speakers.size();
        done = scene_.headphones ? total : juce::jmin(total, host_.numDeviceOutputs());
        what = scene_.headphones ? tr("über Kopfhörer") : tr("an Ausgängen");
    } else {
        total = (int)scene_.sources.size();
        for (int i = 0; i < total; ++i) done += sourceHasSignal_[(size_t)i] ? 1 : 0;
        for (int j = 0; j < (int)dawTracks_.size(); ++j) {
            const int e = dawTrackLevelIndex(j);
            done += (sourceHasSignal_[(size_t)e] || sourceHasSignal_[(size_t)e + 1]) ? 1 : 0;
        }
        total += (int)dawTracks_.size();
        what = tr("mit Signal");
    }
    g.setColour(p.text2);
    g.setFont(rkui::uiFont(12.0f));
    const juce::String info = statusText_.isNotEmpty() ? statusText_
                                                       : juce::String(done) + " " + tr("von") + " " + juce::String(total) + " " + what;
    g.drawText(info, footer.removeFromTop(18), juce::Justification::centredLeft, true);
    auto bar = footer.removeFromTop(10).withSizeKeepingCentre(footer.getWidth(), 4).toFloat();
    g.setColour(p.select);
    g.fillRoundedRectangle(bar, 2.0f);
    if (total > 0) {
        g.setColour(done >= total ? p.ok : p.warn);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * (float)done / (float)total), 2.0f);
    }
}

void MainComponent::resized() {
    auto r = getLocalBounds();
    auto top = r.removeFromTop(kTopBar);
    const int sw = juce::jmin(steps_.idealWidth(), top.getWidth() - 560);
    steps_.setBounds(top.withSizeKeepingCentre(sw, 34));
    auto right = top.reduced(12, 12);
    helpBtn_.setBounds(right.removeFromRight(30));
    saveBtn_.setBounds(right.removeFromRight(30));
    openBtn_.setBounds(right.removeFromRight(30));
    right.removeFromRight(13); // separator
    redoBtn_.setBounds(right.removeFromRight(30));
    undoBtn_.setBounds(right.removeFromRight(30));

    auto side = r.removeFromLeft(kSidebar);
    side.removeFromBottom(52);
    outputStrip_.setBounds(side.removeFromBottom(64));
    if (addBtn_->isVisible()) addBtn_->setBounds(side.removeFromBottom(44).reduced(12, 6));
    sidebarView_.setBounds(side.reduced(0, 4));
    sidebar_.setSize(sidebarView_.getMaximumVisibleWidth(), sidebar_.contentHeight());
    inspector_.setBounds(r.removeFromRight(kInspector).reduced(0, 8));
    snapBar_.setBounds(r.removeFromBottom(48));
    stage_.setBounds(r);
    reverbPanel_.setBounds(r);
    if (intro_ != nullptr) intro_->setBounds(getLocalBounds());
}

void MainComponent::updateTooltips() {
    undoBtn_.setTooltip(tr("Rückgängig"));
    redoBtn_.setTooltip(tr("Wiederholen"));
    openBtn_.setTooltip(tr("Öffnen …"));
    saveBtn_.setTooltip(tr("Sichern unter …"));
    helpBtn_.setTooltip(tr("Hilfe"));
}

void MainComponent::showIntro() {
    intro_ = std::make_unique<IntroOverlay>();
    intro_->onDemo = [this] { startDemo(); };
    intro_->onEmpty = [this] { startEmpty(); };
    addAndMakeVisible(*intro_);
    resized();
    intro_->grabKeyboardFocus();
}

void MainComponent::updateCoach() {
    // One short instruction for what to do next, like Projection Studio's coach bar.
    const int outs = host_.numDeviceOutputs();
    const int nSpk = (int)scene_.layout.speakers.size();
    const rk::LayoutInfo info = rk::analyze(scene_.layout);
    const bool layoutOk = info.numActive > 0 && std::find(info.hints.begin(), info.hints.end(), "single_speaker") == info.hints.end();
    if (calibrating_) {
        if (phoneMode_) {
            stage_.setCoach("1", tr("iPhone-Messung prüfen"), tr("Gestrichelt: bisher. Rechts die Zuordnung wählen und übernehmen."));
        } else if (proposal_) {
            stage_.setCoach({}, tr("Ergebnis prüfen"), tr("Gestrichelt: bisher. Rechts übernehmen oder verwerfen."), true);
        } else if (calib_ != nullptr && calib_->phase() == Calibration::Phase::Measuring) {
            stage_.setCoach("M" + juce::String(calib_->currentPoint() + 1), tr("Bitte leise sein"),
                            tr("Lautsprecher") + " " + juce::String(calib_->playingSpeaker() + 1) + " " + tr("spielt den Messton") + ", " +
                                juce::String(calib_->done() + 1) + " " + tr("von") + " " + juce::String(calib_->total()));
        } else if (calib_ != nullptr && calib_->message().isNotEmpty()) {
            stage_.setCoach("!", tr("Bitte prüfen"), calib_->message());
        } else if (calib_ != nullptr && calib_->nextPoint() >= 0) {
            const int j = calib_->nextPoint();
            stage_.setCoach("M" + juce::String(j + 1), tr("Mikrofon auf Messpunkt") + " " + juce::String(j + 1) + " " + tr("stellen"), calib_->describePoint(j));
        } else {
            stage_.setCoach({}, tr("Alle Punkte gemessen"), tr("Rechts das Ergebnis zeigen."), true);
        }
        return;
    }
    switch (step_) {
    case StepSpeakers:
        if (!layoutOk) stage_.setCoach("1", tr("Vorlage wählen"), tr("Rechts eine Vorlage wählen oder Lautsprecher hinzufügen."));
        else if (selSpeaker_ < 0) stage_.setCoach("1", tr("Lautsprecher anpassen"), tr("Ziehen verschiebt sie, mit ⇧ rastet die Position ein."));
        else stage_.setCoach({}, tr("Lautsprecher stehen"), tr("Weiter mit 2 Quellen."), true);
        break;
    case StepSources:
        if (scene_.sources.empty()) stage_.setCoach("2", tr("Quelle hinzufügen"), tr("Links unten auf „+ Quelle hinzufügen“ klicken."));
        else stage_.setCoach("2", tr("Quellen im Raum platzieren"), tr("Ziehen verschiebt sie, mit ⇧ rastet die Position ein. Mausrad zoomt."));
        break;
    case StepRoom:
        stage_.setCoach({}, {}, {});
        reverbPanel_.setCoach("3", tr("Raum einstellen"), tr("Raumtyp wählen, dann die Punkte im Bild ziehen."));
        break;
    default:
        if (outs == 0) stage_.setCoach("4", tr("Audiogerät wählen"), tr("Rechts das Audio-Interface auswählen."));
        else if (!scene_.headphones && outs < nSpk)
            stage_.setCoach("!", tr("Zu wenige Ausgänge"), juce::String(outs) + " " + tr("von") + " " + juce::String(nSpk) + ". " + tr("Anderes Gerät wählen oder unter Ausgabe auf Kopfhörer umschalten."));
        else if (speakerTestRunning_) stage_.setCoach("4", tr("Hörst du den markierten Lautsprecher?"), tr("Sonst Kabel und Reihenfolge der Ausgänge prüfen."));
        else stage_.setCoach({}, tr("Bereit"), tr("Mit „Lautsprecher einzeln anspielen“ die Verkabelung prüfen."), true);
        break;
    }
}

bool MainComponent::keyPressed(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (key.getKeyCode() == 'Z' && mods.isCommandDown()) {
        if (mods.isShiftDown()) redo();
        else undo();
        return true;
    }
    if (key.getKeyCode() == 'Y' && mods.isCommandDown()) { redo(); return true; }
    if (key.getKeyCode() == 'S' && mods.isCommandDown()) { saveProjectAs(); return true; }
    if (key.getKeyCode() == 'O' && mods.isCommandDown()) { openProject(); return true; }
    if (key.getKeyCode() >= '1' && key.getKeyCode() <= '4' && mods.isCommandDown()) { setStep(key.getKeyCode() - '1'); return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Speaker calibration

void MainComponent::startCalibration() {
    if (step_ != StepSpeakers) setStep(StepSpeakers);
    speakerTestRunning_ = false;
    host_.engine.speakerTest = -1;
    calibrating_ = true;
    phoneMode_ = false;
    proposal_.reset();
    proposalOpts_ = {};
    calibSettings_.micInput = juce::jlimit(0, juce::jmax(0, host_.numDeviceInputs() - 1), calibSettings_.micInput);
    // In a donut not every point hears every speaker: start with at least 8 points there.
    if (rk::analyze(scene_.layout).facesInwardAndOutward) calibSettings_.numPoints = juce::jmax(calibSettings_.numPoints, 8);
    ensureInputs();
    calib_ = std::make_unique<Calibration>(host_, scene_.layout, calibSettings_);
    calibSeen_ = -1;
    refreshAll();
}

void MainComponent::restartCalibration() {
    // Settings changed before anything was measured: new measuring points, new sweep.
    if (calib_ != nullptr && calib_->measuredPoints() > 0) return;
    calib_ = std::make_unique<Calibration>(host_, scene_.layout, calibSettings_);
    calibSeen_ = -1;
    showStage();
    rebuildInspector();
    updateCoach();
}

void MainComponent::endCalibration() {
    if (calib_ != nullptr) calib_->cancel();
    calib_.reset();
    calibrating_ = false;
    phoneMode_ = false;
    proposal_.reset();
    stage_.clearCalibration();
    calibSeen_ = -1;
}

void MainComponent::evaluateCalibration() {
    if (calib_ == nullptr || !calib_->evaluate()) {
        rebuildInspector();
        updateCoach();
        return;
    }
    proposal_ = calib_->proposal();
    proposalOpts_ = {};
    proposalOpts_.positions = proposal_->canPositions;
    refreshAll();
}

void MainComponent::acceptProposal() {
    if (!proposal_) return;
    const bool moved = proposal_->replaces || (proposalOpts_.positions && proposal_->canPositions);
    scene_.layout = rkui::applyProposal(scene_.layout, *proposal_, proposalOpts_);
    if (moved) scene_.presetId = "custom";
    selSpeaker_ = juce::jmin(selSpeaker_, (int)scene_.layout.speakers.size() - 1);
    endCalibration();
    applyScene(true);
    commit(); // one undo step, also shared with the plugins (e.g. in Logic)
    refreshAll();
}

void MainComponent::updateCalibrationOverlay() {
    if (!calibrating_) {
        if (stage_.hasCalibration()) stage_.clearCalibration();
        return;
    }
    rkui::StageView::CalibrationOverlay o;
    if (calib_ != nullptr && !phoneMode_ && !proposal_) {
        o.points = calib_->points();
        o.pointState = calib_->pointState();
        o.activePoint = calib_->phase() == Calibration::Phase::Measuring ? calib_->currentPoint() : calib_->nextPoint();
        o.playingSpeaker = calib_->playingSpeaker();
    }
    if (proposal_) {
        const auto preview = rkui::applyProposal(scene_.layout, *proposal_, proposalOpts_);
        if (!proposal_->replaces)
            for (size_t i = 0; i < scene_.layout.speakers.size() && i < preview.speakers.size(); ++i) {
                o.before.push_back(scene_.layout.speakers[i].position);
                o.moved.push_back(rk::length(scene_.layout.speakers[i].position - preview.speakers[i].position) > 0.01f ? 1 : 0);
            }
    }
    stage_.setCalibration(std::move(o));
}

juce::String MainComponent::calibrationSubtitle(int m) const {
    const juce::String dot = ", ";
    if (proposal_) {
        if (m >= (int)proposal_->speakers.size()) return {};
        const auto& q = proposal_->speakers[(size_t)m];
        juce::String t = q.note;
        if (t.isEmpty() && q.hasPosition && m < (int)scene_.layout.speakers.size()) {
            // e.g. after an iPhone survey: how far the speaker moves.
            const float d = rk::length(q.position - scene_.layout.speakers[(size_t)m].position);
            t = d < 0.01f ? tr("Unverändert") : tr("Verschoben um") + " " + juce::String(juce::roundToInt(d * 100.0f)) + " cm";
        }
        if (q.measured && q.hasGain) t << dot << rkui::num(q.gainDb, 1) << " dB";
        return t;
    }
    if (calib_ == nullptr) return {};
    if (calib_->playingSpeaker() == m) return tr("Misst") + u8("\xe2\x80\xa6");
    int heard = 0, tried = 0;
    for (int j = 0; j < (int)calib_->points().size(); ++j) {
        const auto& c = calib_->cell(m, j);
        if (!c.measured) continue;
        ++tried;
        if (c.m.arrival.valid && !c.m.clipped) ++heard;
    }
    if (tried == 0) return tr("Ausgang") + " " + juce::String(m + 1);
    return tr("Erkannt an") + " " + juce::String(heard) + " " + tr("von") + " " + juce::String(tried) + " " + tr("Messpunkten");
}

void MainComponent::buildCalibrationPage() {
    inspector_.clear();
    if (calib_ == nullptr) return;
    const juce::String dot = ", ";
    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Einmessen mit Mikrofon"),
        tr("Ein Messmikrofon (oder ein gutes Kugelmikrofon) nach oben zeigend auf ein Stativ, auf Ohrhöhe. Jeder Lautsprecher spielt nacheinander einen kurzen Gleitton. "
           "Aus der Laufzeit folgt der Abstand, aus mehreren Messpunkten die genaue Position. Laufzeit und Pegel werden am Messpunkt 1 ausgeglichen.")), 26);

    if (proposal_) {
        const auto& v = calib_->verdict();
        auto card = std::make_unique<rkui::StatusCard>();
        card->set(v.ok ? rkui::StatusCard::Kind::Ok : rkui::StatusCard::Kind::Warn, v.title, v.detail);
        card->setProgress(v.good, v.total);
        const int h = card->idealHeight(juce::jmax(200, inspector_.getWidth() - 24));
        inspector_.add(std::move(card), h);
        inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Übernehmen"), tr("Was ins Layout übernommen wird. Alles lässt sich mit Rückgängig zurücknehmen.")), 26);
        auto toggle = [this](const juce::String& name, bool& flag, bool enabled) {
            auto* t = inspector_.add(std::make_unique<juce::ToggleButton>(name), 28);
            t->setToggleState(flag && enabled, juce::dontSendNotification);
            t->setEnabled(enabled);
            t->onClick = [this, t, &flag] {
                flag = t->getToggleState();
                showStage();
                refreshSidebar();
            };
        };
        toggle(tr("Positionen"), proposalOpts_.positions, proposal_->canPositions);
        toggle(tr("Laufzeit angleichen"), proposalOpts_.delays, proposal_->canDelays);
        toggle(tr("Pegel angleichen"), proposalOpts_.levels, proposal_->canLevels);
        auto* ok = inspector_.add(rkui::makeButton(tr("Übernehmen"), true), 32);
        ok->onClick = [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) sp->acceptProposal(); }); };
        auto* more = inspector_.add(rkui::makeButton(tr("Weiter messen"), false, true), 32);
        more->onClick = [this] {
            proposal_.reset();
            if (calib_ != nullptr) calib_->reopen();
            juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) sp->refreshAll(); });
        };
        auto* discard = inspector_.add(rkui::makeButton(tr("Verwerfen"), false, true), 32);
        discard->onClick = [this] {
            juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) { sp->endCalibration(); sp->refreshAll(); } });
        };
        inspector_.addDetails(tr("Gestrichelt ist, wo die Lautsprecher bisher standen. Positionen werden nur übernommen, wenn sie von mindestens vier Messpunkten bestätigt sind.\n\n"
                                 "Laufzeit: alle Lautsprecher klingen am Messpunkt 1 gleichzeitig. Pegel: alle gleich laut, leiser gestellt wird nur, was zu laut ist.\n\n"
                                 "Die Ausrichtung bleibt, wie sie war (zur Mitte, nach außen oder nach innen). Für die genaue Richtung jedes Lautsprechers die iPhone-Messung verwenden."));
        return;
    }

    const bool locked = calib_->measuredPoints() > 0 || calib_->phase() == Calibration::Phase::Measuring;
    const juce::StringArray inputs = host_.inputNames();
    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Messung")), 26);
    auto* mic = inspector_.add(std::make_unique<rkui::ChoiceRow>(tr("Mikrofon"), inputs.isEmpty() ? juce::StringArray{tr("Kein Eingang")} : inputs), 36);
    mic->box().setSelectedItemIndex(juce::jlimit(0, juce::jmax(0, inputs.size() - 1), calibSettings_.micInput), juce::dontSendNotification);
    mic->setEnabled(!locked && !inputs.isEmpty());
    mic->onChange = [this](int i) { calibSettings_.micInput = i; restartCalibration(); };
    auto* mode = inspector_.add(std::make_unique<rkui::Segmented>(juce::StringArray{tr("Schnell"), tr("Mit Positionen")}, false), 32);
    mode->setTooltip(tr("Schnell: ein Messpunkt, gleicht Laufzeit und Pegel aus. Mit Positionen: mehrere Messpunkte, misst auch, wo die Lautsprecher stehen."));
    mode->setSelected(calibSettings_.positions ? 1 : 0);
    mode->setEnabled(!locked);
    mode->onChange = [this](int i) { calibSettings_.positions = i == 1; restartCalibration(); };
    if (calibSettings_.positions) {
        auto* n = inspector_.add(std::make_unique<rkui::ParamSlider>(tr("Messpunkte"), 4, 12, 1, "", 0, 4), 32);
        n->setTooltip(tr("Mehr Punkte machen die Messung genauer. Beim Donut sieht nicht jeder Punkt jeden Lautsprecher, dort 8 oder mehr."));
        n->setValue(calibSettings_.numPoints);
        n->setEnabledState(!locked);
        n->onChange = [this](double v) { calibSettings_.numPoints = (int)v; };
        n->onCommit = [this] { restartCalibration(); };
    }
    auto* lvl = inspector_.add(std::make_unique<rkui::ParamSlider>(tr("Messlautstärke"), -40, -6, 1, "dB", 0, -40), 32);
    lvl->setTooltip(tr("So laut, dass der Messton deutlich über dem Raumgeräusch liegt. Bei „Zu laut“ leiser stellen."));
    lvl->setValue(calibSettings_.levelDb);
    lvl->setEnabledState(calib_->phase() != Calibration::Phase::Measuring);
    lvl->onChange = [this](double v) { calibSettings_.levelDb = (float)v; };
    lvl->onCommit = [this] { restartCalibration(); };

    // Status in plain words.
    auto card = std::make_unique<rkui::StatusCard>();
    const int total = (int)calib_->points().size();
    const juce::String problem = calib_->problemBeforeStart();
    if (problem.isNotEmpty()) {
        card->set(rkui::StatusCard::Kind::Warn, tr("Noch nicht bereit"), problem);
    } else if (calib_->phase() == Calibration::Phase::Measuring) {
        card->set(rkui::StatusCard::Kind::Info, tr("Messpunkt") + " " + juce::String(calib_->currentPoint() + 1) + dot + tr("bitte leise sein"),
                  tr("Lautsprecher") + " " + juce::String(juce::jmin(calib_->done() + 1, calib_->total())) + " " + tr("von") + " " + juce::String(calib_->total()));
        card->setProgress(calib_->done(), calib_->total());
    } else if (calib_->message().isNotEmpty()) {
        card->set(rkui::StatusCard::Kind::Warn, tr("Bitte prüfen"), calib_->message());
        card->setProgress(calib_->measuredPoints(), total);
    } else if (calib_->nextPoint() >= 0) {
        const int j = calib_->nextPoint();
        card->set(rkui::StatusCard::Kind::Info, tr("Mikrofon auf Messpunkt") + " " + juce::String(j + 1), calib_->describePoint(j));
        card->setProgress(calib_->measuredPoints(), total);
    } else {
        card->set(rkui::StatusCard::Kind::Ok, tr("Alle Punkte gemessen"), juce::String(total) + " " + tr("von") + " " + juce::String(total));
        card->setProgress(total, total);
    }
    const int h = card->idealHeight(juce::jmax(200, inspector_.getWidth() - 24));
    inspector_.add(std::move(card), h);

    // Actions, added main action first: it ends up rightmost (measure, evaluate, leave).
    if (calib_->phase() == Calibration::Phase::Measuring) {
        auto* stop = inspector_.add(rkui::makeButton(tr("Abbrechen"), false, true), 32);
        stop->onClick = [this] { if (calib_ != nullptr) calib_->cancel(); calibSeen_ = -1; };
    } else {
        const int next = calib_->nextPoint();
        const int needed = calibSettings_.positions ? 4 : 1;
        if (next >= 0) {
            const bool repeat = calib_->pointState()[(size_t)next] == 2;
            auto* go = inspector_.add(rkui::makeButton((repeat ? tr("%1 wiederholen") : tr("%1 messen")).replace("%1", "M" + juce::String(next + 1)), true), 32);
            go->setEnabled(problem.isEmpty());
            go->onClick = [this, next] {
                if (calib_ != nullptr) calib_->measurePoint(next);
                calibSeen_ = -1;
            };
        }
        if (calib_->measuredPoints() >= 1) {
            auto* eval = inspector_.add(rkui::makeButton(tr("Ergebnis zeigen"), next < 0 || calib_->measuredPoints() >= total), 32);
            if (calib_->measuredPoints() < needed) eval->setTooltip(tr("Für Positionen mindestens 4 Messpunkte. Mit weniger nur Laufzeit und Pegel."));
            eval->onClick = [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) sp->evaluateCalibration(); }); };
        }
        auto* leave = inspector_.add(rkui::makeButton(tr("Beenden"), false, true), 32);
        leave->onClick = [this] {
            juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) { sp->endCalibration(); sp->refreshAll(); } });
        };
    }

    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Genauer"), tr("Mit einem gemessenen Abstand (Zollstock oder Laser) von Messpunkt 1 zu Lautsprecher 1 wird die Messung auch mit wenigen Punkten genau. "
                                                                           "Sonst rechnet Raumklang die Verzögerung des Audio-Interfaces aus den Messpunkten heraus.")), 26);
    auto* tape = inspector_.add(std::make_unique<rkui::ParamSlider>(tr("Abstand M1 zu 1"), 0, 30, 0.01, "m", 2, 0), 32);
    tape->formatter = [](double v) { return v < 0.05 ? tr("Aus") : rkui::num(v, 2) + " m"; };
    tape->setValue(calibSettings_.tapeMetres);
    tape->setEnabledState(calib_->phase() != Calibration::Phase::Measuring);
    tape->onChange = [this](double v) { calibSettings_.tapeMetres = (float)v; };
    tape->onCommit = [this] { if (calib_ != nullptr && calib_->measuredPoints() == 0) restartCalibration(); };
    auto* temp = inspector_.add(std::make_unique<rkui::ParamSlider>(tr("Raumtemperatur"), 0, 40, 0.5, juce::String(juce::CharPointer_UTF8("\xc2\xb0""C")), 1, 0), 32);
    temp->setValue(calibSettings_.temperature);
    temp->setTooltip(tr("Die Schallgeschwindigkeit steigt um etwa 0,6 m/s pro °C. 5 °C Abweichung ergeben auf 5 m etwa 4 cm Fehler."));
    temp->onChange = [this](double v) { calibSettings_.temperature = (float)v; };
    temp->onCommit = [this] { if (calib_ != nullptr && calib_->measuredPoints() == 0) restartCalibration(); };
    inspector_.addDetails(tr("Ablauf: Mikrofon auf den angezeigten Messpunkt stellen, „Messen“ drücken und still sein. Jeder Lautsprecher spielt etwa 2,5 Sekunden. Dann zum nächsten Punkt.\n\n"
                             "Messpunkt 1 ist der Hörplatz. Die anderen liegen rundherum, einer davon höher, damit auch die Höhe der Lautsprecher bestimmt werden kann. "
                             "Auf 10 cm genau aufstellen reicht, den Rest rechnet Raumklang heraus.\n\n"
                             "Beim Donut liegen die Punkte im Ring zwischen innen und außen. Dass die Säule manche Lautsprecher verdeckt, ist eingeplant.\n\n"
                             "Während der Messung schweigen alle Quellen und die DAW-Spuren."));
}

void MainComponent::buildPhonePage() {
    inspector_.clear();
    const juce::String dot = ", ";
    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("iPhone-Messung"), tr("Positionen und Ausrichtung aus der App „Raumklang Messen“. Laufzeit und Pegel bleiben, wie sie sind. Danach lohnt sich „Mit Mikrofon einmessen“ im Modus „Schnell“.")), 26);
    auto card = std::make_unique<rkui::StatusCard>();
    const int nm = (int)survey_.speakers.size(), ns = (int)scene_.layout.speakers.size();
    const juce::String from = survey_.source.isNotEmpty() ? survey_.source : juce::String("iPhone");
    if (nm == ns || surveyMatch_ == 2)
        card->set(rkui::StatusCard::Kind::Ok, juce::String(nm) + " " + tr("Lautsprecher gemessen"), from);
    else
        card->set(rkui::StatusCard::Kind::Warn, juce::String(nm) + " " + tr("von") + " " + juce::String(ns) + " " + tr("Lautsprechern gemessen"),
                  from + dot + tr("die übrigen bleiben, wo sie sind"));
    const int h = card->idealHeight(juce::jmax(200, inspector_.getWidth() - 24));
    inspector_.add(std::move(card), h);
    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Zuordnung"), tr("Reihenfolge: der zuerst angetippte Lautsprecher wird Lautsprecher 1 (Ausgang 1) und so weiter.\n\n"
                                                                             "Nach Nähe: jeder Lautsprecher bekommt die gemessene Position, die seiner bisherigen am nächsten liegt.\n\n"
                                                                             "Neues Layout: genau die gemessenen Lautsprecher, in der angetippten Reihenfolge.")), 26);
    auto* match = inspector_.add(std::make_unique<rkui::Segmented>(juce::StringArray{tr("Reihenfolge"), tr("Nach Nähe"), tr("Neues Layout")}, false), 32);
    match->setSelected(surveyMatch_);
    match->onChange = [this](int i) {
        surveyMatch_ = i;
        proposal_ = rkui::proposalFromSurvey(scene_.layout, survey_, (rkui::SurveyMatch)i);
        juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) sp->refreshAll(); });
    };
    auto* ok = inspector_.add(rkui::makeButton(tr("Übernehmen"), true), 32);
    ok->onClick = [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) sp->acceptProposal(); }); };
    auto* discard = inspector_.add(rkui::makeButton(tr("Verwerfen"), false, true), 32);
    discard->onClick = [this] {
        juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) { sp->endCalibration(); sp->refreshAll(); } });
    };
    inspector_.addDetails(tr("Die App misst mit LiDAR, wo jeder Lautsprecher hängt und wohin er zeigt. Der Hörplatz und die Richtung „vorne“ werden in der App festgelegt.\n\n"
                             "Die Datei kommt per AirDrop, Mail oder Cloud auf den Rechner und lässt sich einfach auf das Raumklang-Fenster ziehen."));
}

void MainComponent::chooseSurveyFile() {
    chooser_ = std::make_unique<juce::FileChooser>(tr("iPhone-Messung laden"), juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Downloads"),
                                                   "*.raumklang-messung;*.json");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [sp = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& fc) {
                              if (sp == nullptr) return;
                              const juce::File f = fc.getResult();
                              if (f.existsAsFile()) sp->loadSurvey(f);
                          });
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".raumklang-messung") || f.endsWithIgnoreCase(".json")) return true;
    return false;
}

void MainComponent::filesDropped(const juce::StringArray& files, int, int) {
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".raumklang-messung") || f.endsWithIgnoreCase(".json")) {
            loadSurvey(juce::File(f));
            return;
        }
}

void MainComponent::loadSurvey(const juce::File& file) {
    juce::String error;
    rkui::Survey s;
    const bool ok = file.getSize() < 8 * 1024 * 1024 && rkui::surveyFromJson(file.loadFileAsString(), s, error);
    if (!ok) {
        if (error.isEmpty()) error = "Die Datei ist zu groß.";
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, tr("iPhone-Messung laden"), tr(error));
        return;
    }
    if (step_ != StepSpeakers) setStep(StepSpeakers);
    endCalibration();
    speakerTestRunning_ = false;
    host_.engine.speakerTest = -1;
    survey_ = std::move(s);
    calibrating_ = true;
    phoneMode_ = true;
    surveyMatch_ = (int)survey_.speakers.size() == (int)scene_.layout.speakers.size() ? 0 : 2;
    proposal_ = rkui::proposalFromSurvey(scene_.layout, survey_, (rkui::SurveyMatch)surveyMatch_);
    proposalOpts_ = {};
    refreshAll();
}

} // namespace rkapp
