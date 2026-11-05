# BERNINA WiFi Stick 1.0.3 – automatische NAS-Wiederaufnahme / Automatic NAS recovery

## Deutsch

Ein NAS-Neustart oder ein vorübergehender Verbindungsabbruch soll keinen dauerhaften manuellen Eingriff verlangen. Version 1.0.3 ergänzt die automatische Wiederaufnahme. Die APP-/USB-Übergabe und die bestehenden Schreib-, Journal-, SHA256- und Abschlussprüfungen bleiben verbindlich.

### Verhalten

- **NAS-Ausfall vor Schreibbeginn:** Der Stick prüft den letzten vollständig bestätigten SD-Checkpoint unabhängig vom NAS. Nur wenn dieser zur Karte passt und vollständig lesbar ist, gibt er USB wieder frei. Ohne diesen Nachweis bleibt USB zurückgehalten; die Wiederaufnahme verwendet eine Reparatur.
- **Abbruch nach Schreibbeginn:** USB bleibt durch das bestehende Journal gesperrt. Nach Rückkehr derselben NAS-Quelle wird automatisch die vorhandene Reparatur ausgeführt: passende Dateien per SHA256 bestätigen, fehlende/geänderte Dateien übertragen und Zusatzdaten erst nach der vorgesehenen NAS-Prüfung bereinigen. Kein erneutes Löschen des gesamten Bestands zu Beginn der Wiederaufnahme.
- **NAS weiterhin nicht erreichbar:** Erneut versuchen, standardmässig nach 30, 60, 120 und anschliessend jeweils 300 Sekunden. Es gibt keine feste Anzahl, nach der allein wegen Nichterreichbarkeit aufgegeben wird. Die Wartezeit beginnt nach Abschluss des jeweiligen Fehlversuchs; Verbindungs-Timeouts kommen hinzu. Ohne WLAN wird gewartet, bis WLAN wieder verfügbar ist.
- **Nach Erfolg:** Wiederaufnahme abschliessen, USB nach bestätigter Übergabe anmelden und zum normalen Abgleichintervall zurückkehren. Auch bei einer Wiederaufnahme können viele fehlende Dateien entsprechend lange dauern.
- **Manuelle Pause:** Bleibt wirksam, auch nach Neustart. „Automatik fortsetzen“ gibt eine vorhandene Wiederaufnahme wieder frei.
- **Dauerhafte/technische Fehler:** Falsche Zugangsdaten, Zertifikatsfehler, geänderte Quellenidentität, geänderte Ausschlussregeln während einer Reparatur, ungültige Daten/Prüfsummen oder SD-/Speicherübergabefehler werden nicht durch Wiederholungen oder ein Zurücksetzen der Sperre übergangen. Unbekannte Fehler stoppen vorsichtshalber. Nach Behebung gegebenenfalls „Spiegel reparieren“ ausdrücklich starten.

Die Wiederaufnahme prüft zunächst den NAS-Dienst, ohne die SD vom USB zu übernehmen. Erst nach erfolgreicher Quellenprüfung erfolgt die bestehende APP-Übergabe. Ein während der Wiederaufnahme neu erforderlicher Plan wird vollständig geprüft, bevor Nutzdaten verändert werden. Quelle und Regeln werden vor den Schreiboperationen nochmals geprüft.

### Konfiguration

Auf der Webseite: **Aktionen → Konfiguration → NAS-Ausfälle automatisch erneut versuchen**. Die vier Zahlen sind Sekunden für Versuch 1, 2, 3 und alle weiteren Versuche. Zulässig sind ganze Zahlen von 1 bis 86400, aufsteigend oder gleich. Beispiel: `30,60,120,300`. Passwort eingeben und **Wiederaufnahme speichern** klicken. Kein Neustart nötig. Die Werte werden erst nach erfolgreichem Speichern aktiv. Die Felder sind unabhängig vom normalen Abgleichintervall.

Die Build-Vorgaben stehen in [recovery_config.h](../include/recovery_config.h). Bereits gespeicherte Einstellungen haben Vorrang. Abschalten unterbricht keinen laufenden Schreibvorgang, verhindert aber weitere automatische Wiederaufnahmen. Es entfernt keine Journalsperre. Eine ausdrücklich gestartete manuelle Synchronisierung/Reparatur ersetzt die wartende Wiederaufnahmeabsicht; die technischen Sperren bleiben erhalten.

Wiederaufnahmeabsicht, Quellenbindung und Einstellungen werden separat in NVS gespeichert. Bei einem Neustart kann ein zuvor gespeicherter Wiederaufnahmeauftrag fortgesetzt werden. Ein älterer, vor 1.0.3 entstandener offener Vorgang ohne solchen Auftrag benötigt einmal die manuelle Reparatur. Ein Stromausfall, bevor die Wiederaufnahmeabsicht gespeichert werden konnte, garantiert keine automatische Wiederaufnahme; die bestehenden Journale schützen weiterhin die USB-Freigabe. Nicht lesbare/ungültige gespeicherte Aufträge werden nicht blind ausgeführt.

