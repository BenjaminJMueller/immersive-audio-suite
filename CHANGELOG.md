# Änderungen

## Unveröffentlicht

- **3D-Kompressor**: Ein Kompressor für alle Lautsprecher gemeinsam im Schritt Ausgabe, vor dem Limiter. Er misst den lautesten Ausgang und senkt alle um denselben Betrag ab, so bleibt die räumliche Abbildung stehen. Threshold, Ratio, Attack, Release, Knee und Make-up, Gain Reduction als Anzeige.
- **Routing**: Im Schritt Ausgabe lässt sich jeder Lautsprecher auf einen beliebigen Ausgang des Audio-Interfaces legen oder ganz vom Interface trennen. Das Routing wird im Projekt gespeichert und gilt auch beim Einmessen.
- **Menüs mit runden Ecken**: alle Menüs und Auswahllisten, auch das Hilfemenü (?).
- **Raumklang Return**: Ruft die DAW das Plugin nicht auf (Spur nicht ausgewählt, kein Input-Monitoring, Wiedergabe gestoppt), sagt das Plugin das jetzt, statt still zu bleiben.
- **Master-Streifen**: zeigt die gesamte Gain Reduction von Kompressor und Limiter als „GR“.
- **Update repariert**: Der Update-Dialog zeigt jetzt die Änderungen als Liste, den Download mit Fortschritt und jeden Fehler. Darf Raumklang sich nicht selbst ersetzen (App-Verwaltung von macOS, Start aus dem Disk-Image), sagt es das vor dem Beenden und legt die neue Version in *Downloads/Raumklang-Update*. Das Installationsskript löscht die alte App nicht mehr, wenn das Verschieben scheitert, schreibt *Update.log* und meldet beim nächsten Start, ob es geklappt hat.
- **Plugins ohne Terminal**: Auf dem Mac stecken die Plugins signiert in Raumklang.app. Die App installiert sie beim ersten Start jeder Version selbst (AU und VST3 in ~/Library/Audio/Plug-Ins), über *Hilfe → Plugins installieren* jederzeit neu. *Plugins installieren.command* entfällt.
- **Dateinamen mit Version**: Raumklang-0.1.N-mac.dmg, Raumklang-0.1.N-mac.zip und Raumklang-0.1.N-Windows.zip. Die Release-Notizen und der Update-Dialog nehmen die Punkte aus dem Pull Request statt „Merge pull request …“.
- **Systemschrift**: Auf macOS und Windows nutzt die Oberfläche die Systemschrift (San Francisco, Segoe UI), Inter nur noch unter Linux.
- **Hilfemenü (?)**: Versionsnummer als Überschrift, Untermenüs für Darstellung und Sprache, Menüs im Stil der App.
- **Legende**: *Signalverteilung* statt *Schallenergie*.
- **Einmessen**: *Zurück* oben links statt *Beenden* unten. Der Ablauf steht als nummerierte Schritte direkt auf der Seite. *Mit dem iPhone einmessen* öffnet eine eigene Seite, die die Messung Schritt für Schritt erklärt.
- **DAW-Spuren**: Pegel, Breite und Hall-Send lassen sich in der App einstellen. Das Plugin übernimmt die Werte, die DAW nimmt sie als Automation auf. Dafür gibt es einen zweiten kleinen Shared-Memory-Block, ältere Plugins laufen weiter.
- **Pause in der DAW**: Hält die DAW an, blendet die App den Rest im Puffer aus statt ihn abzuschneiden. Das Plugin glättet den Sprung auf Stille beim Anhalten.
- **Limiter**: Ceiling und Release einstellbar, Gain Reduction als Anzeige im Ausgabe-Schritt und im Master-Streifen.
- **Szenen**: Beim Überblenden zwischen Räumen gleiten Größe und Pre-Delay des Halls, statt in der Verzögerungsleitung zu springen und zu knacken. Die Achsen der Raum-Diagramme gleiten mit. Ein (i) neben *Szenen* erklärt, was eine Szene speichert.
- **Quellen in der Seitenleiste**: über ⋯ oder Rechtsklick umbenennen, duplizieren und entfernen, Entfernen auch mit der Entf-Taste.
- **Hall in der Visualisierung**: Eine Wolke in der Farbe des Halls um jeden Lautsprecher, der Hall bekommt. Sie folgt dem Pegel, der in den Hall geht, und klingt mit der eingestellten Nachhallzeit aus.
- **Neue Oberfläche**: eine gemeinsame Raumansicht in App und Plugins, wahlweise von oben, von vorne, beides nebeneinander oder in 3D. Weniger Text, Hinweise nur noch, wenn etwas nicht stimmt.
- **Raum**: Nachhallzeit über die Frequenz als Kurve mit ziehbaren Punkten für Tiefen, Mitten und Höhen, darunter der Zeitverlauf mit Direktschall, frühen Reflexionen, Cluster und Nachhall. Raumtypen Studio, Kammer, Saal, Kirche und Platte. Der Hall hat dafür eine eigene Nachhallzeit für die Tiefen und getrennt regelbare frühe Reflexionen, Cluster und Nachhall bekommen.
- **Raumklang Quelle**: Raumansicht mit den anderen DAW-Spuren, Name und Farbe der DAW-Spur, „Hall-Send“ statt Hall-Send, selten gebrauchte Einstellungen unter *Erweitert*. Spielt das Plugin ohne App, rechnet es Laufzeit und Pegel der Lautsprecher selbst ein.
- **Raumklang Return**: Raumansicht mit allen Spuren und Pegel je Lautsprecher.
- **Raumklang Raum entfällt**: Den Raum gibt es nur noch einmal, in der App und im Hall-Send jeder Quelle.
- **Ausgabe**: Der Limiter wird erklärt.
- **DAW-Spuren in der App bewegen**: Ziehen in der Raumansicht oder die Regler im Schritt Quellen schicken die Position an das Plugin zurück, die DAW nimmt sie als Automation auf.
- **Szenen**: den ganzen Mix speichern und mit Überblendzeit abrufen, Leiste unter der Raumansicht mit Vorherige, Nächste, Aktualisieren und Neue Szene. Positionen blenden auf dem kürzesten Bogen über.
- **Neue Seitenleiste**: M und S direkt in jeder Quellenzeile, unten fest ein Master-Streifen mit Master-Pegel und Pegelanzeige. Abtastrate und Latenz stehen unter dem Projektnamen.
- **Kopfhörer**: Das Kopfmodell bekommt Ohrmuschel-Echos und eine Höhenabsenkung von hinten, damit vorne, hinten, oben und unten unterscheidbar werden. Neuer Modus *Binaural*: die Quellen erklingen frei um den Kopf, unabhängig von den Lautsprechern.
- **Schallteilchen**: Zwischen jeder spielenden Quelle und ihren Lautsprechern leuchten viele kleine Teilchen. Ihre Dichte zeigt, wie viel Energie jeder Lautsprecher von der Quelle bekommt; sie funkeln an Ort und Stelle, in der Draufsicht, von vorne und in 3D.
- **Abstrahlverhalten**: Richtwirkung und Ausrichtung je Quelle. Abgewandt klingt eine gerichtete Quelle leiser und dumpfer, der Hall-Send bleibt. Dazu ein Doppler-Effekt für bewegte Quellen.
- **Einmessen mit Mikrofon**: Gleitton über jeden Lautsprecher, Laufzeit und Pegel angleichen, mit 4 bis 12 Messpunkten auch die Positionen (Multilateration, Verzögerung des Interfaces wird herausgerechnet). Hinweis auf vertauschte Kabel, Vorschau mit gestrichelter alter Position, Rückgängig.
- **iPhone-App „Raumklang Messen“**: Lautsprecher mit LiDAR antippen, Position und Ausrichtung als Datei an Raumklang. Laden oder auf das Fenster ziehen.
- **Testraum**: `RAUMKLANG_TESTRAUM=1` startet die App mit einem simulierten Raum und Mikrofon, zum Ausprobieren ohne Aufbau.
- **Schallteilchen überarbeitet**: Sie erscheinen nur, solange eine Quelle Signal hat, folgen dem Pegel, sind weicher und ohne festes Muster. In der Legende heißt der Eintrag jetzt *Schallenergie*.
- **Schärfere Schrift**: Die Oberfläche nutzt jetzt die eingebettete Schrift Inter statt der Standardschrift von JUCE (auf dem Mac Lucida Grande), ohne die Stammverstärkung der macOS-Textglättung. Text wirkt dadurch auf Retina-Displays dünner und klarer. Leere Hinweiskarten passen ihre Höhe dem Text an, „vorne“ verschwindet in der geteilten Ansicht nicht mehr hinter der Legende.
- **Mikrofon-Abfrage auf dem Mac**: App und Plugins werden jetzt vollständig (ad hoc) signiert, mit Hardened Runtime und der Berechtigung für Audio-Eingänge. Bisher konnte macOS die Erlaubnis nicht speichern und fragte nach jedem Neustart des Audiogeräts, etwa nach dem Ruhezustand, erneut. Nach einem Update fragt macOS einmal, bis die App eine Developer ID hat.
- **Menüleiste auf dem Mac**: Ablage (Neues Projekt, Demo, Öffnen, Sichern unter, iPhone-Messung laden), Bearbeiten (Rückgängig, Wiederholen), Darstellung (Bereiche ⌘1 bis ⌘4, Ansichten, Dunkel/Hell, Sprache) und Hilfe; *Nach Updates suchen …* im Raumklang-Menü. Neues Projekt und Demo fragen nach, wenn das aktuelle Projekt in keiner Datei gesichert ist.
- **3D-Ansicht**: Lautsprecher erscheinen als Boxen mit Abstrahlkegel, die Ausrichtung ist auf einen Blick zu sehen.
- **Lautsprecher in der Seitenleiste**: über ⋯ oder Rechtsklick umbenennen, duplizieren, als Subwoofer markieren, abschalten, entfernen; *+ Lautsprecher hinzufügen* darunter.
- **Einmessen**: eine ruhige Liste mit drei Wegen (Mikrofon, iPhone-Messung, Laufzeit und Pegel ausgleichen) statt vieler Knöpfe.
- **Raumtyp** lässt sich auch rechts auswählen, die Pegelbalken in der Raumliste entfallen.
- **Hören über**: drei erklärte Optionen *Lautsprecher*, *Kopfhörer, Anlage* und *Kopfhörer, frei*.
- **Info-Knöpfe**: Jede Überschrift mit (i) öffnet beim Klick eine Erklärung, *Details und Hilfen* entfällt.
- **Einführung**: fünf Seiten beim ersten Start statt Schrittnummern und Hinweisen unter der Ansicht, über *Hilfe* (?) wieder aufrufbar, ohne das Projekt zu verlieren.
- **Neue Quellen** heißen „DAW-Spur 1, 2, …“, wenn die DAW keinen Spurnamen liefert, statt „void“.
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
