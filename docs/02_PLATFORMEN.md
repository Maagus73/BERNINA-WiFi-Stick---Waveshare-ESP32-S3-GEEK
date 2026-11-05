# Plattformen und lokale Varianten / Platforms and local variants

[Deutsch](#deutsch) · [English](#english)

## Deutsch

### Getestete Referenz

Aufgebaut und getestet wurde die Lösung mit **Synology DS423+, DSM, Container Manager, DSM Reverse Proxy und eigenem Zertifikat**, zusammen mit dem ESP32-S3-GEEK. Beschrieben wird der tatsächlich verwendete eigene Python-Indexdienst. Die Quellablage kann grundsätzlich auch ein lokaler Ordner eines Rechners sein; ein NAS ist keine technische Voraussetzung.

### Windows und macOS mit Docker

Ein Linux-Container unter Docker Desktop bietet denselben grundsätzlichen Dienstaufbau. Anzupassen sind lokale Quell-/State-Pfade, Dateifreigabe für Docker, Rechte, DNS-Name, Zertifikat, HTTPS-Proxy, Host-Firewall und die URL in der Firmware. Der Rechner muss eingeschaltet und aus dem Stick-WLAN erreichbar sein. Schlafmodus kann Abgleiche verhindern. Diese Varianten sind **nicht als Komplettlösung getestet**.

Beispiele für `source:` des Quell-Bind-Mounts in Compose:

```yaml
# Windows (Docker Desktop, Linux containers)
source: "C:/Users/YOUR_USER/Embroidery"
# macOS
# source: "/Users/YOUR_USER/Embroidery"
```

Nur eine passende Zeile verwenden. `target: /source`, `read_only: true` und einen separaten beschreibbaren State beibehalten. Der Beispielwert `user: "1234:100"` ist Synology-spezifisch anzupassen, kein universelles Windows-/Mac-Konto. Rechte/SQLite-Schreiben und Quellenidentität auf der eigenen Docker-Freigabe prüfen. Den HTTP-Backend-Port nur lokal veröffentlichen; HTTPS nach aussen bereitstellen.

### Python direkt, ohne Container

Unter macOS ist die POSIX-Basis grundsätzlich vorhanden. Der vorhandene Container-Einstieg erwartet jedoch `/source`, `/state` und `/run/credentials.json`; für direkten Start braucht es einen eigenen Launcher mit lokalen Pfaden und denselben Zugangsdaten. Das ist keine mitgetestete Installation. Beispiel: Im Verzeichnis mit `server.py` einen lokalen Launcher erstellen; Passwort aus einer privaten `credentials.json` lesen:

```python
import json
from pathlib import Path
from server import make_server

base = Path.home() / "bernina-index-local"
credentials = json.loads((base / "credentials.json").read_text())
server = make_server(
    str(Path.home() / "Embroidery"), str(base / "state"),
    credentials["username"], credentials["password"],
    prefix="/Embroidery/", address=("127.0.0.1", 18080),
    nas_rules=str(base / "nas-excludes.txt"),
    sd_rules=str(base / "sd-excludes.txt"),
)
server.serve_forever()
```

Alle Pfade und Dateien vorher anlegen; Quelle dem Benutzer nur lesbar geben. Ein separater HTTPS-Proxy bleibt nötig. **Nativer Windows-Python-Betrieb funktioniert mit dem jetzigen Code nicht allein durch Pfadänderung:** Sichere Dateizugriffe verwenden `dir_fd`, `O_DIRECTORY` und `O_NOFOLLOW`. Eine Windows-Portierung benötigt entsprechende Codeanpassungen und Sicherheitsprüfungen. Für Windows ist daher der Linux-Container die hier skizzierte Variante.

Weitere Mini-Beispiele stehen in den Zertifikats-, Proxy-, Docker- und Build-Anleitungen. Eigene Nutzung und Anpassung sind gestattet; **kein Support**. Quellen: [Docker Desktop Windows](https://docs.docker.com/desktop/setup/install/windows-install/), [Docker Desktop macOS](https://docs.docker.com/desktop/setup/install/mac-install/), [Python OS-Schnittstellen](https://docs.python.org/3/library/os.html). Die Portierungsgrenzen ergeben sich zusätzlich aus dem aktuellen `server.py`.

---

## English

### Tested reference

The solution was built and tested with **Synology DS423+, DSM, Container Manager, DSM reverse proxy and a private certificate**, together with ESP32-S3-GEEK. This documentation describes the custom Python index service actually used. The source can in principle be a computer's local directory; a NAS is not technically required.

### Windows and macOS with Docker

A Linux container under Docker Desktop can provide the same service architecture. Adapt source/state paths, Docker file sharing, permissions, DNS, certificate, HTTPS proxy, host firewall and firmware URL. Keep the computer awake and reachable from the stick's Wi-Fi network. These variants are **not tested complete installations**.

Example Compose source bind paths:

```yaml
# Windows (Docker Desktop, Linux containers)
source: "C:/Users/YOUR_USER/Embroidery"
# macOS
# source: "/Users/YOUR_USER/Embroidery"
```

Use one appropriate line. Preserve `target: /source`, `read_only: true` and separate writable state. `user: "1234:100"` is a Synology example to adapt, not a universal Windows/Mac account. Validate permissions, SQLite writes and source identity on your Docker file share. Publish the HTTP backend on loopback only, and expose HTTPS.

### Direct Python, without a container

macOS has the required POSIX foundation in principle. The container entry point expects `/source`, `/state` and `/run/credentials.json`; direct operation needs a local launcher using appropriate paths and the same authentication. It has not been accepted/tested. In the directory containing `server.py`, a local launcher could read a private `credentials.json`:

```python
import json
from pathlib import Path
from server import make_server

base = Path.home() / "bernina-index-local"
credentials = json.loads((base / "credentials.json").read_text())
server = make_server(
    str(Path.home() / "Embroidery"), str(base / "state"),
    credentials["username"], credentials["password"],
    prefix="/Embroidery/", address=("127.0.0.1", 18080),
    nas_rules=str(base / "nas-excludes.txt"),
    sd_rules=str(base / "sd-excludes.txt"),
)
server.serve_forever()
```

Prepare all directories/files first; give the process read-only source permissions. A separate HTTPS proxy is still required. **Native Windows Python cannot run the current code merely by changing paths:** protected file access uses `dir_fd`, `O_DIRECTORY` and `O_NOFOLLOW`. A Windows port needs equivalent code changes and security validation. The Linux container is therefore the Windows approach outlined here.

Further examples appear in the certificate, proxy, Docker and firmware guides. Own use/adaptation is permitted; **no support is provided**. Sources: [Docker Desktop Windows](https://docs.docker.com/desktop/setup/install/windows-install/), [Docker Desktop macOS](https://docs.docker.com/desktop/setup/install/mac-install/), [Python OS interfaces](https://docs.python.org/3/library/os.html). Porting limits also follow from the current `server.py` implementation.
