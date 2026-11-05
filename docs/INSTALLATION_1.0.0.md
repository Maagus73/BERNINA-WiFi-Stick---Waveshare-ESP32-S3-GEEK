# Installation und Bedienung 1.0.0

Diese Version baut auf dem erfolgreichen Test35 auf. NAS-Planer, Ausschlussregeln, Reparatur, DMA-Transfer, Zertifikatsprüfung und bestätigte APP-/USB-Übergabe bleiben erhalten. Neu sind die vereinheitlichte Oberfläche, Konfiguration, API, HA-Anbindung und das Tagesprotokoll. Noch nicht geflasht und noch nicht auf echter Hardware abgenommen.

Einrichtung, Build und Updates: [INSTALLATION.md](INSTALLATION.md).

## Webseite

`http://IP-DES-STICKS/` öffnen. Im Router eine DHCP-Reservierung für den Stick setzen. Der Link in HA stammt zusätzlich aus der aktuell gemeldeten IP. Es bleibt bei lokalem HTTP für die Stick-Oberfläche; der NAS-Zugriff verwendet weiterhin HTTPS. Steuerpasswort im Heimnetz verwenden.

- Status zeigt Tätigkeit, Speicherbesitzer, WLAN, Laufzeit, Intervall, letzten Erfolg und den zuletzt bestätigten Bestand. Unbekannter/veralteter Bestand wird markiert; die Seite liest dafür keine SD-Verzeichnisse. Nach dem ersten erfolgreichen Lauf unter 1.0.0 sind die Zähler gespeichert.
- **APP übernehmen:** einen am Computer eingebundenen Datenträger zuerst auswerfen. Die technische USB-Abmeldung und bestätigte APP-Freigabe erfolgen wie bisher.
- **Jetzt abgleichen:** setzt bestätigtes APP und einen gültigen Zustand voraus. Automatik und normale Speicherübergabe folgen der Test35-Logik.
- **Automatik pausieren/fortsetzen:** wird dauerhaft gespeichert. Fortsetzen startet den nächsten Zeitabstand neu.
- **Spiegel reparieren:** exakten Text `DAS KANN LANGE DAUERN` eingeben. Nach sicherer APP-Übernahme werden vorhandene Inhalte geprüft, fehlende/geänderte Dateien übertragen und überzählige, nicht ausgeschlossene SD-Daten nach Gesamtprüfung bereinigt. Nur nach Erfolg wird USB wieder angemeldet. Ein Fehler hält die Sperre aufrecht.
- **USB anmelden:** nur ohne offene Journalsperre und nach gültiger APP-Übergabe.
- **Bildschirmschoner:** schaltet den bestehenden Matrix-Effekt. Während Arbeit hat die Fortschrittsanzeige Vorrang.

Unter Konfiguration Wert und Minuten/Stunden wählen, dann speichern. Bereich **1 Minute bis 7 Tage**, also maximal 10080 Minuten oder 168 Stunden. Wirksam ohne Neustart. Ein gesonderter Neustart ist im Leerlauf nach Bestätigung möglich. Intervall-Vorgabe und weitere Build-Parameter stehen in `include/sync_config.h`. Der bestehende Vollaufbau-Schwellwert **2500** wurde beibehalten; er wurde nicht stillschweigend auf 25 zurückgesetzt.

## Ereignisprotokoll

Unter Berichte Passwort eingeben und Tagesprotokoll laden. Scrollen, aktualisieren oder als Text herunterladen. Anzeigezeit ist **Europe/Zurich**, `HH:MM:SS`, einschliesslich Sommer-/Winterzeit. Der Kalendertag umfasst bei Zeitumstellung korrekt 23 beziehungsweise 25 Stunden.

Die sichtbare Historie ist nicht mehr auf 128 Einträge begrenzt: bestätigte Ereignisse werden im NAS-State archiviert und seitenweise geladen. Die Ansicht zeigt nur den aktuellen Tag. NAS-Aufbewahrung: drei Tage nach Empfang; pro Stick höchstens 50000 neue Ereignisse beziehungsweise 10 MiB Nachrichtentext innerhalb 24 Stunden. Grössere Mengen werden abgelehnt und als Archivproblem sichtbar.

Der ESP hält weiterhin einen begrenzten Offline-Puffer von **128 Ereignissen**. Bei Ausfall oder sehr langer Nicht-Erreichbarkeit können alte, noch unbestätigte Einträge verdrängt werden; die Seite zeigt die Anzahl. Ein Neustart verliert noch nicht archivierte RAM-Einträge. Ereignisse vor gültiger Uhrzeit stehen als „Zeit noch nicht synchronisiert“ und werden anhand ihres Empfangstags zugeordnet. Es ist kein garantiert lückenloses Audit-Log. Während eines laufenden Transfers zeigt ein Abruf zunächst den lokalen Puffer; nach Ende erneut laden, um das Archiv nachzuladen. Archivfehler sperren keine sonst erfolgreiche Spiegelung.

## Ausschlüsse und Home Assistant

`nas-excludes.txt` und `sd-excludes.txt` liegen im Containerprojekt und werden wie bisher eingebunden. Beispiel `/**/@eaDir/` ignoriert diese Verzeichnisse auf jeder Ebene. `*`/`?` gelten innerhalb eines Pfadteils, `**` als eigener Teil über beliebig viele Ebenen, abschliessendes `/` für Verzeichnisse samt Inhalt. Kommentare nur auf separaten Zeilen. BNCACHE bleibt immer reserviert. Regeln nicht während eines laufenden Plans ändern; danach neu planen beziehungsweise bewusst reparieren. Vorhandene geschützte SD-Pfade werden nicht gelöscht.

[HA-Anleitung](../integrations/home_assistant/README.md) erklärt Installation, Intervallsteuerung, HTTP-Link und Zustandsverlauf. Die Pakete werden hier nur vorbereitet; kein bestehendes HA-System wurde verändert.

## Abnahme nach dem Einspielen

1. Version und WLAN auf der einzelnen Startseite prüfen; vorhandenen Checkpoint behalten. Ohne NAS-Änderung abgleichen: kein erzwungener Vollaufbau.
2. Datei/Unterordner neu anlegen, Inhalt bei gleicher Länge ändern, umbenennen und löschen; jeweils kleine Delta-Übernahme und tatsächlichen SD-Inhalt prüfen.
3. Intervall/Einheit und Pause ändern, neu starten und gespeicherte Werte prüfen. Matrix während/nach Transfer prüfen.
4. Tagesprotokoll inklusive älterer Einträge laden; bei mehr als 128 Ereignissen Archiv prüfen. Kurzzeitige NAS-Unterbrechung und anschliessende Nachlieferung prüfen.
5. Reparatur nur bewusst mit Bestätigung testen; APP vorher und USB erst nach erfolgreichem Ende kontrollieren. HA-Paket zunächst mit „Konfiguration prüfen“ und danach allen gewünschten Tasten abnehmen.

Bei Fehlern Automatik pausieren, Ereignisse sichern, nicht durch Reset eine erfolgreiche Synchronisation vortäuschen. Private Rücksicherungen sind nicht Teil dieses Pakets.
