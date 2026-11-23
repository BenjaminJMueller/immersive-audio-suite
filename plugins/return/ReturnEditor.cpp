#include "ReturnEditor.h"

using rkui::tr;

namespace rkplug {
namespace {

juce::String L(const char* de, const char* en) {
    return juce::String::fromUTF8(rkui::language() == rkui::Language::German ? de : en);
}

juce::String rateText(double hz) {
    return juce::String(hz / 1000.0, std::abs(hz - 44100.0) < 1.0 || std::abs(hz - 88200.0) < 1.0 ? 1 : 0) + " kHz";
}

} // namespace

juce::AudioProcessorEditor* ReturnProcessor::createEditor() { return new ReturnEditor(*this); }

void ReturnMeters::set(int first, int channels, int appChannels, const std::vector<float>& levels) {
    first_ = first;
    channels_ = channels;
    appChannels_ = appChannels;
    levels_ = levels;
    repaint();
}

void ReturnMeters::paint(juce::Graphics& g) {
    const auto& p = rkui::palette();
    g.fillAll(p.stage);
    g.setColour(p.line);
    g.fillRect(0, 0, getWidth(), 1);
    auto area = getLocalBounds().toFloat().reduced(16.0f, 12.0f);
    if (channels_ <= 0) return;

    // Up to 16 meters per row; more channels wrap into further rows.
    const int perRow = std::min(channels_, 16);
    const int rows = (channels_ + perRow - 1) / perRow;
    const float rowH = std::min(320.0f, area.getHeight() / (float)rows);
    const float gap = 6.0f;
    const float w = std::min(64.0f, (area.getWidth() - gap * (float)(perRow - 1)) / (float)perRow);
    for (int i = 0; i < channels_; ++i) {
        const int r = i / perRow, c = i % perRow;
        auto cell = juce::Rectangle<float>(area.getX() + (float)c * (w + gap), area.getY() + (float)r * rowH, w, rowH - 8.0f);
        auto label = cell.removeFromBottom(20.0f);
        const int speaker = first_ + i;
        const bool exists = speaker <= appChannels_;
        g.setColour(p.group);
        g.fillRoundedRectangle(cell, 4.0f);
        const float lv = i < (int)levels_.size() ? juce::jlimit(0.0f, 1.0f, levels_[(size_t)i]) : 0.0f;
        if (exists && lv > 0.0f) {
            // 20 segments, colour by level, like the meters under the speakers in the app.
            const int segs = 20;
            const float sh = (cell.getHeight() - 4.0f) / (float)segs;
            const int lit = juce::roundToInt(lv * (float)segs);
            for (int s = 0; s < lit; ++s) {
                const float f = (float)s / (float)(segs - 1);
                auto col = f > 0.9f ? p.bad : f > 0.7f ? p.warn : p.ok;
                g.setColour(col.withAlpha(0.55f + 0.45f * f));
                g.fillRoundedRectangle(cell.getX() + 3.0f, cell.getBottom() - 2.0f - (float)(s + 1) * sh + 1.0f, cell.getWidth() - 6.0f,
                                       sh - 2.0f, 1.5f);
            }
        }
        g.setColour(exists ? p.text2 : p.text3.withAlpha(0.5f));
        g.setFont(rkui::uiFont(11.0f, exists));
        g.drawText(juce::String(speaker), label.toNearestInt(), juce::Justification::centred);
    }
}

ReturnEditor::ReturnEditor(ReturnProcessor& p) : AudioProcessorEditor(p), proc_(p) {
    rkui::setDarkMode(true);
    lnf_.refreshColours();
    setLookAndFeel(&lnf_);
    room_.setMode(static_cast<rkui::RoomView::Mode>(
        juce::jlimit(0, 3, (int)proc_.params.state.getProperty("view", (int)rkui::RoomView::Mode::Top))));
    room_.onModeChanged = [this](rkui::RoomView::Mode m) { proc_.params.state.setProperty("view", (int)m, nullptr); };
    room_.setSelectedSource(-1);
    room_.setFirstLockedSource(0); // only shown here, moved in the Quelle plugins
    addAndMakeVisible(room_);
    addAndMakeVisible(meters_);
    addAndMakeVisible(page_);
    help_.setTooltip(L("Bringt das, was die Raumklang-App an die Lautsprecher schickt, zur\xc3\xbc" "ck in die DAW. "
                       "Spur mit so vielen Kan\xc3\xa4len wie Lautsprecher (in Logic etwa ein Aux in 7.1.4 f\xc3\xbcr 12 Lautsprecher), "
                       "dann aufnehmen oder in Echtzeit bouncen.",
                       "Brings what the Raumklang app sends to the speakers back into the DAW. "
                       "Use a track with as many channels as speakers (in Logic for example an aux in 7.1.4 for 12 speakers), "
                       "then record it or bounce in real time."));
    addAndMakeVisible(help_);

    page_.add(std::make_unique<rkui::SectionHeader>(
                  L("Eingang", "Input"),
                  L("Die Spur bekommt so viele Lautsprecher, wie sie Kan\xc3\xa4le hat, ab diesem Lautsprecher. "
                    "Mehr Lautsprecher als eine Spur fasst? Eine weitere Spur mit diesem Plugin anlegen und weiterz\xc3\xa4hlen.",
                    "The track receives as many speakers as it has channels, starting at this speaker. "
                    "More speakers than one track holds? Add another track with this plugin and count on.")),
              26);
    auto* prm = proc_.params.getParameter("first");
    const auto range = prm->getNormalisableRange();
    auto* s = page_.add(std::make_unique<rkui::ParamSlider>(L("Ab Lautsprecher", "From speaker"), range.start, range.end, 1.0,
                                                            juce::String(), 0, range.start),
                        34);
    s->setTooltip(L("Welcher Lautsprecher der App auf Kanal 1 dieser Spur kommt", "Which app speaker arrives on channel 1 of this track"));
    bindings_.push_back(std::make_unique<ParamBinding>(*s, *prm));

    tracks_.open();
    setResizable(true, true);
    setResizeLimits(760, 520, 2400, 1600);
    setSize(1100, 680);
    refreshRoom();
    refreshStatus();
    startTimerHz(30);
}

ReturnEditor::~ReturnEditor() {
    stopTimer();
    bindings_.clear();
    setLookAndFeel(nullptr);
}

void ReturnEditor::refreshRoom() {
    // The room from the shared setup, the tracks from the bridge: what the app is playing.
    rkui::readSharedSetup(scene_);
    scene_.sources.clear();
    for (const auto& t : tracks_.scan()) {
        rkui::SourceInfo s;
        s.name = juce::String::fromUTF8(t.name.c_str());
        s.position = rk::toCartesian({t.params.azimuthDeg, t.params.elevationDeg, t.params.distance});
        s.spread = t.params.spread;
        s.mute = !t.sending;
        scene_.sources.push_back(s);
    }
    room_.setSourceStyle({}, 1 << 30, true);
    room_.setScene(scene_);
    std::vector<float> lv(scene_.sources.size(), 0.0f);
    for (size_t i = 0; i < lv.size(); ++i) lv[i] = scene_.sources[i].mute ? 0.0f : 0.5f;
    room_.setSourceLevels(lv);
}

void ReturnEditor::refreshStatus() {
    const auto st = proc_.status();
    if (st.dropouts > 0) {
        recentDropouts_ += st.dropouts;
        dropoutHold_ = 30 * 4;
    } else if (dropoutHold_ > 0 && --dropoutHold_ == 0) {
        recentDropouts_ = 0;
    }
    // Only what needs attention; when audio arrives, the bar stays quiet.
    bool warn = true;
    juce::String text;
    const int last = st.firstChannel + st.trackChannels - 1;
    if (!st.available) {
        text = L("Dieser Host erlaubt keine Verbindung zur App.", "This host does not allow a connection to the app.");
    } else if (!st.appRunning) {
        warn = false;
        text = L("Raumklang-App nicht ge\xc3\xb6" "ffnet. Die Spur bleibt still.", "Raumklang app not open. The track stays silent.");
    } else if (st.rateMismatch) {
        text = L("Abtastrate passt nicht: App ", "Sample rate differs: app ") + rateText(st.appSampleRate) + L(", DAW ", ", DAW ") +
               rateText(st.sampleRate) + L(". Beide gleich einstellen.", ". Set both to the same rate.");
    } else if (!st.processing) {
        // The room above still shows the tracks (they come from the app), but no audio passes:
        // the DAW does not run this track right now.
        text = L("Die DAW verarbeitet diese Spur gerade nicht. Spur ausw\xc3\xa4hlen, Input-Monitoring einschalten oder die Wiedergabe starten.",
                 "The DAW is not processing this track right now. Select the track, turn on input monitoring or start playback.");
    } else if (st.offline) {
        text = L("Nur Echtzeit-Bounce: Die App rechnet in Echtzeit.", "Real-time bounce only: the app renders in real time.");
    } else if (st.firstChannel > st.appChannels) {
        text = L("Die App hat nur ", "The app has only ") + juce::String(st.appChannels) + " " + tr("Lautsprecher") +
               L(". Kleineren ersten Lautsprecher w\xc3\xa4hlen.", ". Choose a lower first speaker.");
    } else if (recentDropouts_ > 0) {
        text = L("Aussetzer: Ein gr\xc3\xb6\xc3\x9f" "erer Puffer in der DAW hilft.", "Dropouts: a larger buffer in the DAW helps.");
    } else if (st.trackChannels < st.appChannels - st.firstChannel + 1) {
        warn = st.firstChannel == 1;
        text = L("Spur hat ", "Track has ") + juce::String(st.trackChannels) + L(" Kan\xc3\xa4le, die App ", " channels, the app ") +
               juce::String(st.appChannels) + " " + tr("Lautsprecher") +
               L(". Spur auf mehr Kan\xc3\xa4le stellen oder eine weitere Spur ab ", ". Use more channels or another track from speaker ") +
               juce::String(last + 1) + L(" anlegen.", ".");
    } else {
        text.clear();
    }
    const juce::String range = tr("Lautsprecher") + " " + juce::String(st.firstChannel) +
                               (st.trackChannels > 1 ? juce::String::fromUTF8("\xe2\x80\x93") + juce::String(last) : juce::String());
    if (text != notice_ || warn != noticeWarn_ || range != rangeText_) {
        notice_ = text;
        noticeWarn_ = warn;
        rangeText_ = range;
        repaint(0, 0, getWidth(), 46);
    }

    levels_.resize((size_t)st.trackChannels);
    for (size_t c = 0; c < levels_.size(); ++c) {
        const float db = rk::gainToDb(proc_.takePeak((int)c));
        levels_[c] = std::max(juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f), levels_[c] * 0.85f);
    }
    meters_.set(st.firstChannel, st.trackChannels, st.appRunning ? st.appChannels : 0, levels_);
    // The same levels on the speakers in the room.
    std::vector<float> spk(scene_.layout.speakers.size(), 0.0f);
    for (size_t c = 0; c < levels_.size(); ++c) {
        const size_t m = (size_t)(st.firstChannel - 1) + c;
        if (m < spk.size()) spk[m] = levels_[c];
    }
    room_.setSpeakerLevels(spk);
}

