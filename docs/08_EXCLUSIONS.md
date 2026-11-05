# NAS-/SD-Ausschlüsse / NAS and SD exclusions

[Deutsch](#deutsch) · [English](#english)

## Deutsch

Im NAS-Projekt liegen zwei eingebundene Dateien. NAS-Ausschlüsse verhindern Übernahme aus der Quelle und schützen entsprechende vorhandene SD-Pfade vor Löschung. SD-Ausschlüsse schützen lokale SD-Pfade vor Schreiben und Löschen. Besteht ein benötigter NAS-Pfad unter einem SD-Ausschluss, wird die Planung blockiert; denselben Pfad dann bewusst auf NAS ausschliessen oder den SD-Schutz ändern.

### Syntax

| Regel | Bedeutung |
|---|---|
| `/**/@eaDir/` | Sämtliche @eaDir-Ordner, auch in der Wurzel, samt Inhalt |
| `/Privat/` | Nur der Wurzelordner Privat samt Inhalt |
| `/**/Privat/` | Passende Ordner in jeder Tiefe |
| `/Entwuerfe/*.tmp` | Passende Dateien direkt in Entwuerfe |
| `/**/*.tmp` | Passende Dateien in jeder Tiefe |
| `/**/.DS_Store` | macOS-Metadaten-Dateien in jeder Tiefe |

Führendes `/` ist Pflicht. Abschliessendes `/` bedeutet Ordner; ohne dieses werden nur Dateien erfasst. `*` und `?` bleiben in einem Pfadteil, `**` muss ein ganzer Pfadteil sein und kann auch null Ebenen erfassen. ASCII-Gross-/Kleinschreibung wird ignoriert. Keine Negation, `[abc]`, Backslashes, `..` oder Include-Regeln.

Maximal 32 Regeln mit je 127 ASCII-Zeichen; Datei maximal 8192 UTF-8-Bytes. Kommentare mit `#` auf **eigenen Zeilen**; hinter einer aktiven Regel wird keine Erklärung angehängt. Die Vorlagen enthalten Deutsch, danach Englisch, danach die gemeinsamen aktiven Regeln.

Vorgaben: NAS ignoriert rekursiv @eaDir, .DS_Store und `._*`; SD zusätzlich die Wurzelordner .Spotlight-V100, .Trashes, .fseventsd und .TemporaryItems. **BNCACHE bleibt fest reserviert**, auch ohne Regel. Ausschliessen bedeutet erhalten/ignorieren, nicht vorhandene Metadaten aufräumen.

Regeln nur bei pausierter, nicht laufender Spiegelung ändern. Änderungen werden vom Dienst geprüft; vorhandene Pläne können absichtlich verworfen werden. Anschliessend neu planen beziehungsweise bewusst reparieren, nicht laufend am selben Paket weiterarbeiten. Eine geänderte Regel ist kein NAS-Dateiinhalt und kein vorgetäuschter unveränderter Sync.


### Lokale Variante – nicht abgenommen

Die Syntax bleibt identisch. Beispiel für eine lokale Quelle: `/Privat/` verhindert Übernahme dieses Wurzelordners; `/**/.DS_Store` ignoriert passende Mac-Metadaten. „NAS-Ausschlüsse“ bezeichnet auch lokal weiterhin die Quellseite. Die Dateien liegen im lokalen Dienstprojekt statt auf der Synology.

---

## English

Two mounted files live in the NAS project. NAS exclusions omit source data and protect matching existing SD paths from deletion. SD exclusions protect local paths from writes/deletes. If required NAS data conflicts with an SD exclusion, planning is blocked; deliberately exclude the same NAS path or adjust the SD protection.

### Syntax

| Pattern | Meaning |
|---|---|
| `/**/@eaDir/` | Every @eaDir directory, including at root, with all contents |
| `/Private/` | Only the root Private directory and descendants |
| `/**/Private/` | Matching directories at any depth |
| `/Drafts/*.tmp` | Matching files directly under Drafts |
| `/**/*.tmp` | Matching files at any depth |
| `/**/.DS_Store` | macOS metadata files at any depth |

A leading `/` is required. A trailing `/` selects directories; without it, only files match. `*` and `?` stay within one component; `**` must be a complete component and also matches zero levels. ASCII matching is case-insensitive. No negation, `[abc]`, backslashes, `..` or inclusion rules.

Maximum 32 rules, 127 ASCII characters each; file maximum 8192 UTF-8 bytes. Put `#` comments on **separate lines**, never after an active pattern. Templates contain German comments, then English comments, then shared active rules.

Defaults: NAS recursively ignores @eaDir, .DS_Store and `._*`; SD additionally protects root .Spotlight-V100, .Trashes, .fseventsd and .TemporaryItems. **BNCACHE is always reserved**, even without an explicit rule. Exclusion means preserve/ignore, not clean up existing metadata.

Change rules only while mirroring is paused and idle. The service checks rule contents and may intentionally invalidate old plans. Replan or deliberately repair afterwards; do not continue an old transfer against changed policy. A rule change is not evidence of an unchanged synchronization.

### Local variant — not accepted/tested

Syntax is unchanged. For example, `/Private/` omits that root source directory and `/**/.DS_Store` ignores matching Mac metadata. “NAS exclusions” still means source-side policy for a local deployment. Files live in the local service project instead of Synology.
