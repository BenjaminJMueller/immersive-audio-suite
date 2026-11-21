#include "Updater.h"

#include <juce_cryptography/juce_cryptography.h>
#include <rk_ui/Strings.h>

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

void info(juce::Component* parent, const juce::String& title, const juce::String& text) {
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::InfoIcon)
                                     .withTitle(title)
                                     .withMessage(text)
                                     .withButton("OK", 0)
                                     .withAssociatedComponent(parent),
                                 nullptr);
}

} // namespace

Updater::Updater() : juce::Thread("Raumklang update") {}
Updater::~Updater() { stopThread(20000); }

int Updater::currentBuild() { return RK_BUILD_NUMBER; }

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

void Updater::checkInBackgroundIfDue(juce::Component* parent, juce::PropertySet& settings) {
    if (currentBuild() <= 0 || platformKey().isEmpty()) return;
    const juce::int64 now = juce::Time::currentTimeMillis();
    if (now - settings.getValue("lastUpdateCheck", "0").getLargeIntValue() < 20LL * 3600 * 1000) return;
    settings.setValue("lastUpdateCheck", juce::String(now));
    juce::Timer::callAfterDelay(5000, [this, sp = juce::Component::SafePointer<juce::Component>(parent)] {
        if (sp != nullptr) check(sp, false);
    });
}

void Updater::check(juce::Component* parent, bool manual) {
    if (isThreadRunning()) return;
    if (currentBuild() <= 0 || platformKey().isEmpty()) {
        if (manual) info(parent, tr("Updates"), tr("Diese Version wurde selbst gebaut und aktualisiert sich nicht automatisch."));
        return;
    }
    parent_ = parent;
    manual_ = manual;
    downloading_ = false;
    startThread();
}

void Updater::run() {
    if (!downloading_) {
        // 1) What is the newest version?
        juce::String text;
        int status = 0;
        if (auto in = juce::URL(kFeed).createInputStream(
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(10000).withStatusCode(&status).withNumRedirectsToFollow(5)))
            text = in->readEntireStreamAsString();
        Info got;
        if (threadShouldExit()) return;
        if (status != 200 || !parse(text, platformKey(), got)) {
            finish(false, status == 404 ? tr("Keine Update-Informationen gefunden. Ist das Repository öffentlich?")
                                        : tr("Der Update-Server ist gerade nicht erreichbar."));
            return;
        }
        juce::MessageManager::callAsync([this, got] {
            if (got.build <= currentBuild()) {
                if (manual_) info(parent_, tr("Updates"), tr("Raumklang ist auf dem neuesten Stand."));
                return;
            }
            info_ = got;
            juce::String msg = "Raumklang " + got.version + " " + tr("ist verfügbar.") + "\n" + tr("Installiert ist") + " " +
                               juce::String(JUCE_APPLICATION_VERSION_STRING) + ".";
            if (got.notes.isNotEmpty()) msg << "\n\n" << got.notes;
            msg << "\n\n" << tr("Das Update wird geladen, danach startet die App neu. Das Projekt ist gesichert.");
            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                             .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                             .withTitle(tr("Update"))
                                             .withMessage(msg)
                                             .withButton(tr("Jetzt aktualisieren"), 1)
                                             .withButton(tr("Später"), 0)
                                             .withAssociatedComponent(parent_),
                                         [this](int r) {
                                             if (r != 1) return;
                                             downloading_ = true;
                                             startThread();
                                         });
        });
        return;
    }

    // 2) Download and verify.
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
            finish(false, tr("Download fehlgeschlagen."));
            return;
        }
        while (!in->isExhausted() && !threadShouldExit()) {
            char buf[65536];
            const int n = in->read(buf, sizeof(buf));
            if (n <= 0) break;
            out.write(buf, (size_t)n);
        }
        out.flush();
    }
    if (threadShouldExit()) return;
    if (juce::SHA256(target).toHexString().toLowerCase() != info_.sha256) {
        finish(false, tr("Die geladene Datei ist beschädigt (Prüfsumme stimmt nicht). Bitte später erneut versuchen."));
        return;
    }
    const juce::File unpacked = tmp_.getChildFile("neu");
#if JUCE_MAC
    {
        // ditto keeps permissions, symlinks and signatures inside the bundle.
        juce::ChildProcess unzip;
        if (!unzip.start(juce::StringArray{"/usr/bin/ditto", "-x", "-k", target.getFullPathName(), unpacked.getFullPathName()}) ||
            !unzip.waitForProcessToFinish(120000) || unzip.getExitCode() != 0) {
            finish(false, tr("Das Update ließ sich nicht entpacken."));
            return;
        }
    }
