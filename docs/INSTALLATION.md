# Einrichtung des öffentlichen Quellstands

## NAS-Indexdienst

`tools/nas-indexdienst/` in einen eigenen Container-Projektordner kopieren.
`credentials.beispiel.json` nach `credentials.json` kopieren und eigene Zugangsdaten
setzen. Dieselben Zugangsdaten in der lokalen Firmware-Konfiguration verwenden.

In `compose.yaml` numerische UID:GID und den Quellpfad `/volume1/Embroidery`
anpassen. Dieser Quellordner wird nur lesbar eingebunden. Den lokalen Ordner
`state` anlegen und dem Containerkonto Schreibrechte geben.
`URL_PREFIX` (Beispiel `/Embroidery/`) muss exakt zum Pfad in
`include/nas_index_config.h` passen, einschliesslich beider Schrägstriche.

DSM-Reverse-Proxy: HTTPS mit eigenem NAS-Hostnamen, beispielsweise Port 5443,
auf HTTP `127.0.0.1:18080`. Passendes Zertifikat zuweisen, Pfad unverändert
weiterreichen. Die Datei `synology_cert.h` der Firmware muss das passende
Vertrauenszertifikat enthalten. NAS-Nutzdaten bleiben schreibgeschützt;
der Dienst speichert seinen Zustand unter `/state`.

Nach Prüfung der lokalen Konfiguration im Container-Projektordner starten:

```sh
docker compose up -d
```

Bei einem bestehenden Dienst nur bewusst aktualisieren; eigene Zugangsdaten,
Ausschlüsse und State behalten. Keine zwei Instanzen auf denselben State starten.
Eine neue NAS-Quelle braucht einen eigenen State und eine bewusst neu aufgebaute SD.

## Firmware

Die Vorlagen nach README konfigurieren und `platformio run` ausführen.
Für einen neuen, unkonfigurierten ESP32-S3-GEEK kann PlatformIO das vollständige
Build-Ergebnis mit `platformio run --target upload` installieren. Seriellen Port
bei Bedarf lokal in `platformio.ini` setzen. Dazu eine separate Testkarte verwenden.

Für ein bestehendes kompatibles Gerät mit der vorhandenen Partitionstabelle
nur das App-Image `.pio/build/esp32-s3-devkitc-1/firmware.bin` an **0x10000**
aktualisieren; kein `erase_flash`, keine Partitionstabelle ersetzen, NVS behalten.
Nicht während eines Dateizugriffs flashen. Dieses ZIP enthält keine Firmware-Binaries.

## Erster Funktionstest

Webseite unter `http://IP-DES-STICKS/` öffnen und Version 1.0.3 prüfen.
Mit kleiner Testquelle WLAN, NAS-Zertifikat, Abgleich, APP-/USB-Übergabe und
Dateiinhalte kontrollieren. Danach Pause, Intervall, Neustart, Reparatur und
NAS-Ausfall/Wiederaufnahme testen. Bestehende private Test35-Rücksicherungen
sind nicht Teil dieses öffentlichen Pakets. Hardwareabnahme von 1.0.3 steht aus.
