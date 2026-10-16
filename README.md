# DmxBox

DMX-Steuerbox auf ESP32-Basis mit Weboberfläche. Jede Box gibt ein DMX-Universum (512 Kanäle) aus und wird
per Browser bedient: Handy, Tablet oder Laptop. Mehrere Boxen verbinden sich über **LoRa** zu einem Verbund
und lassen sich von jeder Box aus gemeinsam steuern, auch ohne gemeinsames WLAN.

Die Bedienung ist im **[Handbuch](docs/MANUAL.md)** beschrieben. Diese Datei richtet sich an alle, die
die Firmware bauen, flashen oder weiterentwickeln.

## Funktionen

- **Fixtures und Definitionen:** Fixtures mit Startadresse patchen, Definitionen selbst anlegen oder
  importieren (grandMA2-XML, Open Fixture Library, labX, DmxBox-JSON). 16-Bit-Kanäle (coarse/fine) werden
  erkannt.
- **Fader** nach grandMA2-Vorbild: Kanäle nach Feature-Gruppen (Dimmer, Position, Gobo, Color, Beam,
  Focus, Control), benannte Wertebereiche, Presets pro Gruppe.
- **Cues und Sequenzen:** Sequenzen mit Fade- und Laufzeiten, Go/Back/Skip, Play/Hold, Tracking.
  Manuelle Änderungen werden markiert und lassen sich als Cue speichern oder einzeln zurücksetzen.
- **LoRa-Verbund** (Heltec V4): Boxen sehen und steuern die Fixtures der anderen, Show und Playback
  laufen synchron, Definitionen werden verteilt.
- **Zeitsteuerung:** DMX-Ausgabe nur zu festgelegten Zeiten pro Wochentag (NTP-Zeit im WLAN).
- **WLAN:** verbindet sich mit einem gespeicherten Netz, sonst eigener Access Point mit Captive Portal.
- **Firmware-Updates:** Heltec-Boxen mit Internet holen neue Releases von GitHub, auch per LoRa
  von einer anderen Box angestoßen. Zusätzlich geht OTA per `espota`.

## Hardware

| Env (`platformio.ini`) | Board | Besonderheiten |
|---|---|---|
| `heltec-v4` | Heltec WiFi LoRa 32 V4 (ESP32-S3, 16 MB Flash) | OLED, LoRa 869,525 MHz, Online-Update |
| `esp32dev` | ESP32 DevKit | ohne LoRa und Online-Update (App-Partition zu klein) |
| `esp32-c3-devkitm-1` | ESP32-C3 | ohne LoRa und Online-Update |
| `esp32dev-recovery` | ESP32 DevKit | Minimal-Firmware: nur AP und OTA, zum Retten |

DMX läuft über einen RS-485-Transceiver. Die Pins kommen pro Env aus den Build-Flags `DMX_SERIAL_OUTPUT_PIN`,
`DMX_SERIAL_INPUT_PIN` und `DMX_DIRECTION_PIN`. Beim Heltec V4 sind das 48, 47 und 45.

> **Achtung ESP32-S3:** GPIO26 ist dort der Chip-Select des PSRAM. Diesen Pin nie für DMX oder anderes
> verwenden, die Box stürzt sonst ab.

## Bauen und flashen

