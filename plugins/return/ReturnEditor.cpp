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
    auto area = getLocalBounds().toFloat().reduced(24.0f, 20.0f);
    g.setColour(p.text3);
    g.setFont(rkui::uiFont(12.0f));
    g.drawText(L("Lautsprecher aus der App", "Speakers from the app"), area.removeFromTop(18.0f).toNearestInt(), juce::Justification::centredLeft);
    area.removeFromTop(10.0f);
    if (channels_ <= 0) return;

    // Up to 16 meters per row; more channels wrap into further rows.
    const int perRow = std::min(channels_, 16);
    const int rows = (channels_ + perRow - 1) / perRow;
    const float rowH = std::min(320.0f, area.getHeight() / (float)rows);
    const float gap = 6.0f;
    const float w = std::min(40.0f, (area.getWidth() - gap * (float)(perRow - 1)) / (float)perRow);
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
    addAndMakeVisible(meters_);
    addAndMakeVisible(page_);

    card_ = page_.add(std::make_unique<rkui::StatusCard>(), 72);
    page_.add(std::make_unique<rkui::SectionHeader>(
                  tr("Lautsprecher"),
                  L("Die Spur bekommt so viele Lautsprecher, wie sie Kan\xc3\xa4le hat, ab dem ersten Lautsprecher. "
                    "Mehr Lautsprecher als eine Spur fasst? Weitere Spuren mit diesem Plugin anlegen und den ersten Lautsprecher weiterz\xc3\xa4hlen.",
                    "The track receives as many speakers as it has channels, starting at the first speaker. "
                    "More speakers than one track holds? Add further tracks with this plugin and count the first speaker on.")),
              26);
    auto* prm = proc_.params.getParameter("first");
    const auto range = prm->getNormalisableRange();
    auto* s = page_.add(std::make_unique<rkui::ParamSlider>(L("Ab Nummer", "From number"), range.start, range.end, 1.0,
                                                            juce::String(), 0, range.start),
                        32);
    s->setTooltip(L("Welcher Lautsprecher der App auf Kanal 1 dieser Spur kommt", "Which app speaker arrives on channel 1 of this track"));
    bindings_.push_back(std::make_unique<ParamBinding>(*s, *prm));

    page_.addDetails(L("Bringt das, was die Raumklang-App an die Lautsprecher schickt, zur\xc3\xbc" "ck in die DAW. "
                       "Lege das Plugin auf eine Spur mit so vielen Kan\xc3\xa4len wie Lautsprecher (in Logic etwa ein Aux in 7.1.4 f\xc3\xbcr 12 Lautsprecher) "
                       "und nimm sie auf oder bounce in Echtzeit.\n\n"
                       "Offline-Bounce geht nicht, weil die App in Echtzeit rechnet. In Logic: Bounce mit \xe2\x80\x9e" "Echtzeit\xe2\x80\x9c.\n\n"
                       "Das Plugin ersetzt den Eingang der Spur. DAW und App m\xc3\xbcssen mit derselben Abtastrate laufen.",
                       "Brings what the Raumklang app sends to the speakers back into the DAW. "
                       "Put the plugin on a track with as many channels as speakers (in Logic for example an aux in 7.1.4 for 12 speakers) "
                       "and record it or bounce in real time.\n\n"
                       "Offline bounces do not work because the app renders in real time. In Logic: bounce in \"Realtime\" mode.\n\n"
                       "The plugin replaces the track's input. The DAW and the app must run at the same sample rate."));

    setResizable(true, true);
    setResizeLimits(640, 420, 1800, 1200);
    setSize(860, 520);
    refreshStatus();
    startTimerHz(30);
}

ReturnEditor::~ReturnEditor() {
    stopTimer();
    bindings_.clear();
    setLookAndFeel(nullptr);
}

