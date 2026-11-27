#include "Updater.h"

#include <juce_cryptography/juce_cryptography.h>
#include <rk_ui/Scene.h>
#include <rk_ui/Strings.h>
#include <rk_ui/Theme.h>
#include <rk_ui/Widgets.h>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifndef RK_BUILD_NUMBER
#define RK_BUILD_NUMBER 0
#endif

using rkui::tr;
using rkui::u8;

namespace rkapp {
namespace {

const char* kFeed = "https://github.com/BenjaminJMueller/immersive-audio-suite/releases/latest/download/latest.json";
const char* kPage = "https://github.com/BenjaminJMueller/immersive-audio-suite/releases/latest";

juce::String platformKey() {
#if JUCE_MAC
    return "mac";
#elif JUCE_WINDOWS
    return "win";
#else
    return {};
#endif
}

// RAUMKLANG_UPDATE_DEMO=1 shows the update sheet with a made-up release (for screenshots and checks).
bool demoMode() { return juce::SystemStats::getEnvironmentVariable("RAUMKLANG_UPDATE_DEMO", {}).isNotEmpty(); }

juce::File logFile() { return rkui::appDataFolder().getChildFile("Update.log"); }
juce::File statusFile() { return rkui::appDataFolder().getChildFile("Update-Status.txt"); }

void log(const juce::String& line) {
    logFile().appendText(juce::Time::getCurrentTime().toString(true, true, true, true) + "  " + line + "\n");
}

juce::String megabytes(juce::int64 bytes) {
    return juce::String((double)bytes / (1024.0 * 1024.0), 1).replaceCharacter('.', rkui::language() == rkui::Language::German ? ',' : '.') + " MB";
}

} // namespace

// ---------------------------------------------------------------------------
// The sheet over the window: question, progress, result or error, in the app's own style.

class UpdateSheet : public juce::Component, private juce::Timer {
public:
    enum class Kind { Download, Progress, Done, Error };

    UpdateSheet() {
        primary_ = rkui::makeButton({}, true);
        secondary_ = rkui::makeButton({}, false, true);
        primary_->onClick = [this] { if (onPrimary) onPrimary(); };
        secondary_->onClick = [this] { if (onSecondary) onSecondary(); };
        addChildComponent(*primary_);
        addChildComponent(*secondary_);
        setWantsKeyboardFocus(true);
    }

    void set(Kind kind, const juce::String& title, const juce::String& text, const juce::StringArray& points = {}) {
        kind_ = kind;
        if (kind_ == Kind::Done || kind_ == Kind::Error) fromVersion_ = toVersion_ = {};
        title_ = title;
        text_ = text;
        points_ = points;
        if (kind_ == Kind::Progress) startTimerHz(15);
        else stopTimer();
        resized();
        repaint();
    }
    void setButtons(const juce::String& primary, std::function<void()> onPrimaryClick, const juce::String& secondary = {},
                    std::function<void()> onSecondaryClick = {}) {
        primary_->setButtonText(primary);
        secondary_->setButtonText(secondary);
        primary_->setVisible(primary.isNotEmpty());
        secondary_->setVisible(secondary.isNotEmpty());
        onPrimary = std::move(onPrimaryClick);
        onSecondary = std::move(onSecondaryClick);
        resized();
    }

    /// Shown as "installed -> new" chips under the title (empty: none).
    void setVersions(const juce::String& from, const juce::String& to) {
        fromVersion_ = from;
        toVersion_ = to;
        resized();
        repaint();
    }

    /// Progress 0..1 (negative: unknown) and the line under the bar, read while Kind::Progress is shown.
    std::function<double()> progress;
    std::function<juce::String()> progressText;

    void parentSizeChanged() override {
        if (auto* p = getParentComponent()) setBounds(p->getLocalBounds());
    }

