# Synology Container Manager / Docker

[Deutsch](#deutsch) · [English](#english)

## Deutsch

Dieser Aufbau verwendet den eigenen Python-Indexdienst in Docker über HTTPS. Das DSM-Paket „WebDAV Server“ wird nicht benötigt und nicht verwendet.

### Installation und Verzeichnisse

In DSM Container Manager über das Paket-Zentrum installieren, sofern vom NAS unterstützt. Auf einem bestätigten DS423+ wurde das Projekt betrieben; andere Modelle benötigen eigene Prüfung. Quelle beispielhaft `/volume1/Embroidery`; Projekt `/volume1/docker/bernina-indexdienst`. Das Projekt darf **nicht im Quellordner** liegen.

Aus `tools/nas-indexdienst/` folgende Dateien in den Projektordner kopieren: `server.py`, `compose.yaml`, `nas-excludes.txt`, `sd-excludes.txt`. `credentials.beispiel.json` als `credentials.json` kopieren und lokal Benutzer/Passwort einsetzen. Darunter den Ordner `state` erstellen. Die Vorgaben sind Muster, keine echten Anmeldedaten.

### Rechte und Konfiguration

Via NAS-SSH `id YOUR_DSM_SERVICE_USER` verwenden, **nicht erst nach `sudo -i`**, sonst zeigt `id` Root 0:0. Die tatsächlichen Zahlen in Compose bei `user: "1234:100"` ersetzen. Der Prozess benötigt Quelle lesen/durchqueren, Skript/Regeln/Zugangsdaten lesen und State schreiben. DSM-ACLs zusätzlich zu Unix-Rechten beachten. Nicht pauschal chmod 777 verwenden. Ein normales Konto reicht; Root 0:0 ist keine Voraussetzung.

Quell-Bind-Mount auf den echten `/volume1/...`-Pfad setzen; `read_only: true` und `create_host_path: false` beibehalten. `/state` ist beschreibbar; das übrige Container-Dateisystem bleibt read-only. Portbindung bleibt `127.0.0.1:18080:8080`. `URL_PREFIX: "/Embroidery/"` muss zum ESP-URL-Pfad passen. Die Vorlagen verwenden absichtlich keine fehleranfällige `.env`-UID-Interpolation.

### Projekt starten

Container Manager → Projekt → Erstellen; Arbeitsordner und vorbereitete Compose-Datei auswählen, kein zusätzliches Web-Station-Webportal nötig. Starten. Nach Änderungen an **Mounts/Compose** das Projekt mit der neuen Konfiguration neu erstellen/ausrollen; ein einfacher Prozessneustart übernimmt Mountänderungen nicht. State erhalten, nicht „alle Daten löschen“ wählen.

Optional NAS-SSH im Projektordner:

```sh
sudo docker compose config --quiet
sudo docker compose up -d
sudo docker compose logs --follow --tail 100 index
```

Ältere DSM-Docker-Versionen verwenden eventuell `docker-compose`. Bei abweichendem Projektnamen/Containername keine festen Namen aus fremden Logs übernehmen. `logs --follow` zeigt Live-Ereignisse ohne CSV-Download; Ctrl-C beendet nur die Anzeige.

### Zustand und Quellwechsel

`state/index.sqlite` enthält Index, Pläne und Ereignisse. Die Quelle wird vor autoritativen Antworten lokal geprüft; geänderte Metadaten führen zu neuen Hashprüfungen. Hashes sind keine NAS-ETags. Externe Änderungen über SMB/File Station werden dadurch berücksichtigt; eine während des Scans instabile Quelle kann einen sicheren Abbruch auslösen.

Genau eine Instanz pro State. Beim Stoppen SQLite-Dateien im State zusammen sichern, nicht nur eine möglicherweise laufende `index.sqlite` herauskopieren. Quelle nicht in-place wechseln: Für Test/Produktion separate Quellen und `state-test`/`state` verwenden; am ESP ist ein bewusst neu abgeglichener Bestand erforderlich. `source_root_changed` darf nicht durch Abschalten der Prüfung umgangen werden.

`python:3.13-slim-bookworm` ist ein veränderlicher Image-Tag. Die geprüfte Image-ID kann der Betreiber nach eigener Abnahme festhalten. Das Paket enthält keine privaten State-Daten. [Offizieller Projekt-Assistent](https://kb.synology.com/en-global/DSM/help/ContainerManager/docker_project).


### Lokale Variante – nicht abgenommen

Unter Docker Desktop (Windows: Linux-Container) die Quell-Bind-Quelle etwa auf `C:/Users/YOUR_USER/Embroidery` oder `/Users/YOUR_USER/Embroidery` setzen. State separat im lokalen Projektordner halten, Freigabe/Rechte und UID:GID anpassen. Aus dem Projektordner funktionieren grundsätzlich dieselben `docker compose`-Befehle, lokal meist ohne `sudo`. DSM wird durch einen lokalen HTTPS-Proxy ersetzt. [Plattformen](02_PLATFORMEN.md) erläutert Grenzen; kein unverändert ausführbares, abgenommenes Windows-/Mac-Paket.

---

## English

This setup uses the custom Python index service in Docker over HTTPS. DSM WebDAV Server is neither required nor used.

### Installation and directories

Install Container Manager through DSM Package Center if supported by the NAS. The project ran on a DS423+; other models require their own validation. Example source: `/volume1/Embroidery`; project: `/volume1/docker/bernina-indexdienst`. Keep the project **outside the source directory**.

Copy `server.py`, `compose.yaml`, `nas-excludes.txt` and `sd-excludes.txt` from `tools/nas-indexdienst/` into the project directory. Copy `credentials.beispiel.json` to `credentials.json`, then enter your service username/password locally. Create a `state` subdirectory. The supplied values are examples, not working credentials.

### Permissions and configuration

Run `id YOUR_DSM_SERVICE_USER` over NAS SSH, **not after `sudo -i`**, which would identify root as 0:0. Replace Compose's `user: "1234:100"` with the real numeric IDs. The process needs source read/traverse, script/rules/credentials read, and state write access. Check DSM ACLs as well as Unix permissions. Do not blanket-apply chmod 777. A normal account is sufficient; root 0:0 is not required.

Set the bind source to the actual `/volume1/...` directory. Preserve `read_only: true` and `create_host_path: false`. `/state` is writable; the remaining container filesystem is read-only. Keep `127.0.0.1:18080:8080`. `URL_PREFIX: "/Embroidery/"` must match the ESP URL path. Templates deliberately avoid `.env` UID interpolation.

### Start the project

In Container Manager → Project → Create, select the working directory and prepared Compose configuration. No additional Web Station portal is needed. Start it. Changes to **mounts/Compose** require redeploying/recreating with the new configuration; restarting only the process does not change mounts. Preserve state; do not choose deletion of all project data.

Optional NAS SSH commands from the project directory:

```sh
sudo docker compose config --quiet
sudo docker compose up -d
sudo docker compose logs --follow --tail 100 index
```

Older DSM Docker versions may use `docker-compose`. Do not assume another user's project/container names. `logs --follow` displays live events without CSV downloads; Ctrl-C exits the viewer only.

### State and source changes

`state/index.sqlite` stores index, plans and events. The service checks the source locally before authoritative responses; changed metadata triggers hashing. Hashes are not interpreted from NAS ETags. SMB/File Station changes are included; changes during scanning can intentionally abort an unstable scan.

Use one instance per state directory. For backups, stop the service and preserve the SQLite state files together rather than copying only a potentially live database. Do not silently switch sources: use separate test/production sources and `state-test`/`state`, and establish the intended SD baseline explicitly. Do not disable `source_root_changed` protection.

`python:3.13-slim-bookworm` is a mutable image tag. Operators may record/pin their tested image ID after acceptance. No private state is distributed. [Official project wizard](https://kb.synology.com/en-global/DSM/help/ContainerManager/docker_project).

### Local variant — not accepted/tested

With Docker Desktop (Linux containers on Windows), adapt the source bind to e.g. `C:/Users/YOUR_USER/Embroidery` or `/Users/YOUR_USER/Embroidery`. Keep separate local project state and adjust file sharing, permissions and UID:GID. The same `docker compose` commands apply in principle, usually without local `sudo`. Use a local HTTPS proxy instead of DSM. See [platforms](02_PLATFORMEN.md) for limitations; this is not an accepted ready-to-run Windows/Mac package.
