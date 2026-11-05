# BERNINA WiFi Stick in Home Assistant

Vorbereitetes YAML-Paket für 1.0.0. Es liest dieselbe lokale HTTP-API wie die Webseite und ändert keine NAS-Dateien. Es wurde nicht in deinem HA installiert. YAML-Syntax und lokale API-Verträge sind geprüft; die abschliessende HA-Konfigurationsprüfung und Geräteabnahme erfolgen bei dir.

## Installation

1. Im Router eine feste DHCP-Zuordnung für den Stick anlegen. Die Webseite soll unter `http://STICK-IP/` erreichbar sein.
2. In HA `configuration.yaml` unter dem bestehenden `homeassistant:`-Block `packages: !include_dir_named packages` ergänzen. Einen bestehenden Block erweitern, keinen zweiten gleichen Schlüssel anlegen. [Offizielle Paket-Anleitung](https://www.home-assistant.io/docs/configuration/packages/).
3. **Nur** `bernina_stick.yaml` nach `/config/packages/bernina_stick.yaml` kopieren. Dashboard und Beispiels-Secrets gehören nicht in diesen Include-Ordner.
4. Die vier Einträge aus `secrets.beispiel.yaml` in die bestehende `/config/secrets.yaml` übernehmen. IPs und Steuerpasswort lokal einsetzen. Das Passwort ist das Web-Steuerpasswort des Sticks, nicht das NAS-Passwort.
5. HA „Konfiguration prüfen“, anschliessend neu starten. Bereits vorhandene gleichnamige Entitäten vermeiden.
6. `dashboard.yaml` als manuelle Karte im Dashboard übernehmen. Falls HA wegen Namenskollisionen abweichende Entity-IDs vergeben hat, diese dort und in Paket-Referenzen anpassen.

Es werden REST-Sensoren, Template-Tasten/Schalter und Eingabehelfer eingerichtet; keine zusätzliche Custom Integration erforderlich. Grundlage sind [REST](https://www.home-assistant.io/integrations/rest/), [REST Command](https://www.home-assistant.io/integrations/rest_command/) und [Template](https://www.home-assistant.io/integrations/template).

## Bedienung

Die Statusabfrage läuft alle 10 Sekunden und liest keine SD-Verzeichnisse. WLAN, Laufzeit, Speicherbesitzer, Tätigkeit, Fortschritt, Ergebnis und letzter Erfolg stehen als Entitäten bereit. Die Intervallanzeige ist in Sekunden; Wert und Einheit stehen zusätzlich getrennt zur Verfügung.

Für eine Änderung Eingabezahl und Minuten/Stunden wählen, danach **Intervall speichern** drücken. Bereich: maximal 10080 Minuten oder 168 Stunden, mindestens 1. HA verwendet für beide Einheiten denselben Zahlenhelfer; zu grosse Stundenwerte werden vom Stick abgelehnt. Eine Änderung auf der Webseite wird beim nächsten Statusupdate in die Helfer übernommen. Speichern benötigt keinen Neustart.

Tasten: APP übernehmen, jetzt abgleichen, Automatik pausieren/fortsetzen, USB anmelden und Spiegel reparieren; Matrix als Schalter. Vor APP den Stick am Computer auswerfen. Für Reparatur zuerst `DAS KANN LANGE DAUERN` in den Bestätigungshelfer schreiben. Bei Erfolg gibt der Stick USB selbst zurück; bei Fehler bleibt die Sperre erhalten. HTTP-Abweisungen erscheinen als Fehler im Skript-Trace. Eine angenommene Aktion ist noch kein abgeschlossener Sync.

Das Dashboard enthält einen anklickbaren HTTP-Link mit der vom Stick gemeldeten IP. Die REST-Abfrage selbst verwendet die Adresse in `secrets.yaml`; deshalb bleibt eine DHCP-Reservierung sinnvoll.

## Verlauf und Protokoll

HA Recorder/History zeichnet die Sensorzustände auf, sofern sie nicht von deiner Recorder-Konfiguration ausgeschlossen sind. Der Beispiel-Verlauf zeigt Tätigkeit, Speicherbesitzer, Ergebnis und Aktivität. Sehr kurze Vorgänge können zwischen zwei 10-Sekunden-Abfragen liegen; der separate Zeitstempel des letzten Erfolgs zeigt trotzdem den abgeschlossenen Lauf. Dies ist kein garantiertes Ereignis-für-Ereignis-Abbild.

Das vollständige heutige Textprotokoll bleibt in der Stick-Webseite mit NAS-Archiv und Textdownload. Es wird nicht als stetig wachsendes HA-Sensorattribut gespeichert. Dadurch bleibt die HA-Datenbank überschaubar. Die Webseite beschreibt Offline-Puffer und Archivgrenzen.
