#include "Strings.h"

#include <map>

namespace rkui {
namespace {
Language g_lang = Language::German;

const std::map<std::string, const char*>& english() {
    static const std::map<std::string, const char*> m = {
        {"Lautsprecher", "Speakers"},
        {"Quellen", "Sources"},
        {"Raum", "Room"},
        {"Ausgabe", "Output"},
        {"Neues Projekt", "New project"},
        {"Automatisch gesichert", "Saved automatically"},
        {"Noch nicht gesichert", "Not saved yet"},
        {"Rückgängig", "Undo"},
        {"Wiederholen", "Redo"},
        {"Öffnen …", "Open …"},
        {"Sichern unter …", "Save as …"},
        {"Hilfe", "Help"},
        {"Sprache: Deutsch", "Language: German"},
        {"Sprache: Englisch", "Language: English"},
        {"Vorlage", "Preset"},
        {"Anzahl innen", "Inner count"},
        {"Radius innen", "Inner radius"},
        {"Anzahl außen", "Outer count"},
        {"Radius außen", "Outer radius"},
        {"Anzahl", "Count"},
        {"Radius", "Radius"},
        {"Höhe", "Height"},
        {"Name", "Name"},
        {"Azimut", "Azimuth"},
        {"Elevation", "Elevation"},
        {"Abstand", "Distance"},
        {"Pegel", "Level"},
        {"Verzögerung", "Delay"},
        {"Ausrichtung", "Orientation"},
        {"Zur Mitte", "Towards centre"},
        {"Nach außen", "Outwards"},
        {"Nach innen", "Inwards"},
        {"Subwoofer", "Subwoofer"},
        {"Aktiv", "Active"},
        {"Lautsprecher hinzufügen", "Add speaker"},
        {"Entfernen", "Remove"},
        {"Verfahren", "Method"},
        {"Automatisch", "Automatic"},
        {"Quelle hinzufügen", "Add source"},
        {"Eingang", "Input"},
        {"Testsignal", "Test signal"},
        {"Rosa Rauschen", "Pink noise"},
        {"Sinuston", "Sine tone"},
        {"Klicks", "Clicks"},
        {"Breite", "Width"},
        {"Hallanteil", "Reverb send"},
        {"Entfernung", "Distance cues"},
        {"Bewegung", "Motion"},
        {"Aus", "Off"},
        {"Kreisen", "Orbit"},
        {"Tempo", "Speed"},
        {"Stumm", "Mute"},
        {"Solo", "Solo"},
        {"Nachhallzeit", "Decay time"},
        {"Raumgröße", "Room size"},
        {"Höhendämpfung", "High damping"},
        {"Vorverzögerung", "Pre-delay"},
        {"Hallpegel", "Reverb level"},
        {"Trocken", "Dry"},
        {"Studio", "Studio"},
        {"Saal", "Hall"},
        {"Kirche", "Church"},
        {"Audiogerät", "Audio device"},
        {"Gesamtpegel", "Master level"},
        {"Schutzbegrenzer", "Safety limiter"},
        {"Kopfhörer-Vorschau", "Headphone preview"},
        {"Lautsprecher einzeln anspielen", "Test speakers one by one"},
        {"Test beenden", "Stop test"},
        {"Pegelanzeige", "Meters"},
        {"Mit Demo beginnen", "Start with the demo"},
        {"Leer beginnen", "Start empty"},
        {"Willkommen bei Raumklang", "Welcome to Raumklang"},
        {"Sitzt gut", "Looks good"},
        {"Bitte prüfen", "Please check"},
        {"Keine Lautsprecher", "No speakers"},
        {"Quelle", "Source"},
        {"aktiv", "active"},
        {"Ausgang", "Output"},
        {"Details und Hilfen", "Details and help"},
        {"Lautsprecher umschließen den Hörplatz. VBAP passt am besten.", "The speakers surround the listening area. VBAP fits best."},
        {"Donut erkannt: Lautsprecher stehen sich gegenüber. DBAP wird verwendet.", "Donut detected: speakers face each other. DBAP is used."},
        {"Der Hörplatz ist nicht umschlossen. DBAP verteilt den Klang nach Abstand.", "The listening area is not enclosed. DBAP distributes sound by distance."},
        {"Lautsprecher haben sehr unterschiedliche Abstände. Pegel und Verzögerung unter Lautsprecher angleichen.", "Speakers are at very different distances. Align level and delay per speaker."},
        {"Nur ein Lautsprecher: Es gibt keine Richtung.", "Only one speaker: there is no direction."},
        {"Füge Lautsprecher hinzu oder wähle eine Vorlage.", "Add speakers or choose a preset."},
        {"Das Audiogerät hat weniger Ausgänge als Lautsprecher. Die übrigen bleiben stumm.", "The audio device has fewer outputs than speakers. The rest stay silent."},
        {"Kein Audiogerät geöffnet.", "No audio device open."},
        {"Ziehen verschiebt, Doppelklick setzt zurück.", "Drag to move, double-click to reset."},
    };
    return m;
}
} // namespace

void setLanguage(Language l) { g_lang = l; }
Language language() { return g_lang; }

juce::String tr(const char* german) {
    if (german == nullptr) return {};
    if (g_lang == Language::English) {
        const auto& m = english();
        const auto it = m.find(german);
        if (it != m.end()) return juce::String::fromUTF8(it->second);
    }
    return juce::String::fromUTF8(german);
}

juce::String tr(const juce::String& german) {
    if (g_lang == Language::German) return german;
    const auto& m = english();
    const auto it = m.find(german.toStdString());
    return it != m.end() ? juce::String::fromUTF8(it->second) : german;
}

juce::String num(double value, int decimals) {
    juce::String s = decimals <= 0 ? juce::String(juce::roundToInt(value)) : juce::String(value, decimals);
    return g_lang == Language::German ? s.replaceCharacter('.', ',') : s;
}

} // namespace rkui
