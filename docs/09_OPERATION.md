# Betrieb und Reparatur / Operation and repair

[Deutsch](#deutsch) · [English](#english)

## Deutsch

### Hauptseite

`http://STICK-IP/` öffnen. Der obere Bereich zeigt Aktivität, WLAN, Laufzeit, USB/APP, Automatikintervall, bestätigte Ordner/Dateien, letzten Erfolg und Fortschritt. Bestandszahlen stammen aus bestätigtem Zustand, nicht aus einem SD-Scan pro Webseitenaufruf. Unbekannte/veraltete Werte werden markiert. Die Benutzeroberfläche ist in 1.0.3 deutsch; diese Anleitungen sind zweisprachig.

### Aktionen

Passwort unter „BERNINA WiFi Stick - Aktionen“ eingeben. APP übernehmen erst nach Auswerfen am Computer. „Jetzt abgleichen“ benötigt bestätigtes APP und gültigen Zustand. Automatik pausieren/fortsetzen ist dauerhaft gespeichert. „USB anmelden“ ist nur ohne offene Sperre zulässig. Matrix kann ein-/ausgeschaltet werden; während Arbeit hat Fortschritt Vorrang. Konfiguration erlaubt Minuten/Stunden ohne Reboot; ein separater Neustart ist nur im Leerlauf bestätigt möglich.

### Reparatur

„Spiegel reparieren“ benötigt exakt `DAS KANN LANGE DAUERN`. Die Aktion übernimmt APP selbst, prüft vorhandene SD-Inhalte per SHA-256, übernimmt fehlende/geänderte NAS-Dateien und entfernt nach Gesamt-/NAS-Prüfung nicht ausgeschlossene Zusatzdaten. **Die SD ist ein Spiegel: Fremddaten können verloren gehen.** Nur ein erfolgreicher Abschluss meldet USB zurück. Bei Fehlern nicht wiederholt zurücksetzen, sondern Diagnose und technische Ursache klären. Ein Steckerziehen beweist keinen abgeschlossenen Stand.

### Regelbetrieb

Der Timer prüft NAS-Generationen im eingestellten Abstand. Unverändert bedeutet kein Dateivergleich nötig. Ein gültiger Checkpoint und die unveränderte dedizierte SD sind Voraussetzung für den schnellen Weg. Nach Fehler, Quellenwechsel oder fehlendem vertrauenswürdigem Zustand darf eine Reparatur länger dauern. Bei grossem Delta oberhalb des Build-Schwellwertes kann ein Vollaufbau inklusive SD-Bereinigung erfolgen. Keine pauschale Zeitgarantie.

### NAS-Ausfall und automatische Wiederaufnahme ab 1.0.3

Vorübergehende NAS-/Netzfehler führen zu erneuten Versuchen nach 30, 60, 120 und dann jeweils 300 Sekunden (konfigurierbar). Ohne WLAN wird gewartet. Nach unterbrochenem Schreiben bleibt USB gesperrt, bis die sichere Reparatur erfolgreich abgeschlossen ist. Vor Schreibbeginn wird ein vorhandener SD-Checkpoint separat geprüft; nur ein bestätigter Bestand darf wieder als USB freigegeben werden. Pause/Abschalten verhindert weitere automatische Versuche, entfernt aber keine Journalsperre. Zertifikats-, Zugangsdaten-, Quellen-, Regel- und SD-Fehler werden nicht durch Wiederholung übergangen. Details und Neustartgrenzen: [Wiederaufnahme](INSTALLATION_1.0.3.md).

### Protokoll

„Berichte“ zeigt nur den aktuellen Tag mit `HH:MM:SS` in Europe/Zurich und Textdownload. Schweizer Sommer-/Winterzeit wird berücksichtigt. Das NAS archiviert Events im State; der ESP holt Seiten nach. Bei laufender Arbeit steht zunächst der lokale Puffer bereit.

Es gibt **keine unbegrenzte verlustfreie Offline-Historie**: 128 RAM-Ereignisse, sichtbarer Verdrängungszähler und Verlust noch nicht archivierter Events bei Neustart. Vor NTP ist die Uhrzeit unbekannt. Archiv: drei Tage nach Empfang, je Gerät maximal 50000 neue Events/10 MiB Text innerhalb 24 Stunden. Archivfehler stoppen nicht allein eine gültige Dateiübernahme. HA zeichnet Zustände auf, nicht automatisch jeden Textlogeintrag.

Die NAS-Ebene bleibt bei allen Aktionen lesend. Display zeigt SYNC/Fortschritt während der Arbeit und APP/USB entsprechend bestätigtem Besitz; Fehler dürfen nicht mit einem „alles okay“-Reset verborgen werden.

---

## English

### Main page

Open `http://STICK-IP/`. Status shows activity, Wi-Fi, uptime, USB/APP, interval, confirmed directory/file counts, last success and progress. Inventory comes from confirmed state, not an SD scan on every refresh. Unknown/stale values are labeled. The 1.0.3 UI is German; these guides are bilingual.

### Actions

Enter the control password under actions. Eject the computer volume before APP takeover. Sync requires confirmed APP ownership and a valid state. Pause/resume is persistent. USB registration is allowed only without an outstanding lock. Toggle Matrix as desired; progress takes priority during work. Interval settings accept minutes/hours without reboot; a separate confirmed reboot requires idle state.

### Repair

Repair requires the exact phrase `DAS KANN LANGE DAUERN`. It takes APP ownership, SHA-256-checks existing SD data, transfers missing/changed NAS files, then removes non-excluded extras after overall/source verification. **SD is a mirror: unrelated data may be removed.** USB returns only after success. Diagnose errors rather than repeatedly resetting state. Unplugging does not establish successful completion.

### Routine operation

The timer checks NAS generations at the configured interval. Unchanged generations avoid file comparison. The fast path requires a valid checkpoint and an unchanged dedicated SD. Recovery after errors, source changes or missing trustworthy state can take longer. A large delta exceeding the build threshold can trigger a full rebuild including SD cleanup. There is no universal timing guarantee.

### NAS outages and automatic recovery since 1.0.3

Transient NAS/network failures trigger retries after 30, 60, 120 and then 300 seconds repeatedly (configurable). Without Wi-Fi, recovery waits. Interrupted writes keep USB locked until safe repair succeeds. Before writes, an existing SD checkpoint is validated separately; only confirmed contents may be exposed through USB. Pause/disabling prevents further automatic attempts without removing journal locks. Certificate, credential, source, policy and SD failures are not bypassed by retrying. Details and restart limitations: [recovery](INSTALLATION_1.0.3.md).

### Events

Reports show today's events as `HH:MM:SS` in Europe/Zurich, with text download and Swiss daylight-saving transitions. The NAS archives events in state and the ESP retrieves pages. During work, the local buffer is shown first.

There is **no unlimited lossless offline history**: 128 RAM events, visible overflow count and loss of unarchived events on restart. Timestamps before NTP are unknown. Archive retention: three days after receipt; per device, at most 50000 new events/10 MiB text per rolling 24 hours. Archive failures alone do not invalidate an otherwise successful file transfer. HA records states, not automatically every text event.

All actions remain read-only on the NAS source. Display shows SYNC/progress during work and APP/USB according to confirmed ownership; an error must not be hidden by an “everything okay” reset.