void ReturnEditor::refreshStatus() {
    const auto st = proc_.status();
    if (st.dropouts > 0) {
        recentDropouts_ += st.dropouts;
        dropoutHold_ = 30 * 4;
    } else if (dropoutHold_ > 0 && --dropoutHold_ == 0) {
        recentDropouts_ = 0;
    }
    using K = rkui::StatusCard::Kind;
    K kind = K::Info;
    juce::String title, detail;
    const int last = st.firstChannel + st.trackChannels - 1;
    const juce::String range = juce::String(st.firstChannel) + (st.trackChannels > 1 ? juce::String::fromUTF8("\xe2\x80\x93") + juce::String(last) : "");

    if (!st.available) {
        kind = K::Warn;
        title = L("Keine Verbindung m\xc3\xb6glich", "Cannot connect");
        detail = L("Dieser Host erlaubt keine Verbindung zur App.", "This host does not allow a connection to the app.");
    } else if (!st.appRunning) {
        kind = K::Info;
        title = L("App nicht gefunden", "App not found");
        detail = L("Starte die Raumklang-App. Bis dahin bleibt die Spur still.", "Start the Raumklang app. Until then the track stays silent.");
    } else if (st.rateMismatch) {
        kind = K::Warn;
        title = L("Abtastrate passt nicht", "Sample rate differs");
        detail = L("Die App l\xc3\xa4uft mit ", "The app runs at ") + rateText(st.appSampleRate) + L(", die DAW mit ", ", the DAW at ") +
                 rateText(st.sampleRate) + L(". Stelle beide gleich ein.", ". Set both to the same rate.");
    } else if (st.offline) {
        kind = K::Warn;
        title = L("Nur Echtzeit-Bounce", "Real-time bounce only");
        detail = L("Die App rechnet in Echtzeit. Bounce mit \xe2\x80\x9e" "Echtzeit\xe2\x80\x9c, sonst fehlt Audio.",
                   "The app renders in real time. Bounce in \"Realtime\" mode, otherwise audio is missing.");
    } else if (st.firstChannel > st.appChannels) {
        kind = K::Warn;
        title = L("Kein Lautsprecher in diesem Bereich", "No speaker in this range");
        detail = L("Die App hat ", "The app has ") + juce::String(st.appChannels) + " " + tr("Lautsprecher") +
                 L(". W\xc3\xa4hle einen kleineren ersten Lautsprecher.", ". Choose a lower first speaker.");
    } else if (recentDropouts_ > 0) {
        kind = K::Warn;
        title = L("Aussetzer", "Dropouts");
        detail = L("Die DAW hat kurz nicht mitgehalten. Ein gr\xc3\xb6\xc3\x9f" "erer Puffer in der DAW hilft.",
                   "The DAW briefly could not keep up. A larger buffer in the DAW helps.");
    } else {
        kind = st.receiving ? K::Ok : K::Info;
        title = st.receiving ? L("Empf\xc3\xa4ngt von Raumklang", "Receiving from Raumklang") : L("Verbunden mit Raumklang", "Connected to Raumklang");
        detail = tr("Lautsprecher") + " " + range + L(" von ", " of ") + juce::String(st.appChannels) + ".";
        if (st.appChannels > last)
            detail << L(" F\xc3\xbcr die \xc3\xbc" "brigen eine weitere Spur ab Lautsprecher ", " For the rest add another track from speaker ") +
                          juce::String(last + 1) + ".";
        else if (st.firstChannel == 1 && st.trackChannels > st.appChannels)
            detail << L(" Die letzten Kan\xc3\xa4le bleiben still.", " The last channels stay silent.");
    }
    const int filled = st.appRunning ? juce::jlimit(0, st.trackChannels, st.appChannels - st.firstChannel + 1) : 0;
    const juce::String key = juce::String((int)kind) + title + detail + juce::String(filled);
    if (key != cardKey_) {
        cardKey_ = key;
        card_->set(kind, title, detail);
        card_->setProgress(filled, st.trackChannels);
        page_.setRowHeight(card_, card_->idealHeight(juce::jmax(200, page_.getWidth() - 24)));
    }

    levels_.resize((size_t)st.trackChannels);
    for (size_t c = 0; c < levels_.size(); ++c) {
        const float db = rk::gainToDb(proc_.takePeak((int)c));
        levels_[c] = std::max(juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f), levels_[c] * 0.85f);
    }
    meters_.set(st.firstChannel, st.trackChannels, st.appRunning ? st.appChannels : 0, levels_);
}

void ReturnEditor::timerCallback() {
    for (auto& b : bindings_) b->refresh();
    refreshStatus();
}

void ReturnEditor::paint(juce::Graphics& g) {
    const auto& p = rkui::palette();
    g.fillAll(p.side);
    g.setColour(p.bar);
    g.fillRect(0, 0, getWidth(), 52);
    g.setColour(p.line);
    g.fillRect(0, 51, getWidth(), 1);
    g.setColour(p.text);
    g.setFont(rkui::uiFont(15.0f, true));
    g.drawText(juce::String("Raumklang Return"), 16, 0, 260, 52, juce::Justification::centredLeft);
}

void ReturnEditor::resized() {
    auto r = getLocalBounds();
    r.removeFromTop(52);
    page_.setBounds(r.removeFromRight(juce::jmax(330, getWidth() / 3)).reduced(0, 8));
    meters_.setBounds(r);
    cardKey_.clear();
}

} // namespace rkplug
