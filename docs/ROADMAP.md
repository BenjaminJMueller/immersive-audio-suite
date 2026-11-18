# Roadmap

Stabilität geht vor neuen Funktionen. Jede Phase wird erst abgeschlossen, wenn ihre Tests in der CI auf macOS, Windows und Linux grün sind, auch unter den Sanitizern.

## Phase 1 · Grundlage (0.1, erledigt)

- Engine ohne Abhängigkeiten: VBAP 2D/3D, DBAP mit Richtwirkung (Donut), Ambisonics 3. Ordnung, FDN-Hall, Kopfhörer-Vorschau, Schutzbegrenzer
- Standalone-App mit vier Schritten (Lautsprecher, Quellen, Raum, Ausgabe), Einführung mit Demo, Rückgängig, automatisches Sichern
- Plugins *Raumklang Quelle* und *Raumklang Raum* (VST3, AU, Standalone) mit gemeinsamem Lautsprecher-Setup
- Tests: Unit, Echtzeit-Sicherheit (keine Speicherreservierung im Audio-Thread), Stress, Fuzzing, ASan/UBSan/TSan, Plugin-Host-Stresstest, pluginval

## Phase 2 · Ohne Routing aus der DAW in die App (0.2)

- Direkte Verbindung *Quelle*-Plugin → App über Shared Memory: Jede Spur bleibt Mono oder Stereo, die App rechnet und spielt auf das Audiogerät. Funktioniert damit auch in Ableton Live und Logic, ohne virtuelle Audiogeräte.
- Positionen aus der DAW-Automation erscheinen live in der App.
- Weiche Layoutwechsel ohne hörbare Blende (Überblendung zwischen altem und neuem Layout).

## Phase 3 · Steuerung und Formate (0.3)

- OSC-Fernsteuerung (Positionen, Pegel, Szenen), kompatibel zu gängigen ADM-OSC-Adressen
- Ambisonics-Eingänge (1. bis 3. Ordnung) und Ambisonics-Ausgang
- Kopfhörer-Vorschau mit gemessenen HRTF (SOFA-Dateien)
- Mehrere Räume / Hall-Zonen, Frühreflexionen abhängig von der Quellposition

## Phase 4 · Aufbau vor Ort (0.4)

- Raumeinmessung mit Messmikrofon: Laufzeit, Pegel und Entzerrung je Lautsprecher automatisch
- Szenen und Übergänge für Theater und Installationen
- Bass-Management mit mehreren Subwoofern

## Phase 5 · Veröffentlichung (1.0)

- Installer für macOS (signiert und notarisiert) und Windows (signiert), automatische Releases aus der CI
- Handbuch mit Beispielen für gängige Aufbauten
- Erprobung an echten Mehrkanal-Aufbauten, Rückmeldungen einarbeiten
