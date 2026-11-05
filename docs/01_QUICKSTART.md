# Schnellstart / Quick start

[Deutsch](#deutsch) · [English](#english)

## Deutsch

Dieser Aufbau verwendet den eigenen Python-Indexdienst in Docker über HTTPS. Das DSM-Paket „WebDAV Server“ wird nicht benötigt und nicht verwendet.

### Voraussetzungen

Waveshare ESP32-S3-GEEK (16 MiB Flash, 2 MiB PSRAM), microSD mit bestehendem FAT-Dateisystem, stabile Stromversorgung und 2,4-GHz-WLAN mit WPA2. Der Code formatiert eine Karte nicht automatisch. Eine separate, entbehrliche Testkarte verwenden; die SD ist ein Spiegel, kein zweites Archiv.

NAS mit DSM und Container Manager, lesbarer Quelle, beschreibbarem separatem State und lokal auflösbarem DNS-Namen. Das Entwicklungsgerät benötigt Python 3, PlatformIO, OpenSSL und USB-Zugriff. Für Hosttests zusätzlich Clang, Node.js und Ruby (Linux/macOS; Windows gegebenenfalls WSL und gesonderter USB-Zugriff).

### Reihenfolge

1. Beispielwerte festlegen: NAS-DNS `nas.home.arpa`, Quelle `/volume1/Embroidery`, öffentlicher Pfad `/Embroidery/`, HTTPS-Port 5443. Dies sind **Beispiele**, keine automatisch erkannten Einstellungen.
2. [Zertifikat](03_SYNOLOGY_CERTIFICATE.md) mit passendem DNS-SAN vorbereiten und DSM zuweisen.
3. [Docker-Projekt](05_SYNOLOGY_CONTAINER.md) mit eigener State-Ablage und read-only Quelle anlegen.
4. [Reverse Proxy](04_SYNOLOGY_REVERSE_PROXY.md) einrichten; Manifest mit curl und CA prüfen.
5. [Firmware](06_FIRMWARE_BUILD.md) lokal konfigurieren und bauen. Die drei lokalen Header stehen nicht im ZIP; das Einrichtungswerkzeug erzeugt sie aus Vorlagen und öffentlichem Zertifikat.
6. Neuinstallation bewusst flashen. Danach `http://STICK-IP/` öffnen, Passwort verwenden und die Karte kontrolliert an APP übergeben.
7. Bei erstmaligem/unterbrochenem Bestand „Spiegel reparieren“ mit `DAS KANN LANGE DAUERN` starten. Vorher Datenträger am Computer auswerfen. Nur nach Erfolg wird USB freigegeben.
8. Ohne NAS-Änderung erneut prüfen; danach kleine Änderung, gleich grosse Inhaltsänderung, Umbenennung und Löschung einzeln testen. Erst danach Automatik für den täglichen Betrieb verwenden.
9. Optional [HA installieren](10_HOME_ASSISTANT.md).

Für bestehende erfolgreiche 1.0.0-/Test35-Sticks **keinen Reset und keine neue Partitionstabelle** verwenden. Nur die App aktualisieren und den gültigen Checkpoint behalten. Normale Updates sind kein Erstaufbau.

---

## English

This setup uses the custom Python index service in Docker over HTTPS. DSM WebDAV Server is neither required nor used.

### Requirements

Waveshare ESP32-S3-GEEK (16 MiB flash, 2 MiB PSRAM), microSD with an existing FAT filesystem, stable power and 2.4-GHz WPA2 Wi-Fi. The firmware does not automatically format a card. Use a dedicated disposable test card; the SD is a mirror, not an independent archive.

A DSM NAS with Container Manager, readable source directory, separate writable state directory and locally resolvable DNS hostname. Development requires Python 3, PlatformIO, OpenSSL and USB access. Host tests additionally require Clang, Node.js and Ruby (Linux/macOS; Windows may use WSL with USB configured separately).

### Installation order

1. Choose example values: DNS `nas.home.arpa`, source `/volume1/Embroidery`, public URL path `/Embroidery/`, HTTPS port 5443. These are **examples**, not automatically detected settings.
2. Prepare the matching DNS-SAN [certificate](03_SYNOLOGY_CERTIFICATE.md) and assign it in DSM.
3. Create the [Docker project](05_SYNOLOGY_CONTAINER.md), with separate state and read-only source.
4. Configure the [reverse proxy](04_SYNOLOGY_REVERSE_PROXY.md); verify the manifest with curl and your CA.
5. Configure and [build firmware](06_FIRMWARE_BUILD.md). The three local headers are intentionally absent from the ZIP; the setup tool creates them from templates and the public certificate.
6. Deliberately flash a new installation. Open `http://STICK-IP/`, use the control password and transfer card ownership to APP.
7. For initial/incomplete contents, run repair with `DAS KANN LANGE DAUERN`. Eject the computer's volume first. USB is released only after success.
8. Check an unchanged NAS, then test one addition, same-size content change, rename and deletion. Enable routine automation after validating these.
9. Optionally [install HA](10_HOME_ASSISTANT.md).

For an existing successful 1.0.0/Test35 stick, **do not reset or replace the partition table**. Update only the app and preserve the valid checkpoint. A normal upgrade is not an initial population.