#else
    {
        juce::ZipFile zip(target);
        if (zip.getNumEntries() == 0 || zip.uncompressTo(unpacked).failed()) {
            finish(false, tr("Das Update ließ sich nicht entpacken."));
            return;
        }
    }
#endif
    const juce::File folder = unpacked.getChildFile("Raumklang");
    if (!folder.isDirectory()) {
        finish(false, tr("Das Update ließ sich nicht entpacken."));
        return;
    }
    const juce::String pid(juce::String((juce::int64)
#if JUCE_WINDOWS
                                            ::GetCurrentProcessId()
#else
                                            ::getpid()
#endif
                                            ));

#if JUCE_MAC
    // 3) After quitting: swap the bundle (keep the old one on failure), install the plugins, start again.
    const juce::File app = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    const juce::File script = tmp_.getChildFile("update.sh");
    script.replaceWithText(
        "#!/bin/sh\n"
        "APP=\"$1\"; NEW=\"$2\"; PID=\"$3\"; TMP=\"$4\"\n"
        "while kill -0 \"$PID\" 2>/dev/null; do sleep 0.3; done\n"
        "rm -rf \"$APP.alt\"\n"
        "if mv \"$APP\" \"$APP.alt\" && /usr/bin/ditto \"$NEW/Raumklang.app\" \"$APP\"; then rm -rf \"$APP.alt\"; else rm -rf \"$APP\"; mv \"$APP.alt\" \"$APP\"; fi\n"
        "AU=\"$HOME/Library/Audio/Plug-Ins/Components\"; VST=\"$HOME/Library/Audio/Plug-Ins/VST3\"\n"
        "mkdir -p \"$AU\" \"$VST\"\n"
        "for p in \"$NEW\"/Plugins/*.component; do [ -e \"$p\" ] && rm -rf \"$AU/$(basename \"$p\")\" && /usr/bin/ditto \"$p\" \"$AU/$(basename \"$p\")\"; done\n"
        "for p in \"$NEW\"/Plugins/*.vst3; do [ -e \"$p\" ] && rm -rf \"$VST/$(basename \"$p\")\" && /usr/bin/ditto \"$p\" \"$VST/$(basename \"$p\")\"; done\n"
        "/usr/bin/xattr -dr com.apple.quarantine \"$APP\" \"$AU\"/Raumklang*.component \"$VST\"/Raumklang*.vst3 2>/dev/null\n"
        "killall -9 AudioComponentRegistrar 2>/dev/null\n"
        "/usr/bin/open \"$APP\"\n"
        "rm -rf \"$TMP\"\n");
    juce::ChildProcess chmod;
    chmod.start(juce::StringArray{"/bin/chmod", "755", script.getFullPathName()});
    chmod.waitForProcessToFinish(5000);
    // Detached: survives the app quitting.
    juce::ChildProcess launcher;
    launcher.start(juce::StringArray{"/bin/sh", "-c",
                                     "nohup \"$0\" \"$1\" \"$2\" \"$3\" \"$4\" >/dev/null 2>&1 &", script.getFullPathName(),
                                     app.getFullPathName(), folder.getFullPathName(), pid, tmp_.getFullPathName()});
    launcher.waitForProcessToFinish(5000);
    finish(true, {});
#elif JUCE_WINDOWS
    // 3) After quitting: replace the exe, install the VST3 plugins (asks for administrator rights), start again.
    const juce::File exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const juce::String exePath = exe.getFullPathName();
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
        "copy /y \"" + folder.getChildFile("Raumklang.exe").getFullPathName() + "\" \"" + exePath + "\" >nul || copy /y \"" + exePath +
        ".alt\" \"" + exePath + "\" >nul\r\n"
        "del \"" + exePath + ".alt\" >nul 2>&1\r\n"
        "powershell -NoProfile -ExecutionPolicy Bypass -File \"" + plugins.getFullPathName() + "\"\r\n"
        "start \"\" \"" + exePath + "\"\r\n");
    juce::ChildProcess launcher;
    launcher.start("cmd.exe /c start \"\" /min \"" + script.getFullPathName() + "\"");
    finish(true, {});
#endif
}

void Updater::finish(bool ok, const juce::String& message) {
    juce::MessageManager::callAsync([this, ok, message] {
        if (ok) {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            return;
        }
        if (tmp_.exists()) tmp_.deleteRecursively();
        if (!manual_ && !downloading_) return; // quiet background check
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::WarningIcon)
                                         .withTitle(tr("Update nicht möglich"))
                                         .withMessage(message)
                                         .withButton("OK", 0)
                                         .withButton(tr("Download-Seite öffnen"), 1)
                                         .withAssociatedComponent(parent_),
                                     [](int r) {
                                         if (r == 1) juce::URL(kPage).launchInDefaultBrowser();
                                     });
    });
}

} // namespace rkapp
