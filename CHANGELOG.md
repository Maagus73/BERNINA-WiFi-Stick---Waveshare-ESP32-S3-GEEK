# Versions- und Dokumentationsübersicht / Version and documentation notes

## Deutsch

Der erste öffentliche Git-Stand beginnt bei 1.0.3. Die Versionsnummer der Firmware
wird dafür nicht zurückgesetzt. Der NAS-Dienst meldet weiterhin seine eigene
Dienstversion 1.0.0; das ist unabhängig von der Firmwareversion.

- 1.0.0: gemeinsame Weboberfläche, Status/Konfiguration, Ereignisarchiv und HA-Anbindung.
- 1.0.1/1.0.2: begrenzte Ereignisseiten mit unabhängigen lokalen/Archiv-Cursorn,
  Boot-/Tageswechsel- und Lückenbehandlung; aktuelle API siehe docs/11_API.md.
- 1.0.3: automatische NAS-Wiederaufnahme, konfigurierbare Wartezeiten,
  persistente Wiederaufnahmeabsicht und sichere Reparatur nach Schreibabbruch.

Die ausführliche deutsch-englische Dokumentation aus dem Git-Export „Doku-v2“
wurde übernommen und an 1.0.3 angepasst. Die 14 Themenkapitel, Plattformvarianten,
Zertifikatsbeispiele, Einrichtungshilfe, Nutzung-/Supporthinweise und KI-Mitwirkung
bleiben erhalten. Ein gewöhnlicher WebDAV-Server ist kein Bestandteil dieser
Installation; verwendet wird der eigene Indexdienst in Docker über HTTPS.
PolyForm Noncommercial 1.0.0 ersetzt die früheren unvollständigen Nutzungshinweise
als Projektlizenz. Fremdlizenzen bleiben erhalten.

## English

The first public Git commit starts at 1.0.3; firmware versioning is not reset.
The NAS service still reports its separate service version 1.0.0.

- 1.0.0: unified web UI, status/settings, event archive and HA integration.
- 1.0.1/1.0.2: bounded event pages with independent local/archive cursors,
  boot/day changes and gap handling; current API: docs/11_API.md.
- 1.0.3: automatic NAS recovery, configurable backoff, persisted intent and
  safe repair after interrupted writes.

The complete bilingual Doku-v2 guide has been retained and updated for 1.0.3,
including its 14 chapters, platform variants, certificate examples, setup tool,
usage/support notes and acknowledgement of AI assistance. This installation uses
the custom index service in Docker over HTTPS, not a general WebDAV server.
PolyForm Noncommercial 1.0.0 now supplies the project license; third-party licenses
remain in force.
