# Raumklang Messen (iPhone, iPad)

Misst mit LiDAR, wo die Lautsprecher hängen und wohin sie zeigen, und gibt eine Datei
(`.raumklang-messung`) an Raumklang weiter.

## Warum eine App und keine Webseite

Safari auf iPhone und iPad bietet kein WebXR. Eine Webseite kommt deshalb weder an den
LiDAR-Sensor noch an die Raumerfassung von ARKit, sie sieht nur ein flaches Kamerabild. Ohne
Tiefe lässt sich ein Punkt im Raum nicht zentimetergenau treffen. Eine kleine native App kann es.

## Ablauf

1. **Hörplatz**: aufs Mittelpunkt-Kreuz am Boden zielen und *Setzen* tippen. Die Ohrhöhe lässt
   sich einstellen (Standard 1,20 m).
2. **Vorne**: auf die Richtung zielen, die in Raumklang „vorne“ ist, und *Setzen* tippen.
3. **Lautsprecher**: auf die Front jedes Lautsprechers zielen und *Setzen* tippen, in der
   Reihenfolge der Ausgänge (1, 2, 3 …). Die Ausrichtung kommt aus der Fläche der Front.
4. *Fertig* teilt die Datei per AirDrop, Mail oder Dateien-App. In Raumklang
   *iPhone-Messung laden* oder die Datei auf das Fenster ziehen.

Braucht ein iPhone oder iPad mit LiDAR (iPhone 12 Pro oder neuer in der Pro-Reihe, iPad Pro ab 2020).

## Installieren

Es gibt die App (noch) nicht im App Store. Zum Selbstbauen auf einem Mac:

```sh
brew install xcodegen
cd ios/RaumklangMessen
xcodegen
open RaumklangMessen.xcodeproj
```

In Xcode unter *Signing & Capabilities* das eigene Team wählen (eine kostenlose Apple-ID reicht),
das iPhone anschließen und *Run*. Mit einer kostenlosen Apple-ID läuft die App 7 Tage, dann
einfach erneut aufspielen.

`SurveyKit` enthält Dateiformat und Koordinaten ohne ARKit und lässt sich auf dem Mac testen:
`swift test --package-path ios/SurveyKit`.

## Dateiformat

JSON, `"format": "raumklang-survey"`, `"version": 1`, `"frame": "raumklang"`: x nach rechts,
y nach vorne, z nach oben, Ursprung auf Ohrhöhe am Hörplatz, Meter. Je Lautsprecher `name`,
`position` [x, y, z], `aim` (Einheitsvektor) und `confidence` (0 bis 1).
