#include "MainComponent.h"

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
        else selectSource(row);
    };
    sidebarView_.setViewedComponent(&sidebar_, false);
    sidebarView_.setScrollBarsShown(true, false);
    sidebarView_.setScrollBarThickness(8);
    addAndMakeVisible(sidebarView_);

    stage_.onSourceMoved = [this](int i, rk::Vec3 p) {
        if (i < (int)scene_.sources.size()) {
            scene_.sources[(size_t)i].position = p;
            applyScene(false);
            refreshSidebar();
            if (i == selSource_ && step_ == StepSources) rebuildInspector();
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
    addAndMakeVisible(inspector_);

    // Audio: open the device stored last time, otherwise the system default.
    std::unique_ptr<juce::XmlElement> audioState(settings_->getXmlValue("audioDevice"));
    const juce::String err = host_.start(audioState.get());
    if (err.isNotEmpty()) statusText_ = err;

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

    setWantsKeyboardFocus(true);
    setSize(1320, 820);
    startTimerHz(30);
}

MainComponent::~MainComponent() {
    stopTimer();
    saveNow();
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
        stage_.setScene(scene_);
        refreshSidebar();
    };
    c.commit = [this] { commit(); };
    c.rebuild = [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) { sp->rebuildInspector(); sp->refreshSidebar(); } }); };
    return c;
}

void MainComponent::applyScene(bool structural) {
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
    stage_.setScene(scene_);
    stage_.setSelectedSource(selSource_);
    stage_.setSelectedSpeaker(selSpeaker_);
    stage_.setEditTarget(step_ == StepSpeakers ? rkui::StageView::EditTarget::Speakers : rkui::StageView::EditTarget::Sources);
    updateCoach();
    undoBtn_.setEnabled(!undo_.empty());
    redoBtn_.setEnabled(!redo_.empty());
    addBtn_->setVisible(step_ == StepSources || step_ == StepRoom);
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
    return in + u8(" \xc2\xb7 ") + rkui::num(p.azimuthDeg, 0) + degree() + u8(" \xc2\xb7 ") + rkui::num(p.distance, 1) + " m";
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
            r.subtitle = tr("Ausgang") + " " + juce::String(m + 1) + u8(" \xc2\xb7 ") + rkui::num(p.azimuthDeg, 0) + degree() + u8(" \xc2\xb7 ") + rkui::num(p.distance, 1) + " m";
            r.trailing = sp.isSubwoofer ? "Sub" : (sp.gainDb != 0.0f ? rkui::num(sp.gainDb, 1) + " dB" : juce::String());
            r.dim = !sp.enabled || (!scene_.headphones && m >= outs);
            r.check = !r.dim && r.trailing.isEmpty();
            if (!scene_.headphones && m >= outs) r.subtitle = tr("Kein Ausgang") + u8(" \xc2\xb7 ") + rkui::num(p.azimuthDeg, 0) + degree();
            rows.push_back(r);
        }
        sidebar_.setRows(rows);
        sidebar_.setSelected(selSpeaker_);
    } else {
        for (int i = 0; i < (int)scene_.sources.size(); ++i) {
            const auto& s = scene_.sources[(size_t)i];
            rkui::SidebarList::Row r;
            r.number = i + 1;
            r.colour = rkui::itemColour(i);
            r.title = s.name;
            r.subtitle = sourceSubtitle(i);
            r.trailing = s.solo ? "Solo" : (s.mute ? tr("Stumm") : (s.orbit ? u8("\xe2\x9f\xb3") : juce::String()));
            r.dim = s.mute;
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

void MainComponent::selectSpeaker(int i) {
    selSpeaker_ = i;
    stage_.setSelectedSpeaker(i);
    stage_.setScene(scene_);
    refreshSidebar();
    rebuildInspector();
}

void MainComponent::setStep(int step) {
    step_ = juce::jlimit(0, 3, step);
    if (step_ != StepOutput && speakerTestRunning_) {
        speakerTestRunning_ = false;
        host_.engine.speakerTest = -1;
    }
    refreshAll();
}

void MainComponent::rebuildInspector() {
    switch (step_) {
    case StepSpeakers:
        rkui::buildLayoutPage(inspector_, pageContext(), selSpeaker_, [this](int i) {
            juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this), i] { if (sp) sp->selectSpeaker(i); });
        });
        break;
    case StepRoom: rkui::buildRoomPage(inspector_, pageContext()); break;
    case StepOutput: buildOutputPage(); break;
    default: buildSourcesPage(); break;
    }
}