    void paint(juce::Graphics& g) override {
        const auto& p = rkui::palette();
        g.fillAll(juce::Colours::black.withAlpha(0.5f));
        auto c = card().toFloat();
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRoundedRectangle(c.translated(0, 10).expanded(4), 16.0f);
        g.setColour(p.bar);
        g.fillRoundedRectangle(c, 14.0f);
        {
            // A soft glow of the state colour behind the icon, like the particles of the room view.
            const juce::Colour glow = kind_ == Kind::Error ? p.bad : kind_ == Kind::Done ? p.ok : p.accent;
            juce::Graphics::ScopedSaveState ss(g);
            juce::Path clip;
            clip.addRoundedRectangle(c, 14.0f);
            g.reduceClipRegion(clip);
            const juce::Point<float> o(c.getX() + kPad + 20.0f, c.getY() + kPad + 20.0f);
            g.setGradientFill(juce::ColourGradient(glow.withAlpha(0.22f), o, glow.withAlpha(0.0f), o.translated(260.0f, 90.0f), true));
            g.fillRect(c);
        }
        g.setColour(p.lineStrong);
        g.drawRoundedRectangle(c.reduced(0.25f), 14.0f, 0.75f);

        auto r = c.reduced(kPad, kPad);
        // Icon and title in one row.
        auto head = r.removeFromTop(40.0f);
        auto icon = head.removeFromLeft(40.0f);
        const juce::Colour ic = kind_ == Kind::Error ? p.bad : kind_ == Kind::Done ? p.ok : p.accent;
        g.setColour(ic.withAlpha(0.16f));
        g.fillEllipse(icon);
        g.setColour(ic);
        drawKindIcon(g, icon.reduced(11.0f));
        head.removeFromLeft(14.0f);
        g.setColour(p.text);
        g.setFont(rkui::uiFont(18.0f, true));
        g.drawText(title_, head, juce::Justification::centredLeft, true);
        r.removeFromTop(14.0f);
        if (toVersion_.isNotEmpty()) {
            // Installed version -> new version as two chips.
            auto row = r.removeFromTop(kChips);
            const auto chip = [&](const juce::String& label, const juce::String& v, bool isNew) {
                const juce::String t = label + " " + v;
                const float cw = juce::GlyphArrangement::getStringWidth(rkui::uiFont(12.5f, isNew), t) + 22.0f;
                auto b = row.removeFromLeft(cw).withSizeKeepingCentre(cw, 24.0f);
                g.setColour(isNew ? p.accent.withAlpha(0.18f) : p.group);
                g.fillRoundedRectangle(b, 12.0f);
                g.setColour(isNew ? p.accent : p.text2);
                g.setFont(rkui::uiFont(12.5f, isNew));
                g.drawText(t, b, juce::Justification::centred, false);
            };
            chip(tr("Installiert"), fromVersion_, false);
            auto arrow = row.removeFromLeft(30.0f);
            juce::Path a;
            const float cx = arrow.getCentreX(), cy = arrow.getCentreY();
            a.startNewSubPath(cx - 6.0f, cy);
            a.lineTo(cx + 5.0f, cy);
            a.startNewSubPath(cx + 1.0f, cy - 4.0f);
            a.lineTo(cx + 5.0f, cy);
            a.lineTo(cx + 1.0f, cy + 4.0f);
            g.setColour(p.text3);
            g.strokePath(a, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            chip(tr("Neu"), toVersion_, true);
            r.removeFromTop(14.0f);
        }

        const float w = r.getWidth();
        auto text = layout(text_, 14.0f, p.text2, w);
        text.draw(g, r.removeFromTop(text.getHeight()));
        if (!points_.isEmpty()) {
            r.removeFromTop(12.0f);
            for (const auto& pt : points_) {
                auto line = layout(pt, 13.5f, p.text, w - 18.0f);
                auto row = r.removeFromTop(line.getHeight() + 6.0f);
                g.setColour(p.accent);
                g.fillEllipse(row.getX() + 2.0f, row.getY() + 6.5f, 5.0f, 5.0f);
                line.draw(g, row.withTrimmedLeft(18.0f).withHeight(line.getHeight()));
            }
        }
        if (kind_ == Kind::Progress) {
            r.removeFromTop(16.0f);
            auto bar = r.removeFromTop(6.0f);
            g.setColour(p.track);
            g.fillRoundedRectangle(bar, 3.0f);
            const double v = progress ? progress() : -1.0;
            g.setColour(p.accent);
            if (v >= 0.0) {
                g.fillRoundedRectangle(bar.withWidth(juce::jmax(6.0f, bar.getWidth() * (float)juce::jlimit(0.0, 1.0, v))), 3.0f);
            } else { // unknown: a moving segment
                const float t = (float)std::fmod(juce::Time::getMillisecondCounterHiRes() / 1400.0, 1.0);
                const float sw = bar.getWidth() * 0.28f;
                g.fillRoundedRectangle(bar.withX(bar.getX() + (bar.getWidth() - sw) * t).withWidth(sw), 3.0f);
            }
            r.removeFromTop(8.0f);
            g.setColour(p.text3);
            g.setFont(rkui::uiFont(12.5f));
            g.drawText(progressText ? progressText() : juce::String(), r.removeFromTop(18.0f), juce::Justification::centredLeft, true);
        }
        // Footer line above the buttons.
        if (primary_->isVisible() || secondary_->isVisible()) {
            g.setColour(p.line);
            g.drawHorizontalLine(juce::roundToInt(c.getBottom() - kFooter), c.getX(), c.getRight());
        }
    }

    void resized() override {
        auto foot = card().removeFromBottom((int)kFooter).reduced((int)kPad, 13);
        if (primary_->isVisible()) {
            const int w = juce::roundToInt(juce::GlyphArrangement::getStringWidth(rkui::uiFont(13.5f), primary_->getButtonText())) + 36;
            primary_->setBounds(foot.removeFromRight(juce::jmax(110, w)));
            foot.removeFromRight(8);
        }
        if (secondary_->isVisible()) {
            const int w = juce::roundToInt(juce::GlyphArrangement::getStringWidth(rkui::uiFont(13.5f), secondary_->getButtonText())) + 32;
            secondary_->setBounds(foot.removeFromRight(juce::jmax(90, w)));
        }
    }

    bool keyPressed(const juce::KeyPress& k) override {
        if (k == juce::KeyPress::escapeKey && secondary_->isVisible()) { if (onSecondary) onSecondary(); return true; }
        if (k == juce::KeyPress::returnKey && primary_->isVisible()) { if (onPrimary) onPrimary(); return true; }
        return true;
    }
    void mouseDown(const juce::MouseEvent&) override {} // blocks clicks to the window below

private:
    static constexpr float kPad = 28.0f, kFooter = 64.0f, kWidth = 500.0f, kChips = 26.0f;

    static juce::TextLayout layout(const juce::String& s, float size, juce::Colour c, float width) {
        juce::AttributedString a;
        a.append(s, rkui::uiFont(size), c);
        a.setLineSpacing(2.0f);
        juce::TextLayout tl;
        tl.createLayout(a, width);
        return tl;
    }

    float contentHeight() const {
        const auto& p = rkui::palette();
        const float w = kWidth - 2 * kPad;
        float h = 40.0f + 14.0f + layout(text_, 14.0f, p.text2, w).getHeight();
        if (toVersion_.isNotEmpty()) h += kChips + 14.0f;
        if (!points_.isEmpty()) {
            h += 12.0f;
            for (const auto& pt : points_) h += layout(pt, 13.5f, p.text, w - 18.0f).getHeight() + 6.0f;
        }
        if (kind_ == Kind::Progress) h += 16.0f + 6.0f + 8.0f + 18.0f;
        return h;
    }

    juce::Rectangle<int> card() const {
        const bool buttons = primary_->isVisible() || secondary_->isVisible();
        const float h = 2 * kPad + contentHeight() + (buttons ? kFooter : 0.0f);
        return getLocalBounds().withSizeKeepingCentre((int)kWidth, juce::jmin(getHeight() - 40, juce::roundToInt(h)));
    }

    void drawKindIcon(juce::Graphics& g, juce::Rectangle<float> r) const {
        juce::Path path;
        const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
        if (kind_ == Kind::Done) {
            path.startNewSubPath(x + w * 0.1f, y + h * 0.55f);
            path.lineTo(x + w * 0.4f, y + h * 0.85f);
            path.lineTo(x + w * 0.92f, y + h * 0.18f);
        } else if (kind_ == Kind::Error) {
            path.startNewSubPath(x + w * 0.5f, y + h * 0.08f);
            path.lineTo(x + w * 0.5f, y + h * 0.62f);
            g.fillEllipse(x + w * 0.5f - 1.6f, y + h * 0.84f, 3.2f, 3.2f);
        } else { // arrow down into a tray
            path.startNewSubPath(x + w * 0.5f, y);
            path.lineTo(x + w * 0.5f, y + h * 0.66f);
            path.startNewSubPath(x + w * 0.22f, y + h * 0.4f);
            path.lineTo(x + w * 0.5f, y + h * 0.68f);
            path.lineTo(x + w * 0.78f, y + h * 0.4f);
            path.startNewSubPath(x + w * 0.05f, y + h * 0.78f);
            path.lineTo(x + w * 0.05f, y + h);
            path.lineTo(x + w * 0.95f, y + h);
            path.lineTo(x + w * 0.95f, y + h * 0.78f);
        }
        g.strokePath(path, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void timerCallback() override { repaint(); }

    Kind kind_ = Kind::Download;
    juce::String title_, text_;
    juce::String fromVersion_, toVersion_;
    juce::StringArray points_;
    std::unique_ptr<juce::TextButton> primary_, secondary_;
    std::function<void()> onPrimary, onSecondary;
};

// ---------------------------------------------------------------------------

Updater::Updater() : juce::Thread("Raumklang update") {}
Updater::~Updater() {
    stopThread(20000);
    sheet_.reset();
}

int Updater::currentBuild() { return demoMode() ? 1 : RK_BUILD_NUMBER; }

bool Updater::parse(const juce::String& json, const juce::String& platform, Info& out) {
    const juce::var v = juce::JSON::parse(json);
    if (!v.isObject()) return false;
    out.version = v["version"].toString();
    out.notes = v["notes"].toString();
    out.build = (int)v["build"];
    const juce::var f = v["files"][juce::Identifier(platform.isEmpty() ? "none" : platform)];
    if (!f.isObject()) return false;
    out.fileName = f["name"].toString();
    out.sha256 = f["sha256"].toString().toLowerCase();
    out.size = (juce::int64)f["size"];
    // Only plain file names: the feed must not point anywhere else.
    return out.build > 0 && out.fileName.isNotEmpty() && !out.fileName.containsAnyOf("/\\:") && out.sha256.length() == 64;
}

juce::StringArray Updater::notePoints(const juce::String& notes) {
    juce::StringArray out;
    for (auto line : juce::StringArray::fromLines(notes)) {
        line = line.trim();
        if (line.startsWith("- ") || line.startsWith("* ")) line = line.substring(2).trim();
        line = line.removeCharacters("*`");
        // Old feeds carried only the merge commit's subject, which says nothing to a user.
        if (line.isEmpty() || line.startsWithIgnoreCase("Merge pull request") || line.startsWithIgnoreCase("Merge branch")) continue;
        out.add(line);
        if (out.size() == 6) break;
    }
    return out;
}

void Updater::checkInBackgroundIfDue(juce::Component* parent, juce::PropertySet& settings) {
    if (currentBuild() <= 0 || (platformKey().isEmpty() && !demoMode())) return;
    const juce::int64 now = juce::Time::currentTimeMillis();
    if (now - settings.getValue("lastUpdateCheck", "0").getLargeIntValue() < 20LL * 3600 * 1000 && !demoMode()) return;
    settings.setValue("lastUpdateCheck", juce::String(now));
    juce::Timer::callAfterDelay(5000, [this, sp = juce::Component::SafePointer<juce::Component>(parent)] {
        if (sp != nullptr) check(sp, false);
    });
}

void Updater::showResultOfLastUpdate(juce::Component* parent) {
    const juce::File f = statusFile();
    if (!f.existsAsFile()) return;
    const juce::StringArray lines = juce::StringArray::fromLines(f.loadFileAsString().trim());
    f.deleteFile();
    if (lines.isEmpty() || lines[0] != "failed") return; // "ok": the new version is running, nothing to say
    parent_ = parent;
    juce::String text = tr("Das Update wurde geladen, konnte die App aber nicht ersetzen. Es läuft weiter die bisherige Version.");
    const juce::String copy = lines[1];
#if JUCE_MAC
    text << "\n\n" << tr("Wahrscheinlich hat macOS es blockiert (Systemeinstellungen → Datenschutz & Sicherheit → App-Verwaltung). "
                         "Die neue Version liegt im Ordner „Downloads/Raumklang-Update“: Raumklang.app von dort in den Programme-Ordner ziehen und ersetzen.");
#endif
    showSheet();
    sheet_->set(UpdateSheet::Kind::Error, tr("Update nicht installiert"), text);
    sheet_->setButtons(copy.isNotEmpty() ? tr("Im Finder zeigen") : tr("Download-Seite öffnen"),
                       [this, copy] {
                           if (copy.isNotEmpty() && juce::File(copy).exists()) juce::File(copy).revealToUser();
                           else juce::URL(kPage).launchInDefaultBrowser();
                           closeSheet();
                       },
                       tr("Schließen"), [this] { closeSheet(); });
}

void Updater::showSheet() {
    if (parent_ == nullptr) return;
    if (sheet_ == nullptr) {
        sheet_ = std::make_unique<UpdateSheet>();
        parent_->addAndMakeVisible(*sheet_);
    }
    sheet_->setBounds(parent_->getLocalBounds());
    sheet_->toFront(true);
    sheet_->grabKeyboardFocus();
}

void Updater::closeSheet() {
    // Called from the sheet's own buttons: delete it after the click has finished.
    juce::MessageManager::callAsync([this, sp = juce::Component::SafePointer<juce::Component>(sheet_.get())] {
        if (sp != nullptr) sheet_.reset();
    });
}

void Updater::check(juce::Component* parent, bool manual) {
    if (isThreadRunning() || downloading_) return;
    parent_ = parent;
    manual_ = manual;
    if (currentBuild() <= 0 || (platformKey().isEmpty() && !demoMode())) {
        if (manual) {
            showSheet();
            sheet_->set(UpdateSheet::Kind::Done, tr("Updates"), tr("Diese Version wurde selbst gebaut und aktualisiert sich nicht automatisch."));
            sheet_->setButtons("OK", [this] { closeSheet(); });
        }
        return;
    }
    startThread();
}

void Updater::run() {
    if (downloading_) {
        download();
        return;
    }
    // What is the newest version?
    juce::String text;
    int status = 0;
    if (demoMode()) {
        status = 200;
        text = juce::String::fromUTF8(R"({"version": "0.1.47", "build": 47, "notes": "- Menüleiste auf dem Mac\n- Update mit Fortschritt und klaren Meldungen",
                   "files": {"mac": {"name": "x.zip", "sha256": "0000000000000000000000000000000000000000000000000000000000000000", "size": 29000000},
                             "win": {"name": "x.zip", "sha256": "0000000000000000000000000000000000000000000000000000000000000000", "size": 29000000},
                             "none": {"name": "x.zip", "sha256": "0000000000000000000000000000000000000000000000000000000000000000", "size": 29000000}}})");
    } else if (auto in = juce::URL(kFeed).createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(10000).withStatusCode(&status).withNumRedirectsToFollow(5)))
        text = in->readEntireStreamAsString();
    Info got;
    if (threadShouldExit()) return;
    if (status != 200 || !parse(text, demoMode() && platformKey().isEmpty() ? juce::String("none") : platformKey(), got)) {
        if (manual_)
            fail(status == 404 ? tr("Keine Update-Informationen gefunden. Ist das Repository öffentlich?")
                               : tr("Der Update-Server ist gerade nicht erreichbar."));
        return;
    }
    juce::MessageManager::callAsync([this, got] {
        if (got.build <= currentBuild()) {
            if (manual_) {
                showSheet();
                sheet_->set(UpdateSheet::Kind::Done, tr("Raumklang ist auf dem neuesten Stand."),
                            tr("Installiert ist") + " " + juce::String(JUCE_APPLICATION_VERSION_STRING) + ".");
                sheet_->setButtons("OK", [this] { closeSheet(); });
            }
            return;
        }
        info_ = got;
        showSheet();
        juce::String msg = tr("Raumklang lädt das Update, beendet sich und startet neu. Das Projekt ist gesichert.");
        sheet_->setVersions(juce::String(JUCE_APPLICATION_VERSION_STRING), got.version);
        const juce::StringArray points = notePoints(got.notes);
        if (!points.isEmpty()) msg << "\n\n" << tr("Neu in dieser Version:");
        sheet_->set(UpdateSheet::Kind::Download, tr("Neue Version verfügbar"), msg, points);
        sheet_->setButtons(tr("Jetzt aktualisieren"), [this] { startDownload(); }, tr("Später"), [this] { closeSheet(); });
    });
}

void Updater::startDownload() {
    if (isThreadRunning()) return;
    downloading_ = true;
    received_ = 0;
    sheet_->set(UpdateSheet::Kind::Progress, tr("Update wird geladen"), "Raumklang " + info_.version);
    sheet_->progress = [this] { return info_.size > 0 ? (double)received_.load() / (double)info_.size : -1.0; };
    sheet_->progressText = [this] {
        const juce::int64 r = received_.load();
        if (info_.size > 0 && r < info_.size) return megabytes(r) + " " + tr("von") + " " + megabytes(info_.size);
        return tr("Wird geprüft und entpackt …");
    };
    sheet_->setButtons({}, {}, tr("Abbrechen"), [this] {
        signalThreadShouldExit();
        juce::MessageManager::callAsync([this] {
            stopThread(5000);
            downloading_ = false;
            if (tmp_.exists()) tmp_.deleteRecursively();
            closeSheet();
        });
    });
    logFile().deleteFile();
    log("Update von " + juce::String(JUCE_APPLICATION_VERSION_STRING) + " auf " + info_.version);
    startThread();
}

void Updater::fail(const juce::String& message) {
    log("Fehler: " + message);
    juce::MessageManager::callAsync([this, message] {
        downloading_ = false;
        if (tmp_.exists()) tmp_.deleteRecursively();
        showSheet();
        if (sheet_ == nullptr) return;
        sheet_->set(UpdateSheet::Kind::Error, tr("Update nicht möglich"), message);
        sheet_->setButtons(tr("Download-Seite öffnen"), [this] { juce::URL(kPage).launchInDefaultBrowser(); closeSheet(); },
                           tr("Schließen"), [this] { closeSheet(); });
    });
}

void Updater::download() {
    tmp_ = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("raumklang-update-" + juce::String(juce::Time::currentTimeMillis()));
    tmp_.createDirectory();
    const juce::File target = tmp_.getChildFile(info_.fileName);
    const juce::URL url = juce::URL(kFeed).getParentURL().getChildURL(info_.fileName);
    int status = 0;
    {
        auto in = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                            .withConnectionTimeoutMs(15000).withStatusCode(&status).withNumRedirectsToFollow(5));
        juce::FileOutputStream out(target);
        if (in == nullptr || status != 200 || !out.openedOk()) {
            fail(tr("Download fehlgeschlagen. Ist die Internetverbindung da?"));
            return;
        }
        while (!in->isExhausted() && !threadShouldExit()) {
            char buf[65536];
            const int n = in->read(buf, sizeof(buf));
            if (n <= 0) break;
            out.write(buf, (size_t)n);
            received_ += n;
        }
        out.flush();
    }
    if (threadShouldExit()) return;
    log("Geladen: " + juce::String(received_.load()) + " Bytes");
    if (juce::SHA256(target).toHexString().toLowerCase() != info_.sha256) {
        fail(tr("Die geladene Datei ist beschädigt (Prüfsumme stimmt nicht). Bitte später erneut versuchen."));
        return;
    }
    const juce::File unpacked = tmp_.getChildFile("neu");
#if JUCE_MAC
    {
        // ditto keeps permissions, symlinks and signatures inside the bundle.
        juce::ChildProcess unzip;
        if (!unzip.start(juce::StringArray{"/usr/bin/ditto", "-x", "-k", target.getFullPathName(), unpacked.getFullPathName()}) ||
            !unzip.waitForProcessToFinish(120000) || unzip.getExitCode() != 0) {
            fail(tr("Das Update ließ sich nicht entpacken."));
            return;
        }
    }
#else
    {
        juce::ZipFile zip(target);
        if (zip.getNumEntries() == 0 || zip.uncompressTo(unpacked).failed()) {
            fail(tr("Das Update ließ sich nicht entpacken."));
            return;
        }
    }
#endif
    const juce::File folder = unpacked.getChildFile("Raumklang");
    if (!folder.isDirectory()) {
        fail(tr("Das Update ließ sich nicht entpacken."));
        return;
    }
    if (threadShouldExit()) return;
    const juce::String pid(juce::String((juce::int64)
#if JUCE_WINDOWS
                                            ::GetCurrentProcessId()
#else
                                            ::getpid()
#endif
                                            ));
    statusFile().deleteFile();

#if JUCE_MAC
    const juce::File app = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    // The app swaps its own bundle while it still runs (macOS allows a running app to be renamed;
    // the process keeps its files open). Done here and not in a script after quitting: an app may
    // change itself, but a script started by it is a different process, which App Management
    // ("App-Verwaltung", macOS 13 and later) may block. That left the old version in place.
    auto manualCopy = [this, folder, app](const juce::String& why) {
        log(why);
        const juce::File copy = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Downloads").getChildFile("Raumklang-Update");
        copy.deleteRecursively();
        juce::ChildProcess cp;
        const bool copied = cp.start(juce::StringArray{"/usr/bin/ditto", folder.getFullPathName(), copy.getFullPathName()}) &&
                            cp.waitForProcessToFinish(60000) && cp.getExitCode() == 0;
        juce::MessageManager::callAsync([this, copied, copy, app] {
            downloading_ = false;
            if (tmp_.exists()) tmp_.deleteRecursively();
            showSheet();
            juce::String text = tr("Raumklang darf sich an diesem Ort nicht selbst ersetzen") + " (" + app.getParentDirectory().getFullPathName() + ").";
            if (copied)
                text << "\n\n" << tr("Die neue Version liegt jetzt im Ordner „Downloads/Raumklang-Update“. Raumklang beenden, "
                                     "Raumklang.app von dort in den Programme-Ordner ziehen und ersetzen. Die Plugins installiert Raumklang beim nächsten Start selbst.");
            sheet_->set(UpdateSheet::Kind::Error, tr("Update nicht installiert"), text);
            sheet_->setButtons(copied ? tr("Im Finder zeigen") : tr("Download-Seite öffnen"),
                               [this, copied, copy] {
                                   if (copied) copy.getChildFile("Raumklang.app").revealToUser();
                                   else juce::URL(kPage).launchInDefaultBrowser();
                                   closeSheet();
                               },
                               tr("Schließen"), [this] { closeSheet(); });
        });
    };
    if (app.getFullPathName().startsWith("/Volumes/")) {
        manualCopy("App läuft aus einem Disk-Image: " + app.getFullPathName());
        return;
    }
    const juce::File alt = app.getParentDirectory().getChildFile(app.getFileName() + ".alt");
    alt.deleteRecursively();
    if (!app.moveFileTo(alt)) {
        manualCopy("Alte App ließ sich nicht umbenennen: " + app.getFullPathName());
        return;
    }
    {
        juce::ChildProcess cp;
        const bool ok = cp.start(juce::StringArray{"/usr/bin/ditto", folder.getChildFile("Raumklang.app").getFullPathName(), app.getFullPathName()}) &&
                        cp.waitForProcessToFinish(120000) && cp.getExitCode() == 0 &&
                        app.getChildFile("Contents/MacOS").isDirectory();
        if (!ok) {
            app.deleteRecursively();
            alt.moveFileTo(app);
            manualCopy("Kopieren der neuen App fehlgeschlagen, alte App bleibt");
            return;
        }
    }
    log("Neue App liegt an ihrem Platz: " + app.getFullPathName());
    statusFile().replaceWithText("ok\n");