Voraussetzung ist [PlatformIO](https://platformio.org/) (CLI oder VS-Code-Erweiterung).

```sh
pio run -e heltec-v4                     # bauen
pio run -e heltec-v4 -t upload           # per USB flashen
pio device monitor -e heltec-v4          # serielles Log (115200 Baud)
```

Sind mehrere Boxen angeschlossen, den Port angeben: `--upload-port /dev/cu.usbmodem101`.

Die Build-Flags pro Env legen den WLAN-Namen und das Passwort des Access Points fest (`WIFI_SSID`,
`WIFI_PASSWORD`). Das Passwort gilt auch für OTA. `HOSTNAME_MAC_SUFFIX` hängt die letzten Stellen der
MAC-Adresse an den Hostnamen an. Benennt man die Box in der Oberfläche um, gilt danach der neue Name
als Hostname und als AP-Name.

### OTA per espota

Im `esp32dev`-Env sind die espota-Einstellungen auskommentiert vorbereitet. Hostname: `<box>.local`,
Port 3232, Passwort aus `WIFI_PASSWORD`.

### Partitionen

- `heltec-v4` nutzt `large_spiffs_16MB.csv`: zwei App-Slots mit je 4,5 MB plus LittleFS.
- `esp32dev` und C3 nutzen die Standard-Tabelle (1,3 MB App). Die Firmware liegt bei etwa 90 %. Für
  Online-Updates reicht der Platz nicht, weil der TLS-Client rund 180 KB braucht.

Fixtures, Definitionen, Presets, Cues und Einstellungen liegen im LittleFS. Ein normales Firmware-Update
lässt sie stehen. Eine geänderte Partitionstabelle löscht sie.

## Versionen und Releases

`version.py` setzt beim Bauen `FW_VERSION` aus `git describe` und `FW_ENV` aus dem Env-Namen.

- Steht HEAD auf einem Tag `v1.2.3`, heißt die Version `1.2.3`.
- Liegen Commits dazwischen, heißt sie z.B. `1.2.3-4-gabc1234-dirty`. Für den Versionsvergleich zählt
  der letzte Tag.
- Ohne Tag heißt sie `0.0.0-g<hash>`.

**Release erstellen:**

```sh
git tag v1.2.3
git push origin v1.2.3
```

Der Workflow [.github/workflows/release.yml](.github/workflows/release.yml) baut daraufhin alle drei
Firmware-Envs und legt ein GitHub-Release an:

- `firmware-<env>.bin` für jedes Board
- `latest.json` mit Version sowie URL, Größe und MD5 pro Board

Heltec-Boxen lesen `releases/latest/download/latest.json`. Sie prüfen 30 s nach dem Start und dann alle
6 Stunden. Installiert wird nur per Knopf, entweder auf der Box selbst oder per LoRa von einer anderen
Box aus. Eine fehlerhafte Datei wird abgelehnt (MD5). Die alte Firmware bleibt dann aktiv.

## Aufbau des Codes

| Datei | Inhalt |
|---|---|
| `src/main.cpp` | Setup, Loop, WLAN/AP, DMX-Ausgabe, Zeitsteuerung |
| `src/fixtures.*` | Fixtures, Definitionen, Import, LittleFS |
| `src/presets.*` | Presets pro Feature-Gruppe und aktive Preset-Referenzen |
| `src/cues.*` | Sequenzen, Cues, Playback, Markierungen manueller Änderungen |
| `src/sync.*`, `src/lora_link.*`, `src/link.h` | LoRa-Verbund: Protokoll, Transport, Airtime-Budget |
| `src/firmware.*`, `src/fw_certs.h` | Online-Update von GitHub (nur mit `HAS_FW_UPDATE`) |
| `src/fixture_web.*` | Seitenrahmen, CSS, Startseite, Fixture-, Kanal- und Definitionsseiten |
| `src/fader_ui.*` | Fader-Raster, Gruppen-Tabs, Preset-Leisten |
| `src/cue_web.*` | Playback-Panel, Mini-Playback, Show-Seite |
| `src/sync_web.*` | LoRa-Bereich, Fixtures anderer Boxen |
| `src/system_web.cpp` | Connectivity, WLAN, Zeitplan, Status-API, Firmware |
| `src/display.*` | OLED-Statusanzeige (Heltec) |

Die Oberfläche wird vollständig von der Firmware ausgeliefert, `data/` wird nicht verwendet. CSS und
Skripte liegen als Strings im Code und werden mit Versions-Hash ausgeliefert, damit Browser nach einem
Update keine alten Dateien verwenden.

### LoRa-Protokoll

- Pakete sind per HMAC signiert. Der Schlüssel ist das Link-Passwort, das auf allen Boxen gleich sein muss.
- `PROTO_VER` in `sync.cpp` nur erhöhen, wenn alte und neue Boxen sich wirklich nicht mehr verstehen
  dürfen: Pakete mit anderer Version werden verworfen.
- Neue Felder hinten an bestehende Pakete anhängen (wie die Firmware-Felder im Beacon). Ältere Boxen
  ignorieren sie dann einfach.
- Das Sendebudget liegt bei 8 % Airtime pro Stunde, unter der 10-%-Grenze des Bandes.
