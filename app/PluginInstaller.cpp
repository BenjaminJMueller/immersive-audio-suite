#include "PluginInstaller.h"

namespace rkapp {
namespace {

juce::File bundledFolder() {
#if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::currentApplicationFile).getChildFile("Contents/PlugIns");
#else
    return {};
#endif
}

juce::File destinationFor(const juce::File& plugin) {
    const juce::File base = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Audio/Plug-Ins");
    return base.getChildFile(plugin.hasFileExtension("component") ? "Components" : "VST3").getChildFile(plugin.getFileName());
}

juce::Array<juce::File> bundledPlugins() {
    juce::Array<juce::File> out;
    const juce::File dir = bundledFolder();
    if (!dir.isDirectory()) return out;
    for (const auto& f : dir.findChildFiles(juce::File::findDirectories, false, "*"))
        if (f.hasFileExtension("component;vst3") && f.getFileName().startsWith("Raumklang")) out.add(f);
    out.sort();
    return out;
}

/// Plugins of earlier versions that no longer exist or were renamed. Left installed, they
/// show up in the DAW next to the current ones.
const char* const kObsoletePlugins[] = {"Raumklang Raum", "Raumklang R\xc3\xbc" "ckweg"};

bool run(const juce::StringArray& args, juce::String& output) {
    juce::ChildProcess p;
    if (!p.start(args)) return false;
    output = p.readAllProcessOutput();
    return p.waitForProcessToFinish(30000) && p.getExitCode() == 0;
}

} // namespace

bool hasBundledPlugins() { return !bundledPlugins().isEmpty(); }

bool pluginInstallDue(juce::PropertySet& settings, int build) {
    const auto plugins = bundledPlugins();
    if (plugins.isEmpty()) return false;
    if (settings.getIntValue("pluginsInstalledBuild", 0) != build) return true;
    for (const auto& p : plugins)
        if (!destinationFor(p).isDirectory()) return true; // removed by hand
    const juce::File user = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Audio/Plug-Ins");
    for (const char* name : kObsoletePlugins)
        if (user.getChildFile("Components").getChildFile(juce::String::fromUTF8(name) + ".component").exists() ||
            user.getChildFile("VST3").getChildFile(juce::String::fromUTF8(name) + ".vst3").exists())
            return true; // an old plugin came back (e.g. from a backup)
    return false;
}

PluginInstallResult installBundledPlugins() {
    PluginInstallResult r;
    const auto plugins = bundledPlugins();
    if (plugins.isEmpty()) return r;
    r.attempted = true;
    juce::String out;
    for (const auto& p : plugins) {
        const juce::File dst = destinationFor(p);
        dst.getParentDirectory().createDirectory();
        if (dst.exists() && !dst.deleteRecursively()) {
            r.error = "Konnte " + dst.getFullPathName() + " nicht ersetzen.";
            return r;
        }
        // ditto keeps the bundle and its signature intact.
        if (!run({"/usr/bin/ditto", p.getFullPathName(), dst.getFullPathName()}, out)) {
            r.error = "Kopieren von " + p.getFileName() + " fehlgeschlagen. " + out.trim();
            return r;
        }
        // The app itself was opened once through Gatekeeper; the copies must not carry the
        // download quarantine, or a DAW refuses them.
        run({"/usr/bin/xattr", "-dr", "com.apple.quarantine", dst.getFullPathName()}, out);
        r.installed.add(p.getFileName());
    }
    // Old plugins, in the user's folder and (if writable) the system-wide one.
    for (const auto& base : {juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Audio/Plug-Ins"),
                             juce::File("/Library/Audio/Plug-Ins")})
        for (const char* name : kObsoletePlugins)
            for (const char* ext : {"Components/%s.component", "VST3/%s.vst3"}) {
                const juce::File old = base.getChildFile(juce::String(ext).replace("%s", juce::String::fromUTF8(name)));
                if (old.exists() && old.deleteRecursively()) r.removed.add(old.getFileName());
            }
    // Makes Logic and GarageBand see new Audio Units without logging out.
    run({"/usr/bin/killall", "-9", "AudioComponentRegistrar"}, out);
    r.ok = true;
    return r;
}

void markPluginsInstalled(juce::PropertySet& settings, int build) {
    settings.setValue("pluginsInstalledBuild", build);
}

} // namespace rkapp