Webseite und Display zeigen die Wartephase und den nächsten Versuch. Das Ereignisprotokoll enthält Stufe, HTTP-Status, ESP-/Socket-/TLS-Diagnose und vorhandene sichere Servercodes. „Letzter erfolgreicher Abgleich“ bleibt während eines Ausfalls unverändert. Ein weiterhin lesbarer alter SD-Stand ist kein neuer erfolgreicher NAS-Abgleich.

### Installation und erster Test

1. Firmware selbst konfigurieren und bauen; Installation nach [INSTALLATION.md](INSTALLATION.md).
2. Webseite vollständig neu laden und Version 1.0.3 prüfen. **NAS-Dienst und Home Assistant benötigen kein Update.** Die zusätzlichen Statusfelder sind über `/api/status` verfügbar; bestehende HA-Entitäten bleiben kompatibel.
3. Einen unveränderten Abgleich erfolgreich durchführen. NAS-Dienst dann kurz stoppen und einen weiteren automatischen Abgleich abwarten beziehungsweise in APP „Jetzt abgleichen“ starten.
4. Erwartung: klare Fehlermeldung, Wartezeit und erneute Versuche. Bei bestätigtem unverändertem SD-Stand soll USB nutzbar bleiben beziehungsweise zurückgegeben werden. NAS-Dienst wieder starten: der nächste Versuch muss ohne manuellen USB-Klick abschliessen und USB zurückgeben.
5. Separat eine Unterbrechung während eines kleinen Transfers prüfen: bis zur erfolgreichen Reparatur muss USB gesperrt bleiben. Danach Dateiinhalt und USB-Freigabe kontrollieren. Manuelle Pause und abweichende Wartezeiten ebenfalls prüfen.

Es wurde **nicht geflasht**. Automatisierte Hosttests und PlatformIO-Build sind ausgeführt; die genannten Hardware-/NAS-Ausfalltests sind noch durchzuführen. Private Rücksicherungen sind nicht Teil dieses Pakets.

---

## English

Version 1.0.3 adds automatic recovery after a temporary NAS/network outage. Existing APP/USB ownership, journals, SHA256 verification and completion checks remain authoritative.

- Before writes, USB is returned only after independently validating the last completed SD checkpoint against the card and its marker. Without this proof, recovery uses repair and withholds USB.
- After interrupted writes, the journal keeps USB locked. Once the same NAS source is reachable, repair reuses SHA256-confirmed files, downloads missing/changed files and reconciles extras only after the existing NAS checks. Recovery does not initially erase all user data.
- Retry delays default to 30, 60, 120 and then 300 seconds indefinitely. Each delay starts after a failed attempt completes; connection timeouts add to this. Missing Wi-Fi postpones attempts until connectivity returns.
- Success returns USB through the confirmed handoff and resumes the normal interval. Manual pause remains effective across reboots.
- Authentication, certificate, source-identity, repair exclusion-policy, checksum, SD and ownership errors require inspection. Unknown errors stop conservatively. Guards are never reset to claim success.

The initial NAS probe leaves SD ownership unchanged. Normal APP handoff occurs only after the source is reachable. Plans and source/policy prerequisites are checked before writes.

### Settings

On the device page open **Aktionen → Konfiguration**, enable automatic NAS recovery and set the four delays in seconds. Values must be nondecreasing integers between 1 and 86400. Enter the control password and save. Settings persist in NVS and take effect without reboot, independently of the regular sync interval. Build defaults are in `include/recovery_config.h`; saved values take precedence. Disabling retries does not cancel an active operation or clear journals. Explicit manual sync/repair replaces pending retry intent, subject to unchanged technical guards.

Retry intent and source binding are also persisted. A saved recovery job can resume after reboot. An interrupted operation created by older firmware requires one manual repair; a power loss before recovery intent is saved cannot guarantee automatic recovery. Existing journals still protect USB ownership. Invalid stored jobs are never blindly executed.

The page/display show waiting and retry countdown. Logs include stage, HTTP, ESP/socket/TLS diagnostics and safe server error codes. Last successful synchronization remains unchanged during an outage.

### Upgrade and acceptance

Configure and build your own firmware first; see [INSTALLATION.md](INSTALLATION.md). On an existing compatible device, update only the app at **0x10000**, while idle. Do not erase flash, change partitions, reset NVS or remove the SD cache. Built firmware contains your local credentials and must not be published. Reload the page and verify version 1.0.3. No NAS-service or HA update is required; extra telemetry is available from `/api/status`.

After a successful unchanged sync, briefly stop the NAS service and trigger/wait for another check. Expect automatic retries and USB availability only with a valid completed SD checkpoint. Restart the service and verify successful recovery without a manual USB click. Separately test interruption during a small transfer: USB must remain locked until repair succeeds. Also test manual pause and custom delays.

Nothing has been flashed. Validation status from the private development stage: host tests and the PlatformIO build have run; hardware/NAS-outage acceptance is pending. Private recovery archives are not included.
