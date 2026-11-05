# Fehlersuche / Troubleshooting

[Deutsch](#deutsch) · [English](#english)

## Deutsch

| Symptom | Prüfung / nächster Schritt |
|---|---|
| curl 60 / self-signed | Richtige öffentliche CA mit --cacert; DNS-SAN, zugewiesenes Proxy-Zertifikat, Kette und Uhrzeit prüfen; nicht -k verwenden |
| 401 beim NAS | credentials.json gegen ESP-NAS-Konto prüfen; nicht das Web-Steuerpasswort nehmen |
| 403 an Stick-API | Web-Steuerpasswort und X-Bernina-Control/Key prüfen |
| 409 an Stick-API | Aktiven Vorgang abwarten, bestätigtes APP, Journale und Bestätigungstext prüfen |
| sqlite unable to open | State muss existieren und für Compose-UID:GID schreibbar sein; DSM-ACL und tatsächliche Mounts prüfen |
| Änderungen an Compose unwirksam | Projekt neu ausrollen/erstellen; einfaches Restart ändert Mounts nicht |
| source_root_changed | Quelle/State passen nicht mehr zusammen; tatsächlichen Mount prüfen, für andere Quelle separaten State und bewusste SD-Neuübernahme verwenden |
| exclusion_config_changed | Regeln oder eingebundene Dateiinhalte ändern sich; Arbeit pausieren, stabile Regeln bereitstellen, neu planen |
| file_changed / 412 / 409 | NAS-Datei oder Generation hat sich geändert; stabilen Bestand abwarten, neuen Plan anfordern |
| 503 | Container-Live-Logs lesen; Proxy, Quelle, Rechte, Instabilität, Sonderdateien oder Datenbankfehler unterscheiden; niemals als leeren NAS interpretieren |
| Download unterbrochen / SHA-Fehler | Transferdiagnose und Server-bundle_aborted prüfen; NVS/Checkpoint nicht zurücksetzen; sichere Reparatur nach Fehlerbehebung |
| USB gesperrt | Offener/fehlgeschlagener Sync ist kein erfolgreicher Bestand; APP/Journal-/Fehlerdiagnose und Reparatur, nicht „Alles okay“ erzwingen |
| Leeres Archiv | Passwort, NTP, heutigen Tag, NAS-Dienstversion und Status Archiv/Puffer prüfen; während Transfer lokale Teilliste möglich |
| HA unavailable | Feste IP/HTTP-URL, Konfiguration und tatsächliche Entity-IDs prüfen; Skript-Trace bei abgelehnten Aktionen |
| Build fehlen secrets.h/cert.h | configure_local.py mit öffentlicher CA ausführen, lokale Header ausfüllen |

Auf dem NAS im Compose-Projektordner:

```sh
sudo docker compose logs --follow --tail 100 index
sudo docker compose ps
```

Optional tatsächlichen Container mit `docker inspect` prüfen, ohne Environment-/Credential-Inhalte öffentlich zu kopieren. Quellpfade in Manifesten/Protokollen können private Informationen enthalten. Das NAS-State-Verzeichnis nicht blind löschen, um einen Fehler zu verstecken.

WLAN mit gutem RSSI garantiert keine bestimmte TLS-/SD-Datenrate. Erstaufbau, viele kleine Dateien und verifizierte Reparatur sind langsamer als ein unveränderter Generationscheck. Webseite liest keinen Dateibaum bei jedem Statusrefresh. Für zusätzliche Messungen zuerst den konkreten Engpass belegen, nicht Zertifikats-/Prüfsummen-/Besitzschutz abschalten.

---

## English

| Symptom | Check / next action |
|---|---|
| curl 60 / self-signed | Use the correct public CA with --cacert; check DNS SAN, assigned proxy certificate, chain and time; never substitute -k |
| NAS 401 | Match credentials.json to the ESP NAS account, not its web control password |
| Stick API 403 | Check web password and X-Bernina-Control/Key |
| Stick API 409 | Wait for work, verify confirmed APP ownership, journals and exact confirmation |
| sqlite unable to open | State must exist and be writable by Compose UID:GID; check DSM ACLs and actual mounts |
| Compose changes ignored | Redeploy/recreate the project; restarting does not change mounts |
| source_root_changed | Source/state identity changed; inspect the real mount, use separate state and deliberate SD establishment for a different source |
| exclusion_config_changed | Rules/mounted contents changed; pause, establish stable rules and replan |
| file_changed / 412 / 409 | File or generation changed on NAS; wait for stable source and request a new plan |
| 503 | Read live container logs; distinguish proxy, source, permissions, instability, special files and database errors; never interpret as an empty NAS |
| Interrupted download / SHA error | Inspect transfer diagnostics and server bundle_aborted; preserve NVS/checkpoint and repair after fixing the cause |
| USB locked | Incomplete/failed sync is not success; diagnose APP/journal/error state and repair rather than force “okay” |
| Empty archive | Check password, NTP, current day, service version and queue/archive status; active transfers may expose local data only |
| HA unavailable | Check fixed IP/HTTP URL, configuration and real entity IDs; inspect script trace for rejected actions |
| Missing secrets.h/cert.h at build | Run configure_local.py with the public CA and complete the local headers |

On the NAS, from the Compose project directory:

```sh
sudo docker compose logs --follow --tail 100 index
sudo docker compose ps
```

Optionally inspect the actual container with `docker inspect`, but do not publicly copy credential/environment contents. Manifests/logs may contain private paths. Do not blindly delete NAS state to hide errors.

Good Wi-Fi RSSI does not guarantee a TLS/SD throughput. Initial population, many tiny files and verified repair take longer than an unchanged generation check. Status refresh does not scan directories. Establish the actual bottleneck before adding measurements; do not remove certificate, checksum or ownership protection.
