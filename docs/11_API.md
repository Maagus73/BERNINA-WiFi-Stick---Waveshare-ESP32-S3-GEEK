# Lokale HTTP-API / Local HTTP API

[Deutsch](#deutsch) · [English](#english)

## Deutsch

Basis `http://STICK-IP`, JSON, `Cache-Control: no-store`. Die gleiche Schnittstelle wird von Web und HA verwendet. Status ist im LAN öffentlich lesbar; Schreibaktionen benötigen das Web-Steuerpasswort. Es gibt keine alten Test-/„Alles okay“-Reset-Endpunkte. `/diagnostics` ist nur ein Alias derselben Startseite.

### Lesen

`GET /api/status` liest nur Zustandsdaten, keinen SD-Baum:

| Fields | Values / units |
|---|---|
| version, web_url | Version, HTTP URL |
| busy, operation | Boolean; running / idle / failed |
| wifi_connected, uptime_seconds | Boolean; seconds |
| storage, locked | APP / USB / LOCKED; boolean |
| interval_seconds, interval_value, interval_unit | Seconds; chosen value; minutes / hours |
| paused, next_check_seconds | Boolean; seconds |
| files, directories, inventory_known, inventory_valid | Confirmed counts; validity flags |
| progress, files_done, files_total, phase | Percent; file counters; current phase |
| last_result, last_success | unknown / completed / failed; Unix time or 0 |
| screensaver | Boolean |
| event_archive_online, events_pending, events_dropped | Last transport status; queue/overflow counters |

`GET /api/events?after=0&local_after=0` mit Header `X-Bernina-Key: STEUERPASSWORT` lädt heutige Ereignisse. Antwort enthält `events`, `next`, `more`, `local`, `archive_available`, `busy`, `day`, `boot`, `local_next`, `local_more`, `local_gap`. Für Folgeseiten unabhängig `after=next` und `local_after=local_next` verwenden. Je Antwort höchstens 16 Archiv- und 16 lokale Einträge. Nachladen, solange `more` oder `local_more` wahr ist. Bei neuer Boot-ID lokale Abfrage ab 0 wiederholen; bei Tageswechsel beide Cursor auf 0 zurücksetzen. `local_gap` kennzeichnet eine überholte lokale Pufferposition. `local` kann archivierte Einträge wiederholen: anhand `(boot,seq)` deduplizieren. Während Arbeit oder bei Archivfehler bleibt die lokale Teilliste verfügbar. Timestamps sind Unixsekunden, 0 bedeutet unbekannt. `day` bezieht sich auf Europe/Zurich.


### Schreiben

`POST /api/action` benötigt `X-Bernina-Control: 1`, `X-Bernina-Action` und als Body nur das Steuerpasswort (`text/plain`). Passwort nie als URL-Query schicken. Erforderliche Bestätigung steht gegebenenfalls in `X-Bernina-Confirm`:

| X-Bernina-Action | X-Bernina-Confirm |
|---|---|
| `app` | — |
| `sync` | — |
| `pause`, `resume` | — |
| `usb` | — |
| `repair` | `DAS KANN LANGE DAUERN` |
| `screensaver_on`, `screensaver_off` | — |
| `reboot` | `NEUSTART` |

Sync/USB brauchen bestätigten APP-Besitz und keine sperrenden Journale. Reparatur übernimmt APP selbst und gibt USB nur nach Erfolg frei. Vorher Datenträger am Computer auswerfen. Reboot nur im Leerlauf; aktive Arbeit kann nicht durch neue Steuerbefehle überholt werden.

`POST /api/config`: derselbe Passwortbody und Steuerheader, zusätzlich `X-Bernina-Interval: 10` und `X-Bernina-Unit: minutes` (alternativ `hours`). Ganzzahl 1..10080 Minuten oder 1..168 Stunden. Wird vor Aktivierung dauerhaft gespeichert; kein Reboot.

Statuscodes: 200 erledigt/gespeichert, 202 in Queue, 400 Parameter ungültig, 403 Authentifizierung fehlt, 409 aktiv/technisch gesperrt/Bestätigung fehlt, 500 Speichern fehlgeschlagen, 503 Zustandsabfrage kurzzeitig beschäftigt. `message` erläutert den Grund. **202 ist kein Erfolgsnachweis des späteren Transfers.**

NAS-Endpunkte unter URL_PREFIX: `.bernina-index-v1`, `.bernina-plan-v2`, Ausschluss-Endpunkte und Bundles sowie `.bernina-events-v1`; sie verlangen das NAS-Dienstkonto. Ereignis-POST ist bis 16 Einträge/Request begrenzt und wird erst nach bestätigter Speicherung aus der ESP-Queue freigegeben. Alle diese Endpunkte werden vom mitgelieferten Python-Indexdienst bereitgestellt.

---

## English

Base URL `http://STICK-IP`, JSON, `Cache-Control: no-store`. Web and HA use the same interface. LAN status is public; controls require the web password. Old test/“everything okay” reset endpoints are absent. `/diagnostics` aliases the same main page.

### Read

`GET /api/status` reads state, not the SD tree:

| Fields | Values / units |
|---|---|
| version, web_url | Version, HTTP URL |
| busy, operation | Boolean; running / idle / failed |
| wifi_connected, uptime_seconds | Boolean; seconds |
| storage, locked | APP / USB / LOCKED; boolean |
| interval_seconds, interval_value, interval_unit | Seconds; chosen value; minutes / hours |
| paused, next_check_seconds | Boolean; seconds |
| files, directories, inventory_known, inventory_valid | Confirmed counts; validity flags |
| progress, files_done, files_total, phase | Percent; file counters; current phase |
| last_result, last_success | unknown / completed / failed; Unix time or 0 |
| screensaver | Boolean |
| event_archive_online, events_pending, events_dropped | Last transport status; queue/overflow counters |

`GET /api/events?after=0&local_after=0` requires `X-Bernina-Key: YOUR_STICK_CONTROL_PW`. Responses include `events`, `next`, `more`, `local`, `archive_available`, `busy`, `day`, `boot`, `local_next`, `local_more`, `local_gap`. Continue the two cursors independently using `after=next` and `local_after=local_next` while `more` or `local_more` is true. Each response contains at most 16 archived and 16 local entries. Restart the local cursor at 0 on a new boot ID; reset both cursors on a new day. `local_gap` signals overwritten local history. Deduplicate by `(boot,seq)`. During work or archive failure only the local subset may be available. Timestamps are Unix seconds, 0 unknown; the day is Europe/Zurich.

### Write

`POST /api/action` requires `X-Bernina-Control: 1`, `X-Bernina-Action`, and only the control password in the body (`text/plain`). Never put passwords in URL queries. Where required, supply `X-Bernina-Confirm`:

| X-Bernina-Action | X-Bernina-Confirm |
|---|---|
| `app` | — |
| `sync` | — |
| `pause`, `resume` | — |
| `usb` | — |
| `repair` | `DAS KANN LANGE DAUERN` |
| `screensaver_on`, `screensaver_off` | — |
| `reboot` | `NEUSTART` |

Sync/USB require confirmed APP ownership and no blocking journals. Repair takes APP itself and releases USB only after success. Eject the computer volume first. Reboot requires idle state; controls cannot overtake active work.

`POST /api/config`: same password body/control header plus `X-Bernina-Interval: 10` and `X-Bernina-Unit: minutes` (or `hours`). Integer range: 1..10080 minutes or 1..168 hours. Persisted before activation; no reboot required.

Codes: 200 completed/saved, 202 queued, 400 invalid parameters, 403 authentication missing, 409 busy/technically locked/missing confirmation, 500 persistence failure, 503 status temporarily busy. `message` explains the reason. **202 does not prove the later transfer succeeded.**

NAS endpoints under URL_PREFIX include `.bernina-index-v1`, `.bernina-plan-v2`, exclusions, bundles and `.bernina-events-v1`, authenticated with the NAS service account. Event POST is bounded to 16 entries/request; the ESP releases queued entries only after acknowledged storage. All these endpoints are provided by the included Python index service.

## Wiederaufnahme ab 1.0.3 / Recovery since 1.0.3

Deutsch: `/api/status` ergänzt `retry_enabled`, `retry_pending`, `retry_waiting`, `retry_repair`, `retry_attempts`, `retry_seconds`, `retry_error` und `retry_delays` (vier Sekundenwerte). `retry_attempts` zählt die bisherigen fehlgeschlagenen Versuche dieser Wiederaufnahme; nach Neustart beginnt der Backoff erneut mit der ersten Wartezeit. `retry_waiting` berücksichtigt manuelle Pause und die Freigabe. `locked` und `storage` bleiben die verbindliche Aussage zur Speicherfreigabe; ein wartender Auftrag allein bestätigt keine USB-Freigabe. `retry_error` enthält die letzte Fehlerdiagnose und wird nach erfolgreichem Sync oder ausdrücklich gestarteter manueller Wiederaufnahme gelöscht.

`POST /api/config` unterstützt alternativ `X-Bernina-Retry: 0` oder `1` und `X-Bernina-Retry-Delays: 30,60,120,300`, mit denselben Passwort-/Steuerheadern wie bisher. Vier nicht absteigende ganze Zahlen von 1 bis 86400 Sekunden; nicht mit Intervall-Headern mischen. NVS wird vor Aktivierung gespeichert.

English: The status endpoint adds retry enabled/pending/waiting/repair flags, failure count, seconds to retry, last diagnostic and a four-element delay array. Backoff restarts with the first delay after reboot. Storage/locked remain authoritative for USB availability. Retry diagnostics clear after success or explicit manual restart. Use a separate authenticated config request with `X-Bernina-Retry: 0|1` and `X-Bernina-Retry-Delays: 30,60,120,300`. Delays must be nondecreasing integers from 1 to 86400 seconds; do not mix these headers with interval settings. Settings persist before activation.
