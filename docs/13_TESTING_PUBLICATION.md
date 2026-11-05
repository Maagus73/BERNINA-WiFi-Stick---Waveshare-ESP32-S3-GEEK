# Tests und Veröffentlichung / Testing and publication

[Deutsch](#deutsch) · [English](#english)

## Deutsch

### Bestätigter Stand und Grenzen

Test35 wurde auf dem Originalgerät inklusive Dateiänderungen, Löschungen, Reparatur und schnellem unverändertem Abgleich geprüft. Der Betreiber hat die 1.0.0-Webseite als optisch und funktional erfolgreich rückgemeldet. Das ist keine vollständige Abnahme beliebiger NAS-/HA-Versionen oder Maschinenmodelle.

Die öffentliche Kopie wird separat mit **Platzhalter-Zugangsdaten und einem Test-Vertrauensanker** gebaut. Zertifikate, private Schlüssel und gebaute Images kommen nicht ins ZIP. Der ursprüngliche private Projektstand wird nicht überschrieben. Tatsächliche Ergebnisse stehen in `EXPORT_VALIDATION.md` im Paketstamm.

### Reproduzierbare lokale Tests

```sh
python3 tools/embed_web_ui.py
pio run
python3 test/run.py
```

Vorher eigene lokale Konfiguration/CA anlegen. Hosttests benötigen Clang mit Address-/UndefinedBehaviorSanitizer, Node.js, Ruby und Python 3; die JSON-C-Quellen stammen aus der installierten ESP-IDF-Abhängigkeit. Tests verwenden temporäre Verzeichnisse und Loopback-Testserver, keine produktiven NAS-Dateien. Für eigenes IDF `IDF_PATH`, für eigenen PlatformIO-Paketort `PLATFORMIO_CORE_DIR` setzen.

19 Gruppen prüfen unter anderem API-Authentifizierung, Bestätigungen, Intervall-/NVS-Persistenz, Reparaturübergabe, UI, JSON-/TLS-Transport, Archiv/Deduplizierung/Seiten, Pläne, SHA-256, Regeln, Metadaten und sichere SD-Bereinigung. Prüfsummen kritischer Test35-Funktionen werden gegen den bestätigten Kern geprüft; diese nicht bloss an Fehler anpassen. YAML-Syntaxprüfung ersetzt keine reale HA-Konfigurationsprüfung. Stromausfall-/USB-/SD-Verhalten muss am Gerät erneut getestet werden.

### Vor dem GitHub-Upload

Nur den entpackten öffentlichen Ordner veröffentlichen, nicht das private Arbeitsprojekt oder dessen Backup/Artifacts. Prüfen: `git status`, `git diff --cached` und keine echten Passwörter, Hostnamen, Pfade, PEM/CRT/KEY, State, Images oder privaten Logs. `.gitignore` wirkt nicht auf bereits getrackte Dateien. Das Paket enthält keine Git-Historie; einen neuen Repository-Stand erstellen.

Nichtkommerzielle Nutzung und Anpassung sind nach PolyForm Noncommercial 1.0.0 erlaubt, ohne Supportzusage; [NUTZUNG.md](../NUTZUNG.md). Drittanbieterhinweise bleiben erhalten. Nach eigener Prüfung kann der Betreiber einen 1.0.3-Tag und Release erstellen; diese Vorbereitung lädt nichts zu GitHub hoch.

---

## English

### Validated baseline and limits

Test35 was checked on the original hardware for changes, deletions, repair and fast unchanged checks. The operator reported the 1.0.0 website looks and works well. This is not full acceptance of arbitrary NAS/HA versions or embroidery-machine models.

The public copy is built separately using **placeholder credentials and a test trust anchor**. Certificates, private keys and configured images are excluded from the ZIP. The original private project is not overwritten. Actual results appear in root `EXPORT_VALIDATION.md`.

### Reproduce local tests

```sh
python3 tools/embed_web_ui.py
pio run
python3 test/run.py
```

Prepare local configuration/CA first. Host tests require Clang with Address/UndefinedBehaviorSanitizer, Node.js, Ruby and Python 3; JSON C sources come from the installed ESP-IDF dependency. Tests use temporary directories and loopback servers, not production NAS files. Use `IDF_PATH` for a custom IDF installation or `PLATFORMIO_CORE_DIR` for a custom PlatformIO package directory.

Nineteen groups cover API authentication, confirmations, interval/NVS persistence, repair handoff, UI, JSON/TLS transport, archive deduplication/pagination, plans, SHA-256, rules, metadata and safe SD cleanup. Critical Test35 functions are checked by hash against the accepted core; do not blindly update expected hashes to silence failures. YAML syntax checking is not real HA acceptance. Power-loss/USB/SD behavior still needs hardware testing.

### Before uploading to GitHub

Publish only the extracted public folder, not the private working project or its backup/artifacts. Review `git status` and `git diff --cached` for real passwords, hostnames, paths, PEM/CRT/KEY files, state, images and private logs. `.gitignore` does not affect already tracked files. No Git history is included; initialize a new repository state.

Noncommercial use and adaptation under PolyForm Noncommercial 1.0.0 are permitted, without a support commitment; see [NUTZUNG.md](../NUTZUNG.md). Preserve third-party notices. After review, the operator can create a 1.0.3 tag/release; this preparation uploads nothing to GitHub.