void ReturnEditor::timerCallback() {
    for (auto& b : bindings_) b->refresh();
    if (--roomCountdown_ <= 0) {
        roomCountdown_ = 15;
        refreshRoom();
    }
    refreshStatus();
}

void ReturnEditor::paint(juce::Graphics& g) {
    const auto& p = rkui::palette();
    g.fillAll(p.side);
    g.setColour(p.bar);
    g.fillRect(0, 0, getWidth(), 46);
    g.setColour(p.line);
    g.fillRect(0, 45, getWidth(), 1);
    g.setColour(p.text);
    g.setFont(rkui::uiFont(15.0f, true));
    g.drawText(juce::String("Raumklang Return"), 16, 0, 170, 46, juce::Justification::centredLeft);
    g.setColour(p.text3);
    g.setFont(rkui::uiFont(12.5f));
    g.drawText(rangeText_, 180, 0, 200, 46, juce::Justification::centredLeft);
    if (notice_.isNotEmpty()) rkui::drawNoticePill(g, 380.0f, (float)help_.getX() - 10.0f, 23.0f, noticeWarn_, notice_);
}

void ReturnEditor::resized() {
    auto r = getLocalBounds();
    auto bar = r.removeFromTop(46);
    help_.setBounds(bar.removeFromRight(46).withSizeKeepingCentre(30, 28));
    page_.setBounds(r.removeFromRight(juce::jlimit(280, 340, getWidth() / 3)).reduced(0, 8));
    meters_.setBounds(r.removeFromBottom(juce::jlimit(110, 170, getHeight() / 5)));
    room_.setBounds(r);
}

} // namespace rkplug
