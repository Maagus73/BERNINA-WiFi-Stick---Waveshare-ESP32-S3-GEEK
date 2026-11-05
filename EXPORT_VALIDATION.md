# Exportprüfung / Export validation — 1.0.3, Dokumentationsrevision 2

## Deutsch

Erstellt am 02.11.2026. Basis ist der geprüfte öffentliche Quellstand 1.0.3,
ergänzt um die ausführliche Dokumentation aus dem Export 1.0.0-GitHub-Doku-v2.
Der private Arbeitsstand wurde dabei nicht umgebaut.

- Alle 14 Themenkapitel der ausführlichen alten Dokumentation sind enthalten.
- Die deutsch-englische README, Plattformvarianten, Support-/Nutzungshinweise,
  KI-Mitwirkung, Zertifikatsbeispiele und Einrichtungshilfe sind wieder enthalten.
- Kein WebDAV-Server als Voraussetzung: Docker-Python-Indexdienst über HTTPS.
  WEBDAV_* sind bestehende interne Konfigurationsnamen, kein WebDAV-Installationsweg.
- Ereignis-API aktualisiert: 16 Archiv- plus 16 lokale Einträge, getrennte Cursor,
  Boot-/Tageswechsel und Lückenmarkierung; Wiederaufnahme-API von 1.0.3 ergänzt.
- Aktive Firmwarequellen byteweise identisch mit dem privaten Stand und dem zuvor
  geprüften öffentlichen 1.0.3-Export; keine älteren Firmwaredateien übernommen.
- Alle 19 Host-Testgruppen und PlatformIO-Build dieser unveränderten Firmware
  waren beim vorangegangenen 1.0.3-Export erfolgreich. Kein erneuter Firmware-Build
  für diese Dokumentationsrevision; die Ergebnisse sind keine Hardwareabnahme.
- YAML-Syntax und generierte Webdateien nach Vereinheitlichung der Beispielpfade erneut geprüft.
- Zertifikatsanleitung separat ausgeführt: CA, Server-Signierung, Kette und
  DNS-Hostname erfolgreich geprüft. Einrichtungshilfe auf Erzeugung, Erhalt lokaler
  Zugangsdaten, expliziten Zertifikatswechsel und Abweisung privater Schlüssel
  sowie ungültiger Eingaben geprüft. Temporäre Zertifikate nicht ins Paket übernommen.
- Lokale Markdown-Dateiverweise, Abdeckung der alten Kapitel, bekannte lokale
  Geheimnisse/Hostnamen und ZIP-/Dateiprüfsummen vor dem Verpacken geprüft.
- LICENSE unverändert: PolyForm Noncommercial 1.0.0. Fremdlizenztexte und NOTICE bleiben erhalten.

Keine produktive NAS-Verbindung, kein Flashen, keine Live-HA-Installation,
keine Änderung von NAS-/SD-Nutzdaten und kein Upload zu GitHub durchgeführt.
Die Hardwareabnahme von 1.0.3 bleibt offen. SHA256SUMS prüft die Paketdateien;
die separate .zip.sha256-Datei prüft das ganze Archiv.

## English

Created 2026-11-02 from the validated public 1.0.3 source export, restoring the
complete bilingual documentation from 1.0.0-GitHub-Doku-v2. The private working
project was not restructured.

- All 14 original topic chapters, detailed bilingual README, platform variants,
  support/usage notes, AI acknowledgement, certificate examples and setup tool retained.
- Docker Python index service over HTTPS; no WebDAV server prerequisite.
  Existing WEBDAV_* setting names remain for source compatibility only.
- Event API updated for independent 16-entry local/archive pages, boot/day changes
  and gap signaling; 1.0.3 recovery API included.
- Firmware source files byte-identical to the current working project and previously
  validated public 1.0.3 export. No older firmware copied into this release.
- All 19 host-test groups and PlatformIO build passed for this unchanged firmware
  in the preceding export. No repeated firmware build for this documentation revision.
- Deployment YAML and embedded web assets checked again after aligning example paths.
- Certificate instructions executed in a temporary directory: CA, signing, chain and
  hostname passed. Setup helper checked for initial creation, preservation of local
  credentials, deliberate certificate replacement, and private-key/invalid-input rejection.
- Local Markdown file links, old-chapter coverage, known local secrets/hostnames,
  file checksums and ZIP integrity checked before packaging.
- Unmodified PolyForm Noncommercial 1.0.0 project license; third-party notices retained.

No production NAS connection, flashing, live HA installation, user-data changes or
GitHub upload performed. Hardware acceptance of 1.0.3 remains pending. SHA256SUMS
covers package files; the separate .zip.sha256 covers the archive.
