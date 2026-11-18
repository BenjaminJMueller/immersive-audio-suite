# Raumklang

Deutsch · [English](README.en.md)

Raumklang ist ein freies Werkzeug für räumlichen Klang auf macOS und Windows. Es verteilt Klangquellen frei im Raum auf beliebig viele Lautsprecher, vom Stereopaar über Ring, Kuppel und 7.1.4 bis zum *Donut*, bei dem sich zwei Lautsprecherringe gegenüberstehen. Dazu gibt es Hall, eine Kopfhörer-Vorschau und zwei Plugins für die DAW. Ein Audio-Interface genügt: Die App spricht es direkt an, zusätzliche Programme für das Routing sind nicht nötig.

> **Frühe Version (0.1).** Raumklang ist neu. Die Klangberechnung ist ausführlich automatisch getestet, an echten Lautsprecheraufbauten aber noch kaum. Fehler und Lücken sind wahrscheinlich, siehe [Funktionsumfang und Grenzen](#funktionsumfang-und-grenzen).

**Herunterladen:** Noch keine Releases. Fertige Builds für macOS, Windows und Linux entstehen bei jedem Lauf der [CI](../../actions) und liegen dort unter *Artifacts*.

## Hintergrund

Für räumlichen Klang gibt es gute Werkzeuge, unter anderem:

- **FLUX Spat Revolution** und **IRCAM Spat** als vollständige Raumklang-Umgebungen
- **SpatGRIS / ControlGRIS** (Université de Montréal), frei, braucht aber ein virtuelles Audiogerät wie BlackHole oder JACK für das Routing
- **IEM Plug-in Suite** und **SPARTA** für Ambisonics in der DAW
- **d&b Soundscape**, **L-ISA** und **Holoplot** als Systeme der Lautsprecherhersteller

Raumklang will das Wichtigste davon frei und einfach anbieten: plug and play, verständlich auch ohne Vorerfahrung, und so stabil, dass es im Betrieb nicht abstürzt.

## Arbeitsablauf

1. **Lautsprecher.** Vorlage wählen (Stereo, Quadrofonie, 5.1, 7.1.4, Ring, Kuppel, Würfel, Donut) oder Lautsprecher frei setzen und ziehen. Raumklang prüft das Layout und sagt in Klartext, ob es passt und welches Verfahren es nimmt. *Abstände ausgleichen* gleicht Laufzeit und Pegel an, wenn Lautsprecher unterschiedlich weit weg stehen.
2. **Quellen.** Jede Quelle ist ein Eingang des Audio-Interfaces oder ein eingebautes Testsignal (rosa Rauschen, Sinuston, Klicks). Quellen werden in der Draufsicht gezogen oder über Azimut, Elevation und Abstand gesetzt, dazu Pegel, Breite, Hallanteil und Entfernungswirkung. *Kreisen* bewegt eine Quelle von selbst um den Hörplatz.
3. **Raum.** Hall mit Vorlagen (Trocken, Studio, Saal, Kirche) und Reglern für Nachhallzeit, Raumgröße, Höhendämpfung und Vorverzögerung.
4. **Ausgabe.** Audiogerät, Abtastrate und Puffer wählen. Gesamtpegel, Schutzbegrenzer, Kopfhörer-Vorschau und *Lautsprecher einzeln anspielen* zum Prüfen der Verkabelung.

Projekte werden automatisch gesichert, alle Schritte lassen sich rückgängig machen (⌘Z / Strg+Z). Beim ersten Start bietet eine Einführung an, mit einer Demo zu beginnen. Hat das Audiogerät weniger Ausgänge als das Layout Lautsprecher, schaltet die Demo von selbst die Kopfhörer-Vorschau ein. Die Oberfläche ist auf Deutsch, Englisch lässt sich unter *Hilfe* (?) umstellen.

## In der DAW

Zwei Plugins (VST3, auf dem Mac zusätzlich AU):

- **Raumklang Quelle** kommt auf jede Spur. Es setzt die Spur in den Raum und gibt einen Kanal pro Lautsprecher aus. Position, Breite, Pegel und Hallanteil sind automatisierbar. Die Spur muss so viele Kanäle haben wie das Layout Lautsprecher (z. B. in Reaper, Nuendo/Cubase oder Pro Tools Ultimate).
- **Raumklang Raum** kommt auf den Summen- bzw. Lautsprecher-Bus. Hier wird das Layout bearbeitet, dazu Laufzeit und Pegel je Lautsprecher, Kopfhörer-Vorschau, Lautsprechertest und Schutzbegrenzer.

Das Lautsprecher-Setup teilen sich App und Plugins über eine Datei im Benutzerordner (`Raumklang/Lautsprecher-Setup.json`). Ein in der App eingerichtetes Layout steht damit sofort in der DAW bereit, und alle Quellen-Plugins einer Sitzung verwenden immer denselben Raum. Das DAW-Projekt speichert das Layout mit.

## Technischer Ansatz

**Verfahren.** Drei Panning-Verfahren mit automatischer Auswahl:

- *VBAP* (Pulkki 1997) für Layouts, die den Hörplatz umschließen. 2D oder 3D wird selbst erkannt. Für unvollständige Kuppeln werden virtuelle Lautsprecher ergänzt, deren Anteil auf die Nachbarn verteilt wird. Breite über MDAP.
- *DBAP* (Lossius 2009) für Layouts ohne zentralen Hörplatz, erweitert um die Ausrichtung der Lautsprecher. Ein Lautsprecher, der von der Quelle wegzeigt, trägt weniger bei. Damit funktioniert der **Donut**: Innenring nach außen, Außenring nach innen, das Publikum dazwischen.
- *Ambisonics* bis 3. Ordnung (ACN/SN3D) mit AllRAD-Decoder und max-rE-Gewichtung.

**Hall.** Feedback Delay Network mit 16 Leitungen, Hadamard-Rückkopplung, Eingangsdiffusion und frequenzabhängiger Dämpfung. Jeder Lautsprecher bekommt eine eigene, dekorrelierte Mischung.

**Kopfhörer-Vorschau.** Kugelkopfmodell nach Brown und Duda: Laufzeitunterschied und Kopfschatten je Lautsprecher, ohne gemessene HRTF-Daten.

**Stabilität.** Der Audio-Thread reserviert keinen Speicher, sperrt nichts und wartet auf nichts. Layoutwechsel werden außerhalb des Audio-Threads vorbereitet und mit kurzer Blende übernommen. Alle Eingaben (auch NaN und Unendlich aus fehlerhaften Plugins) werden abgefangen, ein Schutzbegrenzer hält jeden Ausgang unter −0,1 dBFS. Projektdateien werden atomar geschrieben und beim Laden vollständig geprüft.

**Tests.** Bei jedem Push laufen in der CI:

- Unit-Tests für Geometrie, alle Panning-Verfahren, Hall, Kopfhörer-Vorschau und Engine
- ein Test, der beweist, dass der Audio-Thread nie Speicher reserviert oder freigibt
- Stresstests mit gleichzeitigem Audio- und Bedien-Thread, zufälligen Layouts, Blockgrößen, Abtastraten und unsinnigen Werten
- dieselben Tests unter AddressSanitizer, UndefinedBehaviorSanitizer und ThreadSanitizer
- 5 Minuten Fuzzing der Engine mit libFuzzer
- Tests der Projektdatei mit beschädigten und manipulierten Dateien
- ein Host-Stresstest beider Plugins (Kanalzahlen, Automation aus einem zweiten Thread, beschädigte Zustände, Editoren)
- [pluginval](https://github.com/Tracktion/pluginval) mit Strenge 10 auf macOS, Windows und Linux

## Funktionsumfang und Grenzen

| | |
|---|---|
| Lautsprecher | bis 128 in der App, bis 64 je Plugin-Spur, frei platzierbar, Ausrichtung, Pegel, Verzögerung, Subwoofer (Tiefpass 100 Hz) |
| Vorlagen | Stereo, Quadrofonie, 5.1, 7.1.4, Ring (2 bis 64), Kuppel, Würfel, Donut (Innen-/Außenring frei) |
| Quellen | bis 64 in der App, Eingänge des Audio-Interfaces oder Testsignale, mono oder stereo im Plugin |
| Verfahren | VBAP (2D/3D), DBAP mit Richtwirkung, Ambisonics 3. Ordnung |
| Hall | ein gemeinsamer Raum, FDN, Vorlagen und Regler |
| Ausgabe | direkt auf das Audiogerät (Core Audio, WASAPI, ASIO wenn mit SDK gebaut), Kopfhörer-Vorschau, Lautsprechertest, Schutzbegrenzer |
| Sprachen | Deutsch (Standard), Englisch |
| Systeme | macOS 11 oder neuer (Apple Silicon und Intel), Windows 10/11 64 Bit, Linux |

Bekannte Grenzen:

- Noch nicht an echten Mehrkanal-Aufbauten erprobt, nur in Tests und Simulation.
- Die Plugins rechnen in der DAW. DAWs ohne Mehrkanal-Spuren (z. B. Ableton Live, Logic außerhalb von Surround) können die Lautsprecherkanäle nicht weiterreichen. Eine direkte Verbindung vom Plugin zur App steht auf der [Roadmap](docs/ROADMAP.md).
- Bei Layoutwechseln während der Wiedergabe gibt es eine kurze Blende (20 ms). Beim Ziehen von Lautsprechern während der Wiedergabe ist das hörbar.
- Kopfhörer-Vorschau ohne individuelle HRTF, Elevation wird kaum abgebildet.
- Keine OSC-Fernsteuerung, keine Ambisonics-Eingänge, keine Raumeinmessung (siehe Roadmap).
- Mac-App und Plugins sind nicht von Apple signiert. Beim ersten Start *Systemeinstellungen → Datenschutz & Sicherheit → Dennoch öffnen*, oder im Terminal `xattr -dr com.apple.quarantine "/Applications/Raumklang.app"`. Unter Windows bei SmartScreen *Weitere Informationen → Trotzdem ausführen*.

## Selbst bauen

Voraussetzungen: CMake 3.22 oder neuer, ein C++20-Compiler (Xcode 15, Visual Studio 2022, GCC 12 oder Clang 15). JUCE wird beim Konfigurieren automatisch geladen.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

Nur die Engine mit Tests, ohne JUCE: `-DRK_BUILD_APP=OFF -DRK_BUILD_PLUGINS=OFF`. Mit Sanitizern: `-DRK_SANITIZE=address,undefined` oder `-DRK_SANITIZE=thread`.

Aufbau:

| Ordner | Inhalt |
|---|---|
| `core/` | Engine ohne Abhängigkeiten: Layouts, Panning, Hall, Kopfhörer-Vorschau, Ausgangsstufe |
| `shared/` | Oberfläche und Projektdatei, gemeinsam für App und Plugins |
| `app/` | Standalone-App |
| `plugins/` | *Raumklang Quelle* und *Raumklang Raum* |
| `tests/` | Unit-, Stress- und Fuzz-Tests der Engine |

## Lizenz

Raumklang steht unter der [GNU AGPLv3](LICENSE.md). Es baut auf [JUCE](https://juce.com) auf, das für Open-Source-Projekte unter AGPLv3 frei nutzbar ist. Weitere Bestandteile siehe [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
