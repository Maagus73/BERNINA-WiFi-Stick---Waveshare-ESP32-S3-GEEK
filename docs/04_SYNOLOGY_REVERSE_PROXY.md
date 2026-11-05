# Synology Reverse Proxy

[Deutsch](#deutsch) · [English](#english)

## Deutsch

Zuerst den Container starten. In DSM Systemsteuerung → Anmeldeportal → Erweitert → Reverse Proxy eine Regel für den Indexdienst anlegen. Quelle ist der auf dem NAS ankommende HTTPS-Zugriff, Ziel der lokale Container-Port:

| Setting | Value |
|---|---|
| Source protocol | HTTPS |
| Source hostname | `nas.home.arpa` |
| Source port | `5443` |
| Destination protocol | HTTP |
| Destination hostname | `127.0.0.1` |
| Destination port | `18080` |

**Nur den Hostnamen eintragen**, weder `https://` noch `/Embroidery/`. Der vollständige URL-Pfad wird unverändert durchgereicht und vom Container anhand `URL_PREFIX` ausgewertet. Port 5443 muss frei sein.

Das passende Zertifikat in DSM diesem Proxy-Dienst zuweisen. Lokales DNS muss den NAS-Namen zur NAS-IP auflösen. Die CA muss dem ESP und curl bekannt sein. Kein Backend-Loginportal, keine Weiterleitung, kein Antwort-Caching/Kompression für die Transfer-Endpunkte konfigurieren. Authorization und die `X-Bernina-*`-Header müssen ankommen. HTTP/1.1 zum Backend verwenden. Verbindungs-, Sende- und Lese-Timeouts passend für langsame Erstaufbauten setzen; etwa 300 s als Ausgangswert, bei Bedarf anhand der Diagnose prüfen. Der ESP hat eigene Zeitlimits.

Prüfung vom Rechner im LAN:

```sh
curl --fail --cacert certificates/local/ca.crt --user YOUR_NAS_USER \
  'https://nas.home.arpa:5443/Embroidery/.bernina-index-v1' \
  --output /tmp/bernina-manifest.txt
```

Das Passwort wird abgefragt. Die Datei unter `/tmp` liegt **auf dem Rechner, auf dem curl läuft**, nicht auf dem NAS oder Stick. Erwartet: Beginn `BINDEX1`, Abschluss `END` und passende Generation. Der Inhalt enthält Dateipfade und ist nicht automatisch für GitHub geeignet. 401 betrifft Dienst-Zugangsdaten, TLS-Fehler Zertifikat/DNS, 503 den Dienst/Quellzustand oder Proxy.

[Offizielle DSM-Proxy-Anleitung](https://kb.synology.com/en-us/DSM/help/DSM/AdminCenter/system_login_portal_advanced). Die Ports/Pfade und Timeout-Beispiele sind Projektkonfiguration.


### Lokale Variante – nicht abgenommen

Anstelle von DSM kann ein lokal installierter HTTPS-Proxy wie Caddy dienen. Mini-Beispiel für einen auf demselben Rechner laufenden Proxy und Backend:

```caddyfile
https://stickfiles.home.arpa:5443 {
    tls /ABSOLUTE/PATH/server.crt /ABSOLUTE/PATH/server.key
    reverse_proxy 127.0.0.1:18080
}
```

Pfade ersetzen (z.B. `/Users/YOUR_USER/...` oder `C:/Users/YOUR_USER/...`), DNS/SAN passend setzen, Port 5443 in der lokalen Firewall für das benötigte LAN zulassen. Backend 18080 bleibt loopback. Caddy separat installieren und mit der lokalen Caddyfile starten; keine automatische CA-Vertrauensübernahme auf dem ESP erwarten. Dieses Beispiel wurde nicht als Gesamtsystem getestet. [TLS](https://caddyserver.com/docs/caddyfile/directives/tls), [Reverse Proxy](https://caddyserver.com/docs/caddyfile/directives/reverse_proxy).

---

## English

Start the container first. Create an index-service rule in DSM Control Panel → Login Portal → Advanced → Reverse Proxy. The source is incoming HTTPS on the NAS; the destination is the locally published container port:

| Setting | Value |
|---|---|
| Source protocol | HTTPS |
| Source hostname | `nas.home.arpa` |
| Source port | `5443` |
| Destination protocol | HTTP |
| Destination hostname | `127.0.0.1` |
| Destination port | `18080` |

Enter **only the hostname**, without `https://` or `/Embroidery/`. Preserve the entire request path; the container validates it against `URL_PREFIX`. Port 5443 must be available.

Assign the matching certificate to this proxy service. Local DNS must resolve the hostname to the NAS IP. The ESP and curl must trust the CA. Do not add a backend login portal, redirects, response caching or compression for transfer endpoints. Preserve Authorization and `X-Bernina-*` headers. Use backend HTTP/1.1. Set connection/send/read timeouts for slow initial operations, e.g. 300 s as a starting point, then diagnose as needed; the ESP has separate limits.

Verify from a LAN computer:

```sh
curl --fail --cacert certificates/local/ca.crt --user YOUR_NAS_USER \
  'https://nas.home.arpa:5443/Embroidery/.bernina-index-v1' \
  --output /tmp/bernina-manifest.txt
```

The password is prompted. `/tmp/bernina-manifest.txt` is stored **on the computer running curl**, not the NAS or stick. Expect `BINDEX1` at the start, `END` at completion and a matching generation. The file contains paths and is not automatically suitable for public upload. 401 indicates service credentials, TLS errors indicate certificate/DNS problems, and 503 indicates a service/source or proxy issue.

[Official DSM proxy guide](https://kb.synology.com/en-us/DSM/help/DSM/AdminCenter/system_login_portal_advanced). Ports, paths and timeout examples are project-specific choices.

### Local variant — not accepted/tested

A locally installed HTTPS proxy such as Caddy can replace DSM. Minimal example with proxy and backend on the same computer:

```caddyfile
https://stickfiles.home.arpa:5443 {
    tls /ABSOLUTE/PATH/server.crt /ABSOLUTE/PATH/server.key
    reverse_proxy 127.0.0.1:18080
}
```

Replace certificate paths (e.g. `/Users/YOUR_USER/...` or `C:/Users/YOUR_USER/...`), match DNS/SAN and permit port 5443 from the required LAN in the host firewall. Keep backend 18080 loopback-only. Install Caddy separately and run it with your local Caddyfile; do not assume the ESP automatically trusts its CA. Not tested as a complete system. [TLS](https://caddyserver.com/docs/caddyfile/directives/tls), [Reverse proxy](https://caddyserver.com/docs/caddyfile/directives/reverse_proxy).