    // After quitting: remove the old bundle, install the plugins, start the new version.
    const juce::File script = tmp_.getChildFile("update.sh");
    script.replaceWithText(
        "#!/bin/sh\n"
        "APP=\"$1\"; NEW=\"$2\"; PID=\"$3\"; TMP=\"$4\"; LOG=\"$5\"\n"
        "exec >>\"$LOG\" 2>&1\n"
        "echo \"$(date '+%F %T')  Skript: warte auf das Ende von Raumklang ($PID)\"\n"
        "N=0; while kill -0 \"$PID\" 2>/dev/null && [ $N -lt 100 ]; do sleep 0.3; N=$((N+1)); done\n"
        "rm -rf \"$APP.alt\"\n"
        "AU=\"$HOME/Library/Audio/Plug-Ins/Components\"; VST=\"$HOME/Library/Audio/Plug-Ins/VST3\"\n"
        "mkdir -p \"$AU\" \"$VST\"\n"
        "for p in \"$NEW\"/Plugins/*.component; do [ -e \"$p\" ] && rm -rf \"$AU/$(basename \"$p\")\" && /usr/bin/ditto \"$p\" \"$AU/$(basename \"$p\")\"; done\n"
        "for p in \"$NEW\"/Plugins/*.vst3; do [ -e \"$p\" ] && rm -rf \"$VST/$(basename \"$p\")\" && /usr/bin/ditto \"$p\" \"$VST/$(basename \"$p\")\"; done\n"
        "/usr/bin/xattr -dr com.apple.quarantine \"$APP\" \"$AU\"/Raumklang*.component \"$VST\"/Raumklang*.vst3 2>/dev/null\n"
        "killall -9 AudioComponentRegistrar 2>/dev/null\n"
        "echo \"$(date '+%F %T')  Skript: fertig, starte die neue Version\"\n"
        "/usr/bin/open \"$APP\"\n"
        "rm -rf \"$TMP\"\n");
    juce::ChildProcess chmod;
    chmod.start(juce::StringArray{"/bin/chmod", "755", script.getFullPathName()});
    chmod.waitForProcessToFinish(5000);
    // Detached: survives the app quitting. If it does not start, the new version is in place
    // anyway and starts with the next click on the icon.
    juce::ChildProcess launcher;
    const bool launched = launcher.start(juce::StringArray{"/bin/sh", "-c", "nohup \"$0\" \"$1\" \"$2\" \"$3\" \"$4\" \"$5\" >/dev/null 2>&1 &",
                                                           script.getFullPathName(), app.getFullPathName(), folder.getFullPathName(), pid,
                                                           tmp_.getFullPathName(), logFile().getFullPathName()}) &&
                          launcher.waitForProcessToFinish(5000);
    if (!launched) log("Neustart-Skript ließ sich nicht starten, die neue Version startet beim nächsten Öffnen");
    const bool started = true;
#elif JUCE_WINDOWS
    // After quitting: replace the exe, install the VST3 plugins (asks for administrator rights), start again.
    const juce::File exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const juce::String exePath = exe.getFullPathName();
    const juce::String logPath = logFile().getFullPathName(), statusPath = statusFile().getFullPathName();
    // The VST3 folder needs administrator rights: Windows asks once (PowerShell script, so paths with spaces are safe).
    const juce::File plugins = tmp_.getChildFile("plugins.ps1");
    plugins.replaceWithText("$src = '" + folder.getChildFile("Plugins").getFullPathName().replace("'", "''") + "'\r\n"
                            "$dst = Join-Path $env:CommonProgramFiles 'VST3'\r\n"
                            "Start-Process -Wait -WindowStyle Hidden -Verb RunAs -FilePath 'robocopy.exe' "
                            "-ArgumentList @(('\"' + $src + '\"'), ('\"' + $dst + '\"'), '/E', '/NFL', '/NDL', '/NJH', '/NJS')\r\n");
    const juce::File script = tmp_.getChildFile("update.cmd");
    script.replaceWithText(
        "@echo off\r\n"
        ":wait\r\n"
        "tasklist /FI \"PID eq " + pid + "\" | find \"" + pid + "\" >nul && (timeout /t 1 /nobreak >nul & goto wait)\r\n"
        "copy /y \"" + exePath + "\" \"" + exePath + ".alt\" >nul\r\n"
        "copy /y \"" + folder.getChildFile("Raumklang.exe").getFullPathName() + "\" \"" + exePath + "\" >nul && (echo ok> \"" + statusPath + "\") || (copy /y \"" + exePath +
        ".alt\" \"" + exePath + "\" >nul & echo failed> \"" + statusPath + "\" & echo Kopieren fehlgeschlagen>> \"" + logPath + "\")\r\n"
        "del \"" + exePath + ".alt\" >nul 2>&1\r\n"
        "powershell -NoProfile -ExecutionPolicy Bypass -File \"" + plugins.getFullPathName() + "\"\r\n"
        "start \"\" \"" + exePath + "\"\r\n");
    juce::ChildProcess launcher;
    const bool started = launcher.start("cmd.exe /c start \"\" /min \"" + script.getFullPathName() + "\"");
#else
    const bool started = false;
#endif
    if (!started) {
        fail(tr("Das Update ließ sich nicht starten."));
        return;
    }
    log("Installationsskript gestartet, Raumklang beendet sich");
    juce::MessageManager::callAsync([this] {
        if (sheet_ != nullptr) {
            sheet_->set(UpdateSheet::Kind::Progress, tr("Raumklang startet neu"), tr("Das Update wird installiert. Das dauert wenige Sekunden."));
            sheet_->progress = [] { return -1.0; };
            sheet_->progressText = [] { return juce::String(); };
            sheet_->setButtons({}, {});
        }
        // Quit a moment later, so the sheet is seen; the script waits for this process to end.
        juce::Timer::callAfterDelay(900, [] {
            if (auto* a = juce::JUCEApplication::getInstance()) a->systemRequestedQuit();
        });
    });
}

} // namespace rkapp
