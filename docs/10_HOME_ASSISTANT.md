# Home Assistant

[Deutsch](#deutsch) · [English](#english)

## Deutsch

### Installation

1. Dem Stick im Router eine feste DHCP-Zuordnung geben und seine HTTP-Seite prüfen.
2. In HA `configuration.yaml` den **bestehenden** `homeassistant:`-Block um `packages: !include_dir_named packages` erweitern. Keinen zweiten gleichnamigen Block anlegen.
3. Nur `integrations/home_assistant/bernina_stick.yaml` nach `/config/packages/bernina_stick.yaml` kopieren. Dashboard und Secret-Beispiele gehören nicht in den Paket-Include-Ordner.
4. Die vier Einträge aus `secrets.beispiel.yaml` in die vorhandene HA-`secrets.yaml` übernehmen: drei Stick-URLs und **Stick-Web-Steuerpasswort**, nicht NAS-Passwort. Beispiele ersetzen.
5. HA-Konfiguration prüfen, dann neu starten. Bestehende gleichnamige Entitäten beachten. `dashboard.yaml` als manuelle Karte übernehmen; gegebenenfalls von HA abweichend vergebene Entity-IDs anpassen.

```yaml
homeassistant:
  packages: !include_dir_named packages
```

Die mitgelieferte Lösung ist ein YAML-Paket aus REST, Template-Tasten/-Schalter, Helfern und Automation, keine automatisch entdeckte Custom Integration.

### Bedienung

REST liest alle 10 Sekunden Status. Vorhanden sind WLAN, Aktivität, Speicherbesitzer, Ergebnis, Laufzeit, Fortschritt, letzter Erfolg und Intervall. Eingabezahl plus Minuten/Stunden wählen und „Intervall speichern“ auslösen. Maximal 10080 Minuten oder 168 Stunden; derselbe Zahlenhelfer hat ein gemeinsames Maximum, zu grosse Stundenwerte werden vom Stick abgelehnt. Änderungen auf der Webseite werden beim nächsten Sensorupdate übernommen.

Aktionen: APP, Abgleich, Pause, Fortsetzen, USB, Reparatur; Matrix als Schalter. Vor APP Datenträger auswerfen. Für Reparatur im Helfer `DAS KANN LANGE DAUERN` eintragen; der Ablauf übernimmt APP und gibt USB nur bei Erfolg zurück. HTTP-Fehler erscheinen im Skript-Trace. 202 bedeutet angenommen, nicht fertig.

Das Dashboard zeigt den HTTP-Link zur gemeldeten IP. Die REST-URLs aus Secrets ändern sich dadurch nicht automatisch; deshalb DHCP-Reservierung verwenden. Recorder/History zeichnet Zustandswechsel auf, sofern nicht ausgeschlossen. Kurze Abläufe zwischen zwei 10-Sekunden-Abfragen können übersehen werden; der letzte Erfolgszeitpunkt bleibt ein separater Sensor. Vollständige Textlogs in der Webseite abrufen, nicht in ständig wachsenden HA-Attributen speichern.

Dieses Paket wurde syntaktisch und gegen die lokale API geprüft; eine reale HA-Abnahme ist Sache der Installation. Offizielle Grundlagen: [Pakete](https://www.home-assistant.io/docs/configuration/packages/), [REST](https://www.home-assistant.io/integrations/rest/), [REST Command](https://www.home-assistant.io/integrations/rest_command/), [Template](https://www.home-assistant.io/integrations/template/).


### Lokale Variante – nicht abgenommen

HA spricht weiterhin nur mit der HTTP-IP des Sticks. Ob dessen Quelle auf Synology, Windows oder macOS liegt, ändert die HA-Konfiguration nicht. Beispiel: `bernina_stick_status_url: "http://192.168.1.123/api/status"` bleibt die Stick-IP, nicht die Rechner-IP.

---

## English

### Installation

1. Reserve the stick's DHCP address in your router and verify its HTTP page.
2. Extend the **existing** `homeassistant:` section in HA `configuration.yaml` with `packages: !include_dir_named packages`; do not create a duplicate section.
3. Copy only `integrations/home_assistant/bernina_stick.yaml` to `/config/packages/bernina_stick.yaml`. Do not place dashboard or secret examples in the package include directory.
4. Merge the four `secrets.beispiel.yaml` entries into existing HA `secrets.yaml`: three stick URLs and the **stick web control password**, not the NAS password. Replace examples.
5. Check HA configuration and restart. Avoid existing entity-name collisions. Add `dashboard.yaml` as a manual card; adjust entity IDs if HA assigned different names.

```yaml
homeassistant:
  packages: !include_dir_named packages
```

This is a YAML package using REST, template buttons/switch, helpers and automation, not an automatically discovered custom integration.

### Operation

REST polls status every 10 seconds: Wi-Fi, activity, storage owner, result, uptime, progress, last success and interval. Select a number and minutes/hours, then save. Maximum 10080 minutes or 168 hours. The shared numeric helper has one maximum; the stick rejects excessive hour values. Web changes are reflected on the next sensor update.

Actions include APP, sync, pause, resume, USB and repair; Matrix uses a switch. Eject before APP takeover. Enter `DAS KANN LANGE DAUERN` in the confirmation helper before repair; it takes APP and returns USB only on success. HTTP errors appear in script traces. 202 means accepted, not completed.

The dashboard links to the reported HTTP IP address. That does not automatically change REST URLs in secrets, so reserve DHCP. Recorder/History records state changes unless excluded. Short operations can fall between 10-second polls; last success is a separate timestamp sensor. Retrieve full text logs through the website instead of growing HA attributes indefinitely.

The package was syntax-checked and tested against the local API; real HA acceptance remains installation-specific. Official references: [packages](https://www.home-assistant.io/docs/configuration/packages/), [REST](https://www.home-assistant.io/integrations/rest/), [REST Command](https://www.home-assistant.io/integrations/rest_command/), [Template](https://www.home-assistant.io/integrations/template/).

### Local variant — not accepted/tested

HA still connects only to the stick HTTP IP. A Synology, Windows or macOS source does not change HA configuration. Example: `bernina_stick_status_url: "http://192.168.1.123/api/status"` uses the stick IP, not the source computer IP.
