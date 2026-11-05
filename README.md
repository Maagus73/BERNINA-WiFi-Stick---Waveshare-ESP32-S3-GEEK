# BERNINA-WiFi-Stick - Waveshare-ESP32-S3-GEEK 


[Deutsch](#deutsch) · [English](#english)

## Deutsch

Ein dedizierter WLAN-Stick auf Basis des **Waveshare ESP32-S3-GEEK**, der Dateien aus einem Synology-Verzeichnis auf eine microSD spiegelt und diese über USB-Massenspeicher bereitstellt. Die NAS-Quelle bleibt schreibgeschützt. Ziel sind Stickdateien; die Synchronisation filtert nicht nach Dateiendung.

Dieses unabhängige Hobbyprojekt ist kein offizielles BERNINA-Produkt und steht in keiner Verbindung zum Hersteller. Kompatibilität mit einem konkreten Maschinenmodell muss separat geprüft werden. Die Entwicklung erfolgte durch mich mit Unterstützung von **OpenAI ChatGPT/Codex** bei Implementierung, Fehlersuche, Optimierung und Dokumentation.

**Nichtkommerzielle Nutzung und Anpassung im Rahmen der PolyForm Noncommercial 1.0.0 sind ausdrücklich willkommen. Für diese Lösung gibt es keinen Support, keine zugesagte Wartung und keine garantierten Updates.**

**Aktueller öffentlicher Stand: 1.0.3.** Der erste Git-Commit darf mit dieser Version beginnen; frühere private Versionen sind nicht erforderlich. Die Hardwareabnahme von 1.0.3 steht noch aus.

**Kein WebDAV-Server erforderlich:** Der Stick verwendet den eigenen Python-Indexdienst in Docker über HTTPS. Das Synology-Paket „WebDAV Server“ und Port 5006 werden für diesen Aufbau nicht verwendet. Die internen Konfigurationsnamen `WEBDAV_*` bestehen aus Kompatibilitätsgründen fort und bezeichnen hier das Konto und die URL des Indexdienstes.

### Was das Projekt kann

- Automatischer Abgleich im einstellbaren Intervall, zusätzlich manuell über Webseite oder Home Assistant.
- Ab 1.0.3 automatische Wiederaufnahme nach vorübergehenden NAS-Ausfällen: konfigurierbare Wartezeiten, sichere Reparatur nach unterbrochenem Schreiben und weiterhin verbindliche APP-/USB-Sperren.
- NAS-seitiger SQLite-Index und Planer: unveränderte Generationen benötigen keinen erneuten SD-Dateivergleich. Neue Dateien, Inhaltsänderungen, Unterordner, Umbenennungen und Löschungen werden berücksichtigt.
- Kleine Änderungen als Delta; oberhalb des konfigurierten Schwellwertes vollständiger Neuaufbau. Der Erstaufbau kann bei grossen Beständen lange dauern; „sekundenschnell“ ist keine Zusage für grosse Downloads.
- Begrenzte HTTPS-Sammelübertragung mit SHA-256-Prüfung, internen DMA-Puffern und Wiederherstellungs-/Checkpointlogik.
- Technisch bestätigte APP-/USB-Übergabe: niemals gleichzeitiger Dateisystemzugriff. Der USB-Host erhält lesenden Zugriff; manuelle SD-Änderungen gehören nicht zum normalen Betrieb.
- Farbdisplay mit Fortschritt und bestehendem Matrix-Bildschirmschoner.
- Eine schlanke deutsche Webseite: Status, Bestand, Aktionen, Konfiguration, heutiges Ereignisprotokoll und Textdownload.
- Home-Assistant-Tasten, Status-/Verlaufssensoren, Intervallwert plus Minuten/Stunden und Link zur Stick-Webseite.
- Getrennte NAS-/SD-Ausschlussdateien, auch rekursiv, etwa für `@eaDir` und macOS-Metadaten.

### Aufbau

| Teil | Aufgabe |
|---|---|
| NAS-Quellordner | Massgebliche Nutzdaten, im Container nur lesbar |
| Docker-Indexdienst | Index, SHA-256, unveränderliche Pläne, Sammeltransfer, Ereignisarchiv |
| DSM Reverse Proxy | HTTPS auf 5443 → lokales HTTP 18080 → Container 8080 |
| ESP32 | Plan ausführen, SD schreiben, USB-Besitz regeln, Anzeige/Web/API |
| Home Assistant, optional | Bedienung, Intervallkonfiguration, Zustandsverlauf |

**Aufgebaut und getestet wurde die Lösung auf Synology mit Container Manager, DSM Reverse Proxy und eigenem HTTPS-Zertifikat.** Verwendet wird ausschliesslich der eigene Indexdienst. Ein NAS ist technisch nicht zwingend: derselbe Aufbau ist mit angepassten Pfaden, Rechten, DNS und HTTPS auch auf Windows/macOS möglich. Diese Varianten sind nicht abgenommen; Windows nutzt dafür einen Linux-Container. Details und Grenzen: [Plattformen](docs/02_PLATFORMEN.md).

### Einstieg und vollständige Anleitungen

1. [Schnellstart und Voraussetzungen](docs/01_QUICKSTART.md)
2. [Getestete Plattform und lokale Varianten](docs/02_PLATFORMEN.md)
3. [Eigenes Zertifikat und ESP-Vertrauensanker](docs/03_SYNOLOGY_CERTIFICATE.md)
4. [Synology Reverse Proxy](docs/04_SYNOLOGY_REVERSE_PROXY.md)
5. [Synology Container Manager / Docker installieren](docs/05_SYNOLOGY_CONTAINER.md)
6. [Firmware konfigurieren, bauen und installieren](docs/06_FIRMWARE_BUILD.md)
7. [Alle Konfigurationen und Datenablagen](docs/07_CONFIGURATION.md)
8. [NAS-/SD-Ausschlussregeln](docs/08_EXCLUSIONS.md)
9. [Bedienung, Reparatur und Updates](docs/09_OPERATION.md)
10. [Home Assistant installieren](docs/10_HOME_ASSISTANT.md)
11. [HTTP-API](docs/11_API.md)
12. [Fehlersuche](docs/12_TROUBLESHOOTING.md)
13. [Tests und Veröffentlichung](docs/13_TESTING_PUBLICATION.md)
14. [Hardware und Pinbelegung](docs/14_HARDWARE.md)
15. [Automatische NAS-Wiederaufnahme in 1.0.3](docs/INSTALLATION_1.0.3.md)
16. [Versionsübersicht und Exportprüfung](CHANGELOG.md) · [Prüfergebnisse](EXPORT_VALIDATION.md)

### Was ist einstellbar?

| Einstellung | Ort |
|---|---|
| WLAN, NAS-Dienstkonto, Web-Steuerpasswort | lokales `include/secrets.h` aus Vorlage |
| Produktive NAS-URL | lokales `include/nas_index_config.h` |
| HTTPS-Vertrauen | öffentliches PEM → `tools/configure_local.py` → lokales `include/synology_cert.h` |
| Intervall und Pause | Webseite/HA, dauerhaft in ESP-NVS; 1 Minute bis 7 Tage |
| NAS-Wiederaufnahme | Webseite; Build-Vorgaben in `include/recovery_config.h`, Standard 30/60/120/300 s |
| Vollaufbau-Schwellwert | `include/sync_config.h`; Vorgabe **2500**, nicht 25 |
| Quellpfad, UID:GID, State | NAS-`compose.yaml` |
| Ausnahmen | NAS-Projekt: `nas-excludes.txt`, `sd-excludes.txt` |
| HA-IP und Passwort | HA-`secrets.yaml` |

### Wo liegen die Daten?

`/source` im Container ist der schreibgeschützte NAS-Ordner. `/state/index.sqlite` enthält den Index, Pläne, zugehörige Verwaltungsdaten und das Ereignisarchiv. Auf SD bleibt `BNCACHE` reserviert. Der ESP speichert Einstellungen und offene Wiederherstellungszustände in NVS. Die genauen Pfade stehen in [Konfiguration](docs/07_CONFIGURATION.md).

Webseite und HA arbeiten im vertrauenswürdigen LAN über **HTTP**; zum NAS verwendet der ESP **HTTPS mit Zertifikats- und Hostnamenprüfung**. Keine Internet-Portweiterleitung erforderlich. Die Quelldaten werden nicht auf dem NAS gelöscht, **nicht ausgeschlossene SD-Daten können bei Spiegelung/Reparatur jedoch entfernt werden**.

### Build und Projektinhalt

```sh
python3 tools/configure_local.py --ca certificates/local/ca.crt
# secrets.h und nas_index_config.h lokal ausfüllen
python3 tools/embed_web_ui.py
pio run
```

Die Zertifikatsdatei muss vorher nach Anleitung bereitstehen. Für Hosttests nach installiertem ESP-IDF-Paket: `python3 test/run.py` (Clang, Node.js, Ruby erforderlich). Das App-Image entsteht unter `.pio/build/esp32-s3-devkitc-1/firmware.bin`; es wird hier **kein privates fertiges Image** mitgeliefert.

`src/` enthält den C-Kern; `tools/nas-indexdienst/` den Python-Dienst und Compose-Vorlagen; `tools/web-ui/` die Webquellen; `integrations/home_assistant/` die YAML-Vorlagen; `test/` die aktuellen Regressionstests. Abhängigkeiten werden beim Build geladen und nicht als private Entwicklungskopie mitgeliefert.

Der Test35-Kern wurde auf echter Hardware geprüft; die 1.0.0-Oberfläche wurde vom Projektinhaber als funktionierend bestätigt. Eine vollständige Abnahme jeder Funktion, jeder HA-Version oder jedes Nachbaus wird damit nicht behauptet. Offline-Protokollierung ist begrenzt, siehe [Betrieb](docs/09_OPERATION.md).

### Vor dem Veröffentlichen

Dieser Export enthält Platzhalter, keine privaten Backups, NAS-/SD-Nutzdaten, Zertifikate oder Firmware-Images. Eigene lokal erzeugte Dateien sind per `.gitignore` ausgeschlossen. Das ersetzt keine Kontrolle vor einem Commit. Die eigenen Projektdateien dürfen nach [PolyForm Noncommercial 1.0.0](LICENSE) nichtkommerziell verwendet, angepasst und weitergegeben werden. Die Lizenz erlaubt auch die darin genannten Organisationen, nicht ausschliesslich Privatpersonen. Kommerzielle Nutzung ist nicht freigegeben. Es gibt keinen Support, keine zugesagte Wartung und keine garantierten Updates: [Nutzung und Support](NUTZUNG.md). Hinweise zu Abhängigkeiten: [THIRD_PARTY.md](THIRD_PARTY.md).

---

## English

A dedicated Wi-Fi stick based on the **Waveshare ESP32-S3-GEEK** mirrors a Synology directory onto microSD and exposes it as USB mass storage. The NAS source remains read-only. Designed for embroidery files, the synchronization itself does not filter file extensions.

This independent hobby project is not an official BERNINA product and is not affiliated with the manufacturer. Compatibility with a particular embroidery-machine model requires separate testing. I developed it with assistance from **OpenAI ChatGPT/Codex** for implementation, debugging, optimization and documentation.

**Noncommercial use and adaptation under PolyForm Noncommercial 1.0.0 are welcome. No support, maintenance or guaranteed updates are provided.**

**Current public version: 1.0.3.** The first Git commit can start at this version; earlier private releases are not required. Hardware acceptance of 1.0.3 is pending.

**No WebDAV server required:** The stick uses the custom Python index service in Docker over HTTPS. Synology's WebDAV Server package and port 5006 are not used in this setup. Internal `WEBDAV_*` setting names remain for compatibility and refer to the index-service account and URL.

### Features

- Scheduled synchronization with a configurable interval; manual actions through the website or Home Assistant.
- Since 1.0.3, automatic recovery from temporary NAS outages with configurable delays, safe repair after interrupted writes and enforced APP/USB ownership locks.
- NAS-side SQLite index and planner: unchanged generations avoid another SD file comparison. Handles new files, content changes, subdirectories, renames and deletions.
- Delta transfers for small changes; a full rebuild above the configured threshold. Initial population of a large collection can take a long time; large downloads are not promised to finish in seconds.
- Bounded HTTPS bundles with SHA-256 checks, internal DMA buffers and recovery/checkpoint handling.
- Confirmed APP/USB ownership transfer prevents concurrent filesystem access. The USB host receives read-only access; manual SD edits are outside normal operation.
- Color display with progress and Matrix screensaver.
- A single German-language web page for status, inventory, actions, settings, today's events and text download.
- Home Assistant buttons, status/history sensors, interval value and minutes/hours selection, plus a link to the stick.
- Separate NAS/SD exclusion files supporting recursive patterns for `@eaDir` and macOS metadata.

### Architecture

| Component | Purpose |
|---|---|
| NAS source directory | Authoritative files, mounted read-only into Docker |
| Docker index service | Index, SHA-256, immutable plans, bundles and event archive |
| DSM reverse proxy | HTTPS 5443 → local HTTP 18080 → container 8080 |
| ESP32 | Execute plans, write SD, manage USB ownership, display/web/API |
| Optional Home Assistant | Controls, interval configuration and state history |

**This solution was built and tested on Synology using Container Manager, DSM reverse proxy and a private HTTPS certificate.** It uses the custom index service only. A NAS is not technically required: adjusted paths, permissions, DNS and HTTPS allow the same architecture on Windows/macOS. These variants have not been accepted/tested; Windows uses a Linux container. See [platforms](docs/02_PLATFORMEN.md) for examples and limitations.

### Complete installation guides

1. [Quick start and requirements](docs/01_QUICKSTART.md)
2. [Tested platform and local variants](docs/02_PLATFORMEN.md)
3. [Private certificate and ESP trust anchor](docs/03_SYNOLOGY_CERTIFICATE.md)
4. [Synology reverse proxy](docs/04_SYNOLOGY_REVERSE_PROXY.md)
5. [Synology Container Manager / Docker](docs/05_SYNOLOGY_CONTAINER.md)
6. [Configure, build and install firmware](docs/06_FIRMWARE_BUILD.md)
7. [All configuration and storage locations](docs/07_CONFIGURATION.md)
8. [NAS/SD exclusion rules](docs/08_EXCLUSIONS.md)
9. [Operation, repair and upgrades](docs/09_OPERATION.md)
10. [Home Assistant installation](docs/10_HOME_ASSISTANT.md)
11. [HTTP API](docs/11_API.md)
12. [Troubleshooting](docs/12_TROUBLESHOOTING.md)
13. [Testing and publication](docs/13_TESTING_PUBLICATION.md)
14. [Hardware and pins](docs/14_HARDWARE.md)
15. [Automatic NAS recovery in 1.0.3](docs/INSTALLATION_1.0.3.md)
16. [Version notes](CHANGELOG.md) · [Export validation](EXPORT_VALIDATION.md)

### Configuration

| Setting | Location |
|---|---|
| Wi-Fi, NAS service credentials, web control password | Local `include/secrets.h`, copied from example |
| Production NAS URL | Local `include/nas_index_config.h` |
| HTTPS trust | Public PEM → `tools/configure_local.py` → local `include/synology_cert.h` |
| Interval and pause | Web/HA, persisted in ESP NVS; 1 minute to 7 days |
| NAS recovery | Web settings; build defaults in `include/recovery_config.h`, 30/60/120/300 s |
| Full-rebuild threshold | `include/sync_config.h`, default **2500**, not 25 |
| Source path, UID:GID, state directory | NAS `compose.yaml` |
| Exclusions | NAS project: `nas-excludes.txt`, `sd-excludes.txt` |
| HA IP and control password | HA `secrets.yaml` |

### Data locations

Container `/source` is the read-only NAS directory. `/state/index.sqlite` stores the index, plans, management data and event archive. SD folder `BNCACHE` is reserved. ESP NVS stores configuration and recovery state. See [configuration](docs/07_CONFIGURATION.md) for the complete mapping.

The website and HA use **HTTP** on a trusted LAN; NAS access uses **HTTPS with certificate and hostname verification**. No Internet port forwarding is required. The service never deletes source files on the NAS, but **mirroring/repair can delete non-excluded SD data**.

### Build and repository layout

```sh
python3 tools/configure_local.py --ca certificates/local/ca.crt
# Fill in local secrets.h and nas_index_config.h
python3 tools/embed_web_ui.py
pio run
```

Prepare the certificate first as documented. Once the ESP-IDF package is installed, run host tests with `python3 test/run.py` (Clang, Node.js and Ruby required). The app image appears at `.pio/build/esp32-s3-devkitc-1/firmware.bin`; **no preconfigured private binary** is included.

`src/` contains the C firmware; `tools/nas-indexdienst/` the Python service and Compose examples; `tools/web-ui/` the web sources; `integrations/home_assistant/` the HA YAML; `test/` current regression tests. Build dependencies are downloaded rather than included as a private development installation.

The Test35 core was tested on real hardware, and the project owner confirmed the 1.0.0 web interface works. This does not establish full acceptance of every feature, HA version or third-party build. Offline event retention has limits; see [operation](docs/09_OPERATION.md).

### Before publishing

This export contains placeholders, no private backups, NAS/SD user data, certificates or preconfigured firmware. `.gitignore` excludes locally generated secrets and state, but does not replace a pre-commit review. Original project files may be used, modified and shared under [PolyForm Noncommercial 1.0.0](LICENSE). The license also permits the organizations it lists; it is not limited to private individuals. Commercial use is not granted. No support, maintenance or updates are promised: [usage and support](NUTZUNG.md). Dependency notices: [THIRD_PARTY.md](THIRD_PARTY.md).
