# Firmware: Build und Installation / Build and installation

[Deutsch](#deutsch) · [English](#english)

## Deutsch

Dieser Aufbau verwendet den eigenen Python-Indexdienst in Docker über HTTPS. Das DSM-Paket „WebDAV Server“ wird nicht benötigt und nicht verwendet.

### Werkzeuge

PlatformIO IDE in VS Code oder PlatformIO Core installieren. [Offizielle Installation](https://docs.platformio.org/en/latest/core/installation/methods/installer-script.html). `pio --version` muss im verwendeten Terminal funktionieren. Die Projektkonfiguration bindet `espressif32@6.10.0` und ESP-IDF; Abhängigkeiten werden beim ersten Build geladen. Nicht zusätzlich ein anderes Arduino-/ESP-IDF-Projekt darüberlegen.

### Lokale Einrichtung

Vom entpackten Projektstamm arbeiten. Den öffentlichen CA-Vertrauensanker nach Zertifikatsanleitung bereitstellen, dann:

```sh
python3 tools/configure_local.py --ca certificates/local/ca.crt
python3 tools/embed_web_ui.py
pio run
```

**Zwischen Einrichten und Build** die erzeugten `include/secrets.h` und `include/nas_index_config.h` bearbeiten: WLAN, NAS-Dienstkonto, eigenes Web-Steuerpasswort, NAS-DNS/Port/Pfad. Der erste Befehl erzeugt nur Beispiele und Zertifikatheader, keine funktionierenden Zugangsdaten. Konkrete Werte niemals in die `.example.h`-Dateien schreiben. Bestehende lokale Zugangsdaten überschreibt das Werkzeug nicht.

Die internen Konfigurationsnamen `WEBDAV_USER`, `WEBDAV_PASSWORD` und `WEBDAV_URL` bleiben aus Kompatibilitätsgründen erhalten. Sie bezeichnen den Zugriff auf den eigenen Indexdienst. Der aktive Modus verwendet die URL aus `nas_index_config.h`; daher **dort** die produktive URL ändern. Hostname und Pfad müssen zu Zertifikat/Proxy/URL_PREFIX passen. `synology_cert.example.h` ist nur eine Strukturvorlage, kein gültiges Zertifikat.

In `platformio.ini` bei Bedarf `upload_port` und `monitor_port` auf den eigenen Port setzen oder auf der Kommandozeile wählen. Die private Mac-Portbelegung wurde entfernt. Build-Ausgabe: `.pio/build/esp32-s3-devkitc-1/firmware.bin`. Nicht öffentlich verteilen: selbstgebaute Images enthalten die lokalen Zugangsdaten.

### Erstinstallation auf neuem Gerät

Vorher eine dedizierte FAT-microSD einsetzen, Display-/SD-Pins und 16-MiB-Flash/2-MiB-PSRAM prüfen. Eine frische Erstinstallation kann mit PlatformIO erfolgen:

```sh
pio device list
pio run --target upload --upload-port YOUR_SERIAL_PORT
pio device monitor --port YOUR_SERIAL_PORT --baud 115200
```

**Diese Upload-Befehle sind bewusst manuell**, nichts wird beim normalen `pio run` geflasht. PlatformIO installiert bei einem normalen Upload auch die zu diesem Projekt gehörenden Flash-Komponenten/Partitionstabelle. Bei unbekannter Vorbelegung zuvor Daten und Wiederherstellungszustand beurteilen. Das Projekt formatiert keine SD automatisch.

### Update eines funktionierenden Test35-/1.0.0-Sticks

Nicht wie eine neue Karte behandeln. Nur `firmware.bin` als **App an 0x10000** schreiben; niemals ab Adresse 0. Kein `erase_flash`, keine neue Partitionstabelle und kein NVS-Verlust. Ein kompletter generischer Upload ist für diese Update-Anleitung nicht die bevorzugte Methode. Den konkreten manuellen App-Flash mit dem eigenen ESP-Tool durchführen. Der Build hier führt keinen Flash aus.

Den NAS-Dienst zuerst aktualisieren, indem nur `server.py` ersetzt wird; bestehende Compose-/Credential-/State-Dateien beibehalten. Bei gültigem Bestand und Checkpoint keinen Reset auslösen. Nach Installation Version, WLAN, unveränderten Abgleich und kleine Delta-Änderung prüfen. Rechte von USB/APP werden nicht durch einen Build umgangen.

Für Hosttests: zuerst Build-Abhängigkeiten installieren (`pio run`), dann Clang/Node.js/Ruby bereitstellen und `python3 test/run.py` verwenden. `IDF_PATH` beziehungsweise `PLATFORMIO_CORE_DIR` kann für eine abweichende Paketinstallation gesetzt werden. [Testumfang](13_TESTING_PUBLICATION.md).


### Lokale Variante – nicht abgenommen

Der Firmware-Build läuft bereits auf dem Entwicklungsrechner; für eine lokale Quelle ändert sich nur die Serverkonfiguration. Beispiel: produktive URL `https://stickfiles.home.arpa:5443/Embroidery/`, dazu passender CA-Vertrauensanker und erreichbarer Rechner. Keine Änderung an APP-/USB-Schutz oder Prüfsummen dafür vornehmen. Der lokale Dienst selbst ist separat einzurichten.

---

## English

This setup uses the custom Python index service in Docker over HTTPS. DSM WebDAV Server is neither required nor used.

### Tools

Install PlatformIO IDE for VS Code or PlatformIO Core. [Official installation](https://docs.platformio.org/en/latest/core/installation/methods/installer-script.html). Ensure `pio --version` works in your terminal. The project pins `espressif32@6.10.0` and uses ESP-IDF; dependencies are downloaded on the first build. Do not overlay a different Arduino/ESP-IDF project.

### Local setup

Work from the extracted project root. Prepare the public CA trust anchor as documented, then:

```sh
python3 tools/configure_local.py --ca certificates/local/ca.crt
python3 tools/embed_web_ui.py
pio run
```

**Between setup and build**, edit the generated `include/secrets.h` and `include/nas_index_config.h`: Wi-Fi, NAS service credentials, a separate web control password, NAS DNS/port/path. Setup creates templates and a certificate header, not working credentials. Never put real values in `.example.h` files. Existing local credentials are preserved.

The internal names `WEBDAV_USER`, `WEBDAV_PASSWORD` and `WEBDAV_URL` remain for compatibility and configure access to the custom index service. The active mode takes its URL from `nas_index_config.h`, so change the production URL **there**. Hostname/path must match the certificate, proxy and URL_PREFIX. `synology_cert.example.h` only illustrates the format; it is not a valid certificate.

Set `upload_port`/`monitor_port` in `platformio.ini` if necessary, or choose ports on the command line. The author's Mac port is not included. Build output: `.pio/build/esp32-s3-devkitc-1/firmware.bin`. Do not publish your configured image: it contains local credentials.

### New-device installation

Use a dedicated FAT microSD first; check display/SD pins and 16-MiB flash/2-MiB PSRAM. A fresh device can be installed through PlatformIO:

```sh
pio device list
pio run --target upload --upload-port YOUR_SERIAL_PORT
pio device monitor --port YOUR_SERIAL_PORT --baud 115200
```

**These upload commands are deliberate manual actions.** Ordinary `pio run` does not flash anything. A normal PlatformIO upload writes the project's flash components/partition table too. Assess unknown existing contents and recovery state before installation. This project does not automatically format SD cards.

### Upgrade a working Test35/1.0.0 stick

Do not treat an upgrade as a new card. Write only `firmware.bin` as the **app at 0x10000**, never at address 0. Do not erase flash, replace the partition table or lose NVS. A generic full upload is not the preferred method for this upgrade procedure. Use your ESP flashing tool to explicitly write only the app. The build performed here never flashes hardware.

Update the NAS first by replacing only `server.py`, preserving Compose, credentials and state. Do not reset an already valid inventory/checkpoint. Verify version, Wi-Fi, an unchanged check and a small delta after installation. Building firmware does not bypass USB/APP ownership.

For host tests, first install build dependencies (`pio run`), then Clang/Node.js/Ruby and run `python3 test/run.py`. Set `IDF_PATH` or `PLATFORMIO_CORE_DIR` for a non-default package installation. [Test coverage](13_TESTING_PUBLICATION.md).

### Local variant — not accepted/tested

Firmware already builds on the development computer. A local source changes the server configuration: e.g. production URL `https://stickfiles.home.arpa:5443/Embroidery/`, matching CA trust anchor and reachable computer. Do not change APP/USB protections or checksums. Set up the local service separately.
