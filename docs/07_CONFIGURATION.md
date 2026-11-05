# Konfiguration und Daten / Configuration and storage

[Deutsch](#deutsch) · [English](#english)

## Deutsch

Alle Beispielnamen sind austauschbar, müssen aber zusammenpassen:

```text
NAS source:       /volume1/Embroidery
Public endpoint:  https://nas.home.arpa:5443/Embroidery/
URL_PREFIX:       /Embroidery/
Container source: /source
Container state:  /state
Stick website:    http://192.168.1.123/
```

Der öffentliche URL-Pfad muss nicht dem NAS-Dateisystempfad entsprechen. Hostname ist weder Freigabename noch Ordner. Abschliessenden Slash am URL_PREFIX/ESP-Basis-URL beibehalten.

| File / location | Configuration / data |
|---|---|
| `include/secrets.example.h` → `include/secrets.h` | WIFI_SSID, WIFI_PASSWORD, WEBDAV_USER, WEBDAV_PASSWORD, WEB_CONTROL_PASSWORD, WEBDAV_URL (compatibility name) |
| `include/nas_index_config.example.h` → `include/nas_index_config.h` | BERNINA_NAS_INDEX=1, effective HTTPS URL |
| `include/synology_cert.h` | Generated public trust anchor, no private key |
| `include/sync_config.h` | Defaults: automatic=1, interval=600 s, threshold=2500, attempts=3, startup delay=10 s |
| `include/sync_config.h` | Bulk enabled=1, max. 64 files / 8 MiB payload per bundle |
| `include/recovery_config.h` | Retry enabled=1; delays 30/60/120/300 s; runtime web settings override defaults |
| `platformio.ini`, `sdkconfig.defaults`, `sdkconfig.esp32-s3-devkitc-1` | Board/build/SDK/USB/FAT/PSRAM configuration |
| `partitions_16mb.csv` | NVS 0x9000, app 0x10000 / 4 MiB, historical index partition 0x410000 / 8 MiB |
| NAS `compose.yaml` | UID:GID, source/state mounts, URL_PREFIX, loopback port |
| NAS `credentials.json` | Service Basic-auth username/password |
| NAS `nas-excludes.txt`, `sd-excludes.txt` | Separate exclusion policies |
| NAS host `state/` → container `/state/` | SQLite index/plans/events and temporary service state |
| NAS host source → container `/source/` | Read-only authoritative files |
| SD `BNCACHE` | Reserved cache/checkpoint/working files; never user data |
| ESP NVS | Durable ownership/recovery/checkpoint state and `bernina_ui` settings |
| HA `secrets.yaml` | Local stick URLs and control password |

### Laufzeit gegenüber Build

Web/HA speichern Intervall und Pause in NVS; sie übersteuern nach einem Neustart die Build-Vorgaben. Bereich 1..10080 Minuten oder 1..168 Stunden. „Speichern“ aktiviert ohne Neustart und setzt den nächsten Zeitabstand neu. Pause lässt den Timer nicht schreiben. Ab 1.0.3 sind NAS-Wiederaufnahme und vier Wartezeiten auf der Webseite einstellbar: standardmässig 30, 60, 120, 300 Sekunden; Werte 1..86400, nicht absteigend. In einem separaten API-Aufruf speichern, nicht mit Intervallwerten mischen. Diese Einstellungen und eine ausstehende Wiederaufnahme werden in NVS gespeichert; manuelle Pause bleibt wirksam. Weitere Build-Parameter brauchen einen Neubau. Oberhalb des Schwellwerts (`Unterschiede > Wert`) ist ein Vollaufbau möglich; genau der Wert bleibt Delta. Der ausgelieferte Wert 2500 ist absichtlich beibehalten.

### Drei verschiedene Identitäten

1. DSM-Konto/UID:GID bestimmt Dateirechte des Containerprozesses.
2. NAS-Dienst-Benutzer/Passwort in `credentials.json` authentifiziert ESP/curl beim Python-Dienst; die Firmwarewerte müssen exakt passen. Kein automatischer DSM-Login.
3. Web-Steuerpasswort autorisiert schreibende Stick-Aktionen und Ereignisabruf, auch aus HA. Es ist unabhängig vom NAS-Passwort.

### Persistenz und Schutz

Den State nicht auf `/source` oder SD verschieben. Cache/NVS nicht als erfolgreiches Spiegelbild manipulieren. Ein NVS-Reset verliert Wiederherstellungsinformationen. Änderungen an Quelle oder Ausschlüssen können bestehende Pläne/Checkpoints ungültig machen und benötigen eine neue Planung, eventuell Reparatur. Kein Delta gegen eine fremde Quelle erzwingen.

Die alte Flash-Indexpartition bleibt aus Update-Kompatibilität im Layout, obwohl der aktuelle schnelle Planer sie nicht als vollständigen SD-Suchindex nutzt. Sie ist kein freier Bereich für Nutzdaten. SD-Nutzdaten, NAS-State und ESP-NVS sind unterschiedliche Dinge und werden durch dieses GitHub-ZIP nicht gesichert.


### Lokale Variante – nicht abgenommen

Beispielzuordnung: NAS-Quelle `/volume1/Embroidery` wird zu Windows `C:/Users/YOUR_USER/Embroidery` oder macOS `/Users/YOUR_USER/Embroidery`; Container-Ziel `/source` und URL-Pfad `/Embroidery/` bleiben gleich. Statt NAS-Hostname den DNS-Namen des Rechners verwenden, Zertifikat und Firmware passend aktualisieren. State muss weiterhin separat und persistent sein.

---

## English

Example names are replaceable but must agree:

```text
NAS source:       /volume1/Embroidery
Public endpoint:  https://nas.home.arpa:5443/Embroidery/
URL_PREFIX:       /Embroidery/
Container source: /source
Container state:  /state
Stick website:    http://192.168.1.123/
```

The public URL path need not equal the NAS filesystem path. A hostname is neither a share name nor a directory. Preserve the trailing slash on URL_PREFIX and the ESP base URL.

| File / location | Configuration / data |
|---|---|
| `include/secrets.example.h` → `include/secrets.h` | WIFI_SSID, WIFI_PASSWORD, WEBDAV_USER, WEBDAV_PASSWORD, WEB_CONTROL_PASSWORD, WEBDAV_URL (compatibility name) |
| `include/nas_index_config.example.h` → `include/nas_index_config.h` | BERNINA_NAS_INDEX=1, effective HTTPS URL |
| `include/synology_cert.h` | Generated public trust anchor, no private key |
| `include/sync_config.h` | Defaults: automatic=1, interval=600 s, threshold=2500, attempts=3, startup delay=10 s |
| `include/sync_config.h` | Bulk enabled=1, max. 64 files / 8 MiB payload per bundle |
| `include/recovery_config.h` | Retry enabled=1; delays 30/60/120/300 s; runtime web settings override defaults |
| `platformio.ini`, `sdkconfig.defaults`, `sdkconfig.esp32-s3-devkitc-1` | Board/build/SDK/USB/FAT/PSRAM configuration |
| `partitions_16mb.csv` | NVS 0x9000, app 0x10000 / 4 MiB, historical index partition 0x410000 / 8 MiB |
| NAS `compose.yaml` | UID:GID, source/state mounts, URL_PREFIX, loopback port |
| NAS `credentials.json` | Service Basic-auth username/password |
| NAS `nas-excludes.txt`, `sd-excludes.txt` | Separate exclusion policies |
| NAS host `state/` → container `/state/` | SQLite index/plans/events and temporary service state |
| NAS host source → container `/source/` | Read-only authoritative files |
| SD `BNCACHE` | Reserved cache/checkpoint/working files; never user data |
| ESP NVS | Durable ownership/recovery/checkpoint state and `bernina_ui` settings |
| HA `secrets.yaml` | Local stick URLs and control password |

### Runtime versus build settings

Web/HA persist interval and pause in NVS; after reboot these override build defaults. Range: 1..10080 minutes or 1..168 hours. Saving applies immediately without reboot and restarts the time interval. Pause prevents scheduled writes. Since 1.0.3, NAS recovery and four delays are configurable through the web UI: default 30, 60, 120, 300 seconds; nondecreasing values from 1 to 86400. Save in a separate API request, not mixed with interval settings. Configuration and pending recovery intent are persisted in NVS; manual pause remains effective. Other build parameters require rebuilding. A full rebuild is possible when differences **exceed** the threshold; an exact match stays delta. The shipped threshold remains 2500 intentionally.

### Three separate identities

1. DSM account/UID:GID determines filesystem permissions for the container process.
2. NAS service username/password in `credentials.json` authenticates ESP/curl to Python; firmware values must match exactly. There is no automatic DSM login.
3. The web control password authorizes stick actions and event retrieval, including HA. It is independent of the NAS password.

### Persistence and protection

Do not move state onto `/source` or SD. Do not manipulate cache/NVS to pretend a mirror completed. Resetting NVS destroys recovery information. Source or exclusion changes can invalidate plans/checkpoints and require replanning, possibly repair. Never force a delta against an unrelated source.

The historical flash-index partition remains for update compatibility, although the current fast planner does not use it as a complete SD search index. It is not free user storage. SD contents, NAS state and ESP NVS are separate data stores; this GitHub ZIP does not back them up.

### Local variant — not accepted/tested

Example mapping: `/volume1/Embroidery` becomes Windows `C:/Users/YOUR_USER/Embroidery` or macOS `/Users/YOUR_USER/Embroidery`; container `/source` and public `/Embroidery/` stay unchanged. Use the computer DNS hostname instead of the NAS hostname and match certificate/firmware. State must remain separate and persistent.
