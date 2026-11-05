# BERNINA WiFi Stick 1.0.1 – Protokollkorrektur / Event-log fix

## Deutsch

1.0.1 korrigiert den Protokollabruf der Version 1.0.0. Am laufenden Gerät liess sich der Unterschied nachstellen: Status HTTP 200, Protokollverbindung ohne Antwort geschlossen. Speichermangel ist eine plausible Ursache; ohne Geräte-Heap-Diagnose ist diese Ursache nicht abschliessend bewiesen.

Die Antwort enthält jetzt höchstens 16 Archiv- und 16 lokale Einträge. Beide Listen werden unabhängig seitenweise nachgeladen. Der ESP schnappt nur eine kleine lokale Seite unter dem Mutex und erzeugt deren JSON danach. TLS-Ressourcen werden vor dem Parsen der Archiv-Antwort freigegeben. Die fertige Antwort wird in einem festen 32-KiB-PSRAM-Puffer serialisiert. Fehler ergeben eine kleine JSON-Antwort mit HTTP 503 statt eines stillen Abbruchs.

Die Webseite zeigt den Ladevorgang, verhindert parallele Protokollabrufe und behält bereits geladene Zeilen bei Fehlern. Ohne geladene Einträge ist der Download deaktiviert. Heruntergeladene Teilstände sind ausdrücklich als **UNVOLLSTÄNDIGER ZWISCHENSTAND** gekennzeichnet. Tageswechsel und neue Boot-ID setzen die zugehörigen Cursor kontrolliert zurück.

## Installation auf dem bestehenden Stick

Installation: eigenes Image bauen; bestehende kompatible Geräte nur per App-Update an 0x10000 aktualisieren. Siehe [Build](06_FIRMWARE_BUILD.md).
- Kein `erase_flash`, keine neue Partitionstabelle und kein Zurücksetzen von NVS/Checkpoint. Nur im Leerlauf aktualisieren.
- **Docker/NAS-Dienst und HA-Konfiguration müssen für dieses Update nicht geändert werden.** Der bisherige NAS-Endpunkt unterstützt die kleineren Seiten bereits. Keine NAS-/SD-Daten oder Journale löschen.
- Nach dem manuellen Flash die Webseite vollständig neu laden; oben muss 1.0.1 stehen. Passwort erneut eingeben, Berichte öffnen, Aktualisieren und anschliessend Download prüfen.

Es wurde nur gebaut, nicht geflasht. Ein Neustart für die Installation verliert noch nicht archivierte RAM-Einträge; bereits bestätigte NAS-Ereignisse bleiben im bestehenden State. Die Tagesansicht zeigt weiterhin nur den aktuellen Tag. Eine reale Abnahme des neuen Images steht noch aus.

## Abnahme

Ein langer Tagesbestand mit mehr als 128 Einträgen muss vollständig nachladbar sein. Ladezustand und Download-Sperre während des Abrufs prüfen. Bei Unterbrechung dürfen bestehende Zeilen nicht verschwinden; erneutes Laden muss fortsetzen. Ein Teil-Download muss als solcher markiert sein. Einen unveränderten Sync und APP-/USB-Anzeige zusätzlich kontrollieren. Der Test35-Kern und die 1.0.0-Artefakte bleiben als Rückweg erhalten.

Die neuen Hosttests prüfen 128 lokale Datensätze, Archivseiten, 32-KiB-Heapbudget, injizierte Allokationsfehler, gültige 503-Antworten, Cursor, Browser-Abbruch/Timeout, Tageswechsel, Bootwechsel und Downloadschutz. Der Host-Heapverbrauch ist kein Messwert des ESP-Heaps.

---

## English

1.0.1 fixes log retrieval in 1.0.0. The live device reproduced HTTP 200 for status while closing the event connection without a response. Memory pressure is a plausible cause, not conclusively established without device heap diagnostics.

Responses now contain at most 16 archive and 16 local entries, paged independently. Only one small local snapshot is copied under the mutex; JSON construction happens afterwards. TLS resources are released before archive JSON parsing. Output serialization uses a fixed 32-KiB PSRAM buffer. Failures return a small JSON HTTP 503 response rather than silently closing the connection.

The website shows loading, prevents parallel log requests and retains already loaded rows on failure. Download is disabled until entries exist. Partial exports are explicitly labeled **UNVOLLSTÄNDIGER ZWISCHENSTAND**. Day changes and new boot IDs safely reset the relevant cursors.

## Upgrade the existing stick

Installation: build your own image; on existing compatible devices, update only the app at 0x10000. See [build](06_FIRMWARE_BUILD.md).
- No flash erase, partition-table replacement or NVS/checkpoint reset. Update only while idle.
- **No Docker/NAS-service or HA configuration update is required.** Existing NAS endpoints already support smaller pages. Do not delete NAS/SD data or journals.
- After manually flashing, reload the page completely and verify version 1.0.1. Enter the password again, open reports, refresh and check download.

Built only, not flashed. Installation reboot loses unarchived RAM events; acknowledged NAS entries remain in existing state. The page still shows today's entries only. Hardware acceptance of this new image is pending.

## Acceptance checks

A day containing more than 128 entries must load through pagination. Check loading feedback and disabled download during requests. On interruption, existing rows must remain; retry must continue. Partial downloads must be labeled. Also check an unchanged sync and APP/USB display. Test35 and 1.0.0 artifacts remain available for rollback.

New host tests cover 128 local records, archive pages, a 32-KiB heap budget, allocation-failure injection, valid 503 responses, cursors, browser failures/timeouts, day changes, boot changes and download protection. Host heap consumption is not a measurement of the ESP heap.
