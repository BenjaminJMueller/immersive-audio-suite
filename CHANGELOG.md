# Änderungen

## Unveröffentlicht

- **Einmessen mit Mikrofon**: Gleitton über jeden Lautsprecher, Laufzeit und Pegel angleichen, mit 4 bis 12 Messpunkten auch die Positionen (Multilateration, Verzögerung des Interfaces wird herausgerechnet). Hinweis auf vertauschte Kabel, Vorschau mit gestrichelter alter Position, Rückgängig.
- **iPhone-App „Raumklang Messen“**: Lautsprecher mit LiDAR antippen, Position und Ausrichtung als Datei an Raumklang. Laden oder auf das Fenster ziehen.
- **Testraum**: `RAUMKLANG_TESTRAUM=1` startet die App mit einem simulierten Raum und Mikrofon, zum Ausprobieren ohne Aufbau.
- **Logic Pro**: Die AU-Plugins werden in der CI mit `auval` geprüft.

## 0.1.0 – 2026-11-18

Erste Version.

- **Lautsprecher**: Vorlagen Stereo, Quadrofonie, 5.1, 7.1.4, Ring, Kuppel, Würfel und Donut, freies Setzen und Ziehen, Ausrichtung, Pegel und Verzögerung je Lautsprecher, *Abstände ausgleichen*, Prüfung des Layouts in Klartext.
- **Verfahren**: VBAP 2D/3D mit virtuellen Lautsprechern und Breite, DBAP mit Richtwirkung (Donut), Ambisonics 3. Ordnung mit AllRAD, automatische Auswahl.
- **Quellen**: Eingänge des Audio-Interfaces oder Testsignale, Position, Pegel, Breite, Hallanteil, Entfernungswirkung, *Kreisen*, Stumm und Solo.
- **Raum**: Hall (FDN mit 16 Leitungen) mit Vorlagen und Reglern.
- **Ausgabe**: direkt auf das Audiogerät, Kopfhörer-Vorschau, Lautsprechertest, Schutzbegrenzer.
- **DAW**: Plugins *Raumklang Quelle* und *Raumklang Raum* (VST3, AU), gemeinsames Lautsprecher-Setup mit der App.
- **Oberfläche**: vier Schritte, Einführung mit Demo beim ersten Start, Rückgängig, automatisches Sichern, Deutsch und Englisch.
- **Tests**: Unit-Tests, Echtzeit-Sicherheit, Stresstests, Fuzzing, Sanitizer, Plugin-Host-Stresstest und pluginval in der CI auf macOS, Windows und Linux.