void MainComponent::buildSourcesPage() {
    inspector_.clear();
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
        stage_.setScene(scene_);
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

    auto addParam = [this, i](const juce::String& label, double lo, double hi, double step, const juce::String& unit, int dec,
                              double origin, double value, std::function<void(rkui::SourceInfo&, double)> set) {
        auto* p = inspector_.add(std::make_unique<rkui::ParamSlider>(label, lo, hi, step, unit, dec, origin), 32);
        p->setValue(value);
        p->onChange = [this, i, set](double v) {
            if (i >= (int)scene_.sources.size()) return;
            set(scene_.sources[(size_t)i], v);
            applyScene(false);
            stage_.setScene(scene_);
        };
        p->onCommit = [this] { commit(); };
        return p;
    };
    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Klang")), 26);
    addParam(tr("Pegel"), -60, 12, 0.1, "dB", 1, 0, s.gainDb, [](rkui::SourceInfo& x, double v) { x.gainDb = (float)v; });
    addParam(tr("Breite"), 0, 100, 1, "%", 0, 0, s.spread * 100.0, [](rkui::SourceInfo& x, double v) { x.spread = (float)v / 100.0f; })
        ->setTooltip(tr("0 % klingt punktförmig, 100 % verteilt die Quelle auf mehrere Lautsprecher."));
    auto* send = addParam(tr("Hallanteil"), -80, 6, 0.5, "dB", 1, -80, s.reverbSendDb, [](rkui::SourceInfo& x, double v) { x.reverbSendDb = (float)v; });
    send->formatter = [](double v) { return v <= -79.9 ? u8("\xe2\x88\x92\xe2\x88\x9e dB") : rkui::num(v, 1) + " dB"; };
    send->setValue(s.reverbSendDb);
    addParam(tr("Entfernung"), 0, 200, 1, "%", 0, 0, s.distanceAmount * 100.0, [](rkui::SourceInfo& x, double v) { x.distanceAmount = (float)v / 100.0f; })
        ->setTooltip(tr("Wie stark Abstand hörbar wird: leiser, dumpfer und später. 0 % schaltet das aus."));

    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Bewegung")), 26);
    auto* motion = inspector_.add(std::make_unique<rkui::Segmented>(juce::StringArray{tr("Aus"), tr("Kreisen")}, false), 32);
    motion->setSelected(s.orbit ? 1 : 0);
    motion->onChange = [this, i](int m) {
        if (i >= (int)scene_.sources.size()) return;
        scene_.sources[(size_t)i].orbit = m == 1;
        commit();
        refreshSidebar();
    };
    auto* speed = addParam(tr("Tempo"), -30, 30, 0.5, "U/min", 1, 0, s.orbitSpeed * 60.0, [](rkui::SourceInfo& x, double v) { x.orbitSpeed = (float)v / 60.0f; });
    speed->setTooltip(tr("Umdrehungen pro Minute, negativ = im Uhrzeigersinn"));

    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Wiedergabe")), 26);
    auto* mute = inspector_.add(std::make_unique<juce::ToggleButton>(tr("Stumm")), 28);
    mute->setToggleState(s.mute, juce::dontSendNotification);
    mute->onClick = [this, i, mute] {
        if (i >= (int)scene_.sources.size()) return;
        scene_.sources[(size_t)i].mute = mute->getToggleState();
        applyScene(false);
        commit();
        refreshSidebar();
        stage_.setScene(scene_);
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
    inspector_.addDetails(tr("Pegel, Breite und Hallanteil lassen sich mit einem Doppelklick auf den Regler zurücksetzen, ein Doppelklick auf den Wert erlaubt die Eingabe von Zahlen.\n\n"
                             "„Kreisen“ bewegt die Quelle von selbst um den Hörplatz, gut zum Prüfen eines Aufbaus.\n\n"
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
        for (int b : sizes) sizeNames.add(juce::String(b) + " " + tr("Samples") + u8(" \xc2\xb7 ") + rkui::num(1000.0 * b / sr, 1) + " ms");
        auto* buf = inspector_.add(std::make_unique<rkui::ChoiceRow>(tr("Puffer"), sizeNames), 26);
        buf->box().setSelectedItemIndex(juce::jmax(0, sizes.indexOf(dev->getCurrentBufferSizeSamples())), juce::dontSendNotification);
        buf->setTooltip(tr("Kleiner bedeutet weniger Verzögerung, größer ist sicherer gegen Aussetzer."));
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
    const juce::String dot = u8(" \xc2\xb7 ");
    auto card = std::make_unique<rkui::StatusCard>();
    if (deviceError_.isNotEmpty()) card->set(rkui::StatusCard::Kind::Warn, tr("Gerät lässt sich nicht öffnen"), deviceError_);
    else if (outs == 0) card->set(rkui::StatusCard::Kind::Warn, tr("Kein Audiogerät"), tr("Oben ein Gerät wählen."));
    else if (outs < need) card->set(rkui::StatusCard::Kind::Warn, tr("Zu wenige Ausgänge"),
                                    juce::String(outs) + " " + tr("von") + " " + juce::String(need) + dot + tr("die übrigen Lautsprecher bleiben stumm"));
    else card->set(rkui::StatusCard::Kind::Ok, tr("Sitzt gut"),
                   juce::String(need) + " " + tr("von") + " " + juce::String(outs) + " " + tr("Ausgängen") + dot +
                       rkui::num(host_.sampleRate() / 1000.0, 1) + " kHz" + dot + "CPU " + juce::String(juce::roundToInt(host_.devices.getCpuUsage() * 100.0)) + " %");
    card->setProgress(juce::jmin(outs, need), need);
    const int cardH = card->idealHeight(juce::jmax(200, inspector_.getWidth() - 24));
    inspector_.add(std::move(card), cardH);

    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Ausgabe")), 26);
    auto* master = inspector_.add(std::make_unique<rkui::ParamSlider>(tr("Gesamtpegel"), -60, 12, 0.1, "dB", 1, 0), 32);
    master->setValue(scene_.masterGainDb);
    master->onChange = [this](double v) { scene_.masterGainDb = (float)v; applyScene(false); };
    master->onCommit = [this] { commit(); };
    auto* hp = inspector_.add(std::make_unique<juce::ToggleButton>(tr("Kopfhörer-Vorschau")), 28);
    hp->setToggleState(scene_.headphones, juce::dontSendNotification);
    hp->setTooltip(tr("Rechnet alle Lautsprecher auf zwei Kopfhörer-Kanäle herunter. Zum Vorbereiten ohne Lautsprecher."));
    hp->onClick = [this, hp] {
        scene_.headphones = hp->getToggleState();
        applyScene(true);
        commit();
        juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) sp->refreshAll(); });
    };
    auto* lim = inspector_.add(std::make_unique<juce::ToggleButton>(tr("Schutzbegrenzer")), 28);
    lim->setToggleState(scene_.limiter, juce::dontSendNotification);
    lim->setTooltip(tr("Verhindert Übersteuerung und schützt die Lautsprecher. Nur für Messungen ausschalten."));
    lim->onClick = [this, lim] { scene_.limiter = lim->getToggleState(); applyScene(false); commit(); };
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
        } else if (r == 6) {
            juce::URL("https://github.com/BenjaminJMueller/immersive-audio-suite").launchInDefaultBrowser();
        }
    });
}

