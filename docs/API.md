# Lokale API 1.0.3

Basis `http://IP-DES-STICKS`. JSON-Antworten sind nicht cachebar. Die Webseite und HA verwenden dieselbe Steuerung; ein HTTP-202 bestätigt die Annahme, nicht den Abschluss. Es gibt keine alten Test-/Reset-Endpunkte mehr. `/diagnostics` zeigt aus Kompatibilität dieselbe einzelne Seite.

## Status und Ereignisse

`GET /api/status` ist im LAN ohne Passwort lesbar und enthält keine Dateipfade oder Zugangsdaten:

| Feld | Bedeutung |
|---|---|
| version, web_url | Firmwareversion, HTTP-Link mit aktueller IP |
| busy, operation | Aktivität; `running`, `idle`, `failed` |
| wifi_connected, uptime_seconds | WLAN und Laufzeit |
| storage, locked | `USB`, `APP`, `LOCKED`; technische/Journal-Sperre |
| interval_seconds, interval_value, interval_unit | Effektives Intervall und gespeicherte Darstellung `minutes`/`hours` |
| paused, next_check_seconds | Automatikzustand und verbleibender Zeitabstand |
| files, directories, inventory_known, inventory_valid | Zuletzt bestätigter Bestand; Gültigkeitsmarkierung |
| progress, files_done, files_total, phase | Fortschritt und aktuelle Tätigkeit |
| last_result, last_success | `unknown`/`completed`/`failed`, letzter Erfolg als Unixzeit oder 0 |
| screensaver | Matrix-Einstellung |
| event_archive_online, events_pending, events_dropped | Letzter Archivtransport, Warteschlange und Offline-Verluste |

`GET /api/events?after=0&local_after=0` mit Header `X-Bernina-Key: STEUERPASSWORT` lädt heutige Ereignisse. Antwort enthält `events`, `next`, `more`, `local`, `archive_available`, `busy`, `day`, `boot`, `local_next`, `local_more`, `local_gap`. Für Folgeseiten unabhängig `after=next` und `local_after=local_next` verwenden. Je Antwort höchstens 16 Archiv- und 16 lokale Einträge. Nachladen, solange `more` oder `local_more` wahr ist. Bei neuer Boot-ID lokale Abfrage ab 0 wiederholen; bei Tageswechsel beide Cursor auf 0 zurücksetzen. `local_gap` kennzeichnet eine überholte lokale Pufferposition. `local` kann archivierte Einträge wiederholen: anhand `(boot,seq)` deduplizieren. Während Arbeit oder bei Archivfehler bleibt die lokale Teilliste verfügbar. Timestamps sind Unixsekunden, 0 bedeutet unbekannt. `day` bezieht sich auf Europe/Zurich.

## Geschützte Befehle

`POST /api/action`, Header `X-Bernina-Control: 1`, `X-Bernina-Action: AKTION`, Body **nur das Steuerpasswort** (`text/plain`). Passwort nie in URL/Query schreiben.

| Aktion | Zusatz/Voraussetzung |
|---|---|
| app | Sichere APP-Übernahme; Datenträger am Computer vorher auswerfen |
| sync | Bestätigtes APP, keine offene Journalsperre |
| pause, resume | Automatik dauerhaft pausieren/fortsetzen |
| usb | Bestätigtes APP und kein offenes Journal |
| repair | `X-Bernina-Confirm: DAS KANN LANGE DAUERN`; übernimmt APP selbst und gibt USB nur nach Erfolg zurück |
| screensaver_on, screensaver_off | Matrix ein/aus |
| reboot | `X-Bernina-Confirm: NEUSTART`, nur Leerlauf |

`POST /api/config` mit demselben Passwortbody und `X-Bernina-Control: 1`, zusätzlich `X-Bernina-Interval: GANZZAHL` und `X-Bernina-Unit: minutes` oder `hours`. Zulässig 1..10080 Minuten beziehungsweise 1..168 Stunden. Speichert NVS vor Aktivierung und setzt den nächsten Zeitabstand neu; kein Reboot nötig.

Statuscodes: 200 unmittelbar erledigt/gespeichert; 202 in Arbeitsqueue; 400 ungültige Parameter; 403 Passwort/Steuerheader fehlt; 409 Arbeit läuft, Bestätigung fehlt oder technische Voraussetzung nicht erfüllt; 500 persistentes Speichern fehlgeschlagen; 503 Status vorübergehend beschäftigt. Antwort `message` enthält den Grund. Während aktiver Arbeit werden weitere Steueraktionen abgewiesen.

## NAS-Erweiterung

`.bernina-events-v1` unter dem bestehenden NAS-URL-Präfix verwendet dieselbe Basic-Authentifizierung und HTTPS-Zertifikatsprüfung wie der Planer. POST überträgt maximal 16 Einträge mit Geräte-ID, Boot-ID und Sequenz. SQLite bestätigt die vollständige Menge; Wiederholungen sind idempotent. GET unterstützt Geräte-ID, Tagesgrenzen, Cursor und Limit. Es werden nur State-Daten geschrieben, niemals Quelldateien. Archivtransport und Dateitransfer laufen auf dem ESP nacheinander.

## Wiederaufnahme ab 1.0.3 / Recovery since 1.0.3

Deutsch: `/api/status` ergänzt `retry_enabled`, `retry_pending`, `retry_waiting`, `retry_repair`, `retry_attempts`, `retry_seconds`, `retry_error` und `retry_delays` (vier Sekundenwerte). `retry_attempts` zählt die bisherigen fehlgeschlagenen Versuche dieser Wiederaufnahme; nach Neustart beginnt der Backoff erneut mit der ersten Wartezeit. `retry_waiting` berücksichtigt manuelle Pause und die Freigabe. `locked` und `storage` bleiben die verbindliche Aussage zur Speicherfreigabe; ein wartender Auftrag allein bestätigt keine USB-Freigabe. `retry_error` enthält die letzte Fehlerdiagnose und wird nach erfolgreichem Sync oder ausdrücklich gestarteter manueller Wiederaufnahme gelöscht.

`POST /api/config` unterstützt alternativ `X-Bernina-Retry: 0` oder `1` und `X-Bernina-Retry-Delays: 30,60,120,300`, mit denselben Passwort-/Steuerheadern wie bisher. Vier nicht absteigende ganze Zahlen von 1 bis 86400 Sekunden; nicht mit Intervall-Headern mischen. NVS wird vor Aktivierung gespeichert.

English: The status endpoint adds retry enabled/pending/waiting/repair flags, failure count, seconds to retry, last diagnostic and a four-element delay array. Backoff restarts with the first delay after reboot. Storage/locked remain authoritative for USB availability. Retry diagnostics clear after success or explicit manual restart. Use a separate authenticated config request with `X-Bernina-Retry: 0|1` and `X-Bernina-Retry-Delays: 30,60,120,300`. Delays must be nondecreasing integers from 1 to 86400 seconds; do not mix these headers with interval settings. Settings persist before activation.
