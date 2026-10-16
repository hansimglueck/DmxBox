# DmxBox – Handbuch

Dieses Handbuch beschreibt die Bedienung der DmxBox über den Browser. Begriffe aus der Oberfläche
stehen so da, wie sie auf dem Bildschirm erscheinen (englisch).

**Inhalt**

1. [Erste Schritte](#1-erste-schritte)
2. [Die Startseite](#2-die-startseite)
3. [Fixtures patchen](#3-fixtures-patchen)
4. [Definitionen](#4-definitionen)
5. [Fader bedienen](#5-fader-bedienen)
6. [Presets](#6-presets)
7. [Cues und Sequenzen](#7-cues-und-sequenzen)
8. [Mehrere Boxen (LoRa)](#8-mehrere-boxen-lora)
9. [Zeitsteuerung](#9-zeitsteuerung)
10. [Netzwerk](#10-netzwerk)
11. [Firmware-Update](#11-firmware-update)
12. [Fehlersuche](#12-fehlersuche)

---

## 1. Erste Schritte

1. **Box einschalten.** Beim ersten Start hat die Box noch kein WLAN gespeichert und öffnet ein eigenes
   WLAN (Access Point). Der Name steht auf dem Display der Box, z.B. `dmxbox-de8c`.
2. **Mit dem WLAN der Box verbinden.** Das Passwort ist in der Firmware festgelegt. Wer die Box
   eingerichtet hat, kennt es.
3. **Oberfläche öffnen.** Meist öffnet sich die Seite von selbst (Captive Portal). Sonst im Browser
   `http://192.168.4.1` aufrufen.
4. **Box benennen.** Unter *Connectivity → Box* einen sprechenden Namen vergeben, z.B. „Bühne links“.
   Daraus werden Hostname und WLAN-Name (`buehne-links`). Die Box startet dafür neu.
5. **Optional ins WLAN bringen.** Unter *Connectivity → WiFi* ein Netz wählen. Danach ist die Box
   im Netz unter `http://<name>.local` erreichbar, bekommt die Uhrzeit aus dem Internet und kann
   Firmware-Updates laden.

## 2. Die Startseite

Von oben nach unten:

- **Statusleiste:** WLAN (Station mit Signalstärke oder Access Point mit Anzahl der Clients), LoRa
  (Anzahl der Boxen im Verbund) und Uhrzeit. Daneben der Zustand der Zeitsteuerung. Ein Tipp darauf
  öffnet *Connectivity* bzw. *Schedule*.
- **Playback** (blauer Streifen links): Sequenz wählen und abspielen, siehe [Kapitel 7](#7-cues-und-sequenzen).
- **Fixtures:** eine Kachel pro Fixture, auch für die Fixtures der anderen Boxen. Fixtures dieser Box
  haben den Boxnamen blau hervorgehoben.

Jede Kachel zeigt die Feature-Gruppen des Fixtures als Chips. Ein Chip zeigt das aktive Preset oder
die aktuellen Werte, bei Dimmer zusätzlich einen Pegel und bei Color eine Farbvorschau. Ein Tipp auf
einen Chip öffnet das Fixture direkt in dieser Gruppe.

**Gruppenfilter:** Mit den Buchstaben **D P G C B F C** neben „Fixtures“ blendest du Gruppen ein und
aus: Dimmer, Position, Gobo, Color, Beam, Focus, Control. Wer meist nur Dimmer, Position und Farbe
braucht, lässt nur **D P C** an. Der Browser merkt sich die Auswahl.

Fehlt einem Fixture eine Gruppe, steht an ihrer Stelle ein gestrichelter Platzhalter. So steht jede
Gruppe auf allen Kacheln an derselben Stelle, wie die Spalten im grandMA2-Fixture-Sheet.

Eine ausgegraute Kachel gehört zu einer Box, die gerade nicht erreichbar ist.

## 3. Fixtures patchen

**Neues Fixture:** Auf der Startseite *+ New Fixture* wählen.

| Feld | Bedeutung |
|---|---|
| Box | Auf welcher Box das Fixture angeschlossen ist. Auch andere Boxen im Verbund sind wählbar. |
| Name | Frei wählbar, z.B. „Spot links“. |
| Start address | DMX-Startadresse (1–512). Vorgeschlagen wird die erste freie Adresse. |
| Type | Eine Definition oder *Generic* (nur eine Anzahl von Kanälen ohne Bedeutung). |
| Channel count | Nur bei Generic: Anzahl der Kanäle. |

Überschneidet sich ein Fixture mit einem anderen, steht auf der Kachel „overlaps another fixture“.

**Ändern oder löschen:** Auf der Fixture-Seite *Edit*. Dort gibt es auch *Delete fixture*.

**Generic-Fixtures:** Über *Channel names* bekommen die Kanäle Namen. Heißt ein Kanal z.B. „Pan“ und ein
anderer „Pan fine“, werden beide automatisch zu einem 16-Bit-Fader zusammengefasst.

## 4. Definitionen

Eine Definition beschreibt einen Gerätetyp: Kanäle, deren Feature-Gruppen und benannte Wertebereiche.
Zu finden unter *Definitions* in der Fußzeile.

**Importieren** (*Import*): Mehrere Dateien auf einmal sind möglich.
- grandMA2-Fixture-Types (XML)
- Open Fixture Library (JSON)
- labX-Fixture-Dateien
- DmxBox-Export

Hat ein Gerät mehrere Modi, wird pro Modus eine eigene Definition angelegt.

**Selbst anlegen** (*+ New definition*): Eine Zeile pro Kanal:

```
Name | Type | Wertebereiche
Dimmer | Dimmer
Pan | Position
Pan fine
Wheel | Color/wheel | 0-9 Open; 10-19 Red; 20-29 Blue; 255 Reset
```

- Typ und Wertebereiche sind optional. Wertebereiche werden mit `;` getrennt.
- Steht als Typ ein Gruppenname (Dimmer, Position, Gobo, Color, Beam, Focus, Control), legt er die
  Feature-Gruppe fest, auch mit Zusatz wie `Color/wheel`. Sonst errät die Box die Gruppe aus Typ und Name.
- „Pan fine“ wird automatisch mit „Pan“ zum 16-Bit-Kanal verbunden. Bei anderen Namen hilft der Typ
  `fine:Pan`.

Im Verbund verteilen sich Definitionen automatisch auf alle Boxen. Bei Änderungen gewinnt die neueste
Fassung.

## 5. Fader bedienen

Die Fixture-Seite zeigt oben den Pfad (z.B. *Home › Spot links*) mit Zurück-Pfeil. Darunter folgen das
Mini-Playback, der Name mit *Edit* und die Fader.

- **Tabs** (*All*, *Dimmer*, *Position* …) filtern die Fader nach Gruppe. Unter dem Gruppennamen steht
  das aktive Preset. Am Handy stehen die Tabs senkrecht.
- **Fader bewegen:** Auf dem Fader nach oben oder unten ziehen. Am Computer geht auch das Mausrad. Der
  Fader ist ein Drehgeber: Es zählt die Bewegung, nicht die Position des Fingers.
- **Empfindlichkeit:** Die Knöpfe unter dem Fader legen fest, wie stark eine Bewegung wirkt
  (8 Bit: 1 / 5 / 10, 16 Bit: 1 / 10 / 100).
- **Wertebereiche:** Hat der Kanal benannte Bereiche (z.B. Farben auf einem Farbrad), wählt die Auswahl
  darunter einen Bereich direkt an. Sie zeigt auch, in welchem Bereich der Kanal gerade steht.
- **Gelber Rahmen:** Der Fader wurde von Hand verändert, seit der aktuelle Cue aufgerufen wurde.
  **clear** setzt nur diesen Fader auf den Wert des Cues zurück.
- Ein Tipp auf den Kanalnamen öffnet die Detailseite des Kanals mit DMX-Adresse und Direkteingabe.

Das Mini-Playback bleibt beim Scrollen oben stehen. So ist *Go* auch tief unten bei den Fadern
erreichbar.

## 6. Presets

Jede Feature-Gruppe hat eigene Presets, wie bei grandMA2. Sie stehen unter den Fadern, sobald ein
Gruppen-Tab gewählt ist.

- **Position und Focus** gelten **pro Fixture**. „Bühnenmitte“ ist für jeden Scheinwerfer ein anderer
  Wert.
- **Alle anderen Gruppen** gelten **pro Gerätetyp**. „Rot“ ist für alle Geräte desselben Typs gleich.

| Aktion | So geht's |
|---|---|
| Aufrufen | Preset antippen. Es ist dann blau umrandet. |
| Neu anlegen | Werte einstellen, *+ Preset*, Namen eingeben. |
| Überschreiben, umbenennen, löschen | *Edit presets*, dann *Store*, *Rename* oder *Delete*. |

Ein **gestrichelt** umrandetes Preset hat für dieses Fixture noch keine Werte. Antippen bietet an, die
aktuellen Werte zu speichern.

Ein aktives Preset bleibt mit dem Fixture verknüpft: Wird das Preset überschrieben, ändern sich alle
Fixtures mit, die es gerade verwenden. Bewegt man einen Fader der Gruppe von Hand, löst sich die
Verknüpfung.

Unter den eigenen Presets stehen die **Wertebereiche aus der Definition** als zusätzliche Knöpfe.

## 7. Cues und Sequenzen

Eine **Sequenz** ist eine Liste von **Cues** und läuft immer im Kreis. Jeder Cue hat einen Namen, eine
**Fade-Zeit** (Überblendung) und eine **Dauer** (vom Start des Cues bis zum Start des nächsten).
Sequenzen gelten für alle Boxen im Verbund, und jede Box speichert ihre eigenen Kanäle darin.

Gespeichert wird nur, was sich gegenüber dem vorherigen Cue geändert hat (Tracking). Ein Cue zeigt also
alles, was bis dahin aufgebaut wurde.

### Playback (Startseite)

| Element | Funktion |
|---|---|
| Auswahl oben | Sequenz wählen oder *+ New sequence…* |
| Play / Hold | Sequenz automatisch ablaufen lassen oder anhalten |
| Go / Back | nächster bzw. vorheriger Cue mit Fade |
| ⏭ / ⏮ | Skip: nächster bzw. vorheriger Cue ohne Fade |
| Save cue | manuelle Werte als neuen Cue **nach** dem aktuellen speichern |
| Update cue | manuelle Werte **in** den aktuellen Cue speichern |
| Clear | alle manuellen Werte verwerfen, zurück zum gespeicherten Cue |

Das **Mini-Playback** auf den Fixture-Seiten hat dieselben Funktionen als Symbole: ⏮ ‹ › ⏭ Play/Hold,
dann + (Save cue), ✓ (Update cue) und ✕ (Clear).

### Manuelle Werte

Sobald du einen Fader bewegst oder ein Preset aufrufst:
- Der Kanal wird markiert (gelber Rahmen). Im Playback steht die Anzahl der manuellen Werte.
- Läuft die Sequenz gerade, geht sie automatisch auf *Hold*.
- Mit *Save cue* oder *Update cue* wandern die manuellen Werte in den Cue.
- Wer zu einem anderen Cue wechselt, verwirft die manuellen Werte. Die Box fragt vorher nach.
- Einzelne Fader setzt **clear** am Fader zurück.

### Show-Seite

*Show* in der Fußzeile:
- Sequenzen umbenennen und löschen
- Cues bearbeiten: Name, Fade, Dauer, Reihenfolge, Löschen
- **At startup:** Welche Sequenz nach dem Einschalten läuft, die zuletzt aktive oder eine feste

## 8. Mehrere Boxen (LoRa)

Boxen mit LoRa (Heltec V4) finden sich per Funk und bilden einen Verbund. Ein gemeinsames WLAN ist dafür
nicht nötig. Die Reichweite ist deutlich größer als bei WLAN.

**Einrichten:** Unter *Connectivity → LoRa link* auf allen Boxen dasselbe **Link password** eintragen.
Boxen mit anderem Passwort sehen sich nicht. Ist noch das Standardpasswort gesetzt, warnt die Seite.

**Was der Verbund kann:**
- Die Fixtures aller Boxen erscheinen auf jeder Box und lassen sich dort bedienen und patchen.
- Playback, Sequenzen und Cues laufen auf allen Boxen synchron.
- Definitionen werden verteilt.

**Box-Tabelle** unter *Connectivity*:
- Status jeder Box (online/offline) und Signalstärke
- Firmware-Version
- *Ping all boxes* misst die Antwortzeit

**Gut zu wissen:**
- Funk ist langsam. Änderungen an anderen Boxen brauchen einen Moment. Bis die Box bestätigt, dreht
  sich ein Kreisel.
- Pro Stunde darf eine Box nur einen kleinen Teil der Zeit senden (*Airtime*, Budget 8 %). Bei sehr
  vielen schnellen Änderungen wird gebremst.
- Ist eine Box offline, sind ihre Fixtures ausgegraut, und Änderungen werden nicht übertragen.

## 9. Zeitsteuerung

Unter *Schedule* lässt sich für jeden Wochentag eine Start- und Endzeit festlegen. Außerhalb dieser
Zeiten sendet die Box auf allen Kanälen 0. Liegt die Endzeit vor der Startzeit, läuft der Zeitraum über
Mitternacht. Die Zeitsteuerung braucht die richtige Uhrzeit, also WLAN mit Internet. Ist sie
ausgeschaltet, ist DMX immer an.

## 10. Netzwerk

*Connectivity → WiFi*:

- **Station:** Die Box ist mit einem WLAN verbunden. Sie ist unter ihrer IP-Adresse oder
  `http://<name>.local` erreichbar.
- **Access point:** Die Box hat ein eigenes WLAN (`http://192.168.4.1`). Das passiert, wenn kein Netz
  gespeichert ist, das gespeicherte Netz nicht erreichbar ist oder die Verbindung abreißt.

Mit *Scan networks* oder der Eingabe von Netzname und Passwort verbindet sich die Box mit einem Netz und
startet neu.

> `.local`-Adressen brauchen auf manchen Geräten einige Sekunden oder gehen gar nicht, vor allem unter
> Android. Dann die IP-Adresse verwenden. Sie steht auf dem Display und unter *Connectivity*.

## 11. Firmware-Update

Nur Heltec-Boxen können sich selbst aktualisieren. Unter *Connectivity → Firmware*:

- **Installed:** die installierte Version
- **Newest release:** die neueste veröffentlichte Version. Die Box prüft von selbst nach dem Start und
  alle 6 Stunden, sofern sie im WLAN mit Internet ist. *Check now* prüft sofort.
- **Install …:** erscheint, wenn eine neuere Version da ist. Die Box lädt das Update, startet neu, und
  DMX setzt für ein paar Sekunden aus. Die Seite zeigt den Fortschritt.

**Andere Boxen aktualisieren:** In der Box-Tabelle unter *LoRa link* steht bei jeder Box ihre Version.
Hat eine Box Internet und gibt es eine neuere Version, erscheint dort *Update* (bei mehreren auch
*Update all*). Den Download macht jede Box selbst, sie braucht also eigenes Internet. Steht „no internet“
dabei, muss sie erst in ein WLAN.

Schlägt ein Update fehl, bleibt die bisherige Firmware aktiv. Boxen mit älterer Firmware ohne
Update-Funktion erscheinen als „firmware unknown (older)“. Sie brauchen einmal ein Update per USB.

## 12. Fehlersuche

| Problem | Abhilfe |
|---|---|
| Seite lädt nicht | Ist das Gerät im richtigen WLAN? Statt `.local` die IP-Adresse versuchen. |
| Rote Leiste „Connection lost“ | Die Box ist nicht erreichbar oder startet gerade neu. Die Seite verbindet sich selbst wieder. |
| Box erscheint nicht im Verbund | Gleiches Link-Passwort? LoRa eingeschaltet? Mit *Ping all boxes* testen. |
| „fixture list not received yet“ | Die Box ist gerade erst online gekommen, die Liste kommt per Funk nach. |
| Fixture reagiert nicht | Adresse und Kanalanzahl prüfen. Überschneidung mit einem anderen Fixture? Zeitsteuerung aktiv? |
| Werte springen nach Cue-Wechsel zurück | Manuelle Werte vorher mit *Save cue* oder *Update cue* speichern. |
| Keine Uhrzeit | Die Box braucht WLAN mit Internet. |