// ---------------------------------------------------------------------------

void MainComponent::timerCallback() {
    const double now = juce::Time::getMillisecondCounterHiRes();

    // Moving sources ("Kreisen").
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
    if (moved) {
        stage_.setScene(scene_);
        if (step_ == StepSources || step_ == StepRoom) refreshSidebar();
    }

    // Meters.
    static std::vector<float> spk(rk::kMaxSpeakers, 0.0f), src(rk::kMaxSources, 0.0f);
    const int nOut = scene_.headphones ? 2 : (int)scene_.layout.speakers.size();
    for (int c = 0; c < rk::kMaxSpeakers; ++c) spk[(size_t)c] = std::max(levelToUnit(host_.engine.takeOutputPeak(c)), spk[(size_t)c] * 0.85f);
    for (int s = 0; s < rkui::kMaxSceneSources; ++s) {
        src[(size_t)s] = std::max(levelToUnit(host_.engine.takeSourcePeak(s)), src[(size_t)s] * 0.85f);
        if (src[(size_t)s] > 0.05f) signalSeen_[(size_t)s] = now;
        sourceHasSignal_[(size_t)s] = now - signalSeen_[(size_t)s] < 2000.0;
    }
    std::vector<float> spkView(spk.begin(), spk.begin() + juce::jmax(0, scene_.headphones ? 0 : nOut));
    stage_.setSpeakerLevels(spkView);
    stage_.setSourceLevels(std::vector<float>(src.begin(), src.begin() + (long)scene_.sources.size()));
    if (step_ == StepSpeakers || step_ == StepOutput) sidebar_.setLevels(spkView);
    else sidebar_.setLevels(std::vector<float>(src.begin(), src.begin() + (long)scene_.sources.size()));

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
    g.drawText(tr("Automatisch gesichert"), 16, 26, 220, 16, juce::Justification::left);
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
    if (addBtn_->isVisible()) addBtn_->setBounds(side.removeFromBottom(44).reduced(12, 6));
    sidebarView_.setBounds(side.reduced(0, 4));
    sidebar_.setSize(sidebarView_.getMaximumVisibleWidth(), sidebar_.contentHeight());
    inspector_.setBounds(r.removeFromRight(kInspector).reduced(0, 8));
    stage_.setBounds(r);
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
    case StepRoom: stage_.setCoach("3", tr("Raum wählen"), tr("Rechts eine Vorlage wählen. Den Anteil je Quelle regelt der Hallanteil.")); break;
    default:
        if (outs == 0) stage_.setCoach("4", tr("Audiogerät wählen"), tr("Rechts das Audio-Interface auswählen."));
        else if (!scene_.headphones && outs < nSpk)
            stage_.setCoach("!", tr("Zu wenige Ausgänge"), juce::String(outs) + " " + tr("von") + " " + juce::String(nSpk) + ". " + tr("Anderes Gerät wählen oder Kopfhörer-Vorschau einschalten."));
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

} // namespace rkapp
