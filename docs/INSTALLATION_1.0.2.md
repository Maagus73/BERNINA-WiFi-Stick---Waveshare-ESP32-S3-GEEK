# BERNINA WiFi Stick 1.0.2 – ruhige Berichtsanzeige / Stable report display

## Deutsch

1.0.1 lud bei geöffnetem Bericht das Protokoll mit jedem Statusintervall erneut. Dabei wurden die Aktionsbuttons kurzzeitig gesperrt und grosse Textlisten wiederholt neu eingesetzt. 1.0.2 trennt die beiden Abläufe:

- Status wird weiterhin automatisch aktualisiert; Protokoll nur beim Aufklappen oder per „Aktualisieren“.
- Protokollladen deaktiviert nur dessen Lade-/Downloadbutton. Die übrigen Aktionen bleiben nach den normalen Geräte-/Vorgangsvoraussetzungen bedienbar. Die technische APP-/USB-/Journalprüfung bleibt verbindlich.
- Unveränderte Texte werden nicht erneut in die Seite geschrieben. Die Scrollposition der Protokollliste bleibt erhalten.
- Startmeldungen ohne gültige Uhrzeit werden nicht hinter spätere Meldungen desselben Starts sortiert. Wo das Archiv einen Empfangszeitpunkt liefert, steht ausdrücklich „Empfang HH:MM:SS (Start vor Zeitsynchronisierung)“. Ohne diesen Zeitpunkt steht „Start vor Zeitsynchronisierung (Ereigniszeit unbekannt)“. Es wird keine genaue Ereigniszeit erfunden. Einträge nach erfolgreicher Zeitsynchronisierung behalten HH:MM:SS.

Die kleinen Seiten und begrenzten Puffer aus 1.0.1 bleiben erhalten. Ein langer Bericht kann beim ersten Öffnen mehrere Seiten brauchen. Danach bleibt er stabil, bis erneut geladen wird.

## Update

Installation: eigenes Image bauen; bestehende kompatible Geräte nur per App-Update an 0x10000 aktualisieren. Siehe [Build](06_FIRMWARE_BUILD.md).

Prüfung: Bericht aufklappen, laden lassen und geöffnet stehen lassen. Status darf sich aktualisieren, Bericht und Aktionsbuttons dürfen dabei nicht periodisch blinken. Über „Aktualisieren“ sollen neue Zeilen nachladbar sein. Bei einem wirklichen Sync oder Steuerauftrag bleiben die gewohnten Gerätesperren zulässig. Startmeldungen und spätere Uhrzeiten unterscheiden.

---

## English

1.0.1 reloaded events on every status interval while reports were open. It briefly disabled action buttons and repeatedly replaced large text lists. 1.0.2 separates these operations:

- Status still updates automatically; logs load only when opening reports or pressing refresh.
- Log loading disables only its refresh/download buttons. Other actions follow normal device/work prerequisites. Technical APP/USB/journal protection remains mandatory.
- Unchanged text is not rewritten, and log scroll position is preserved.
- Startup entries without a valid clock are no longer sorted after later entries from the same boot. An archive receipt timestamp is explicitly labeled “Empfang HH:MM:SS (Start vor Zeitsynchronisierung)”; without one, startup event time remains explicitly unknown. No exact event timestamp is invented. Events after clock synchronization retain HH:MM:SS.

Bounded pages/buffers from 1.0.1 remain. Initial loading of a long report can require several pages. Afterwards it remains stable until explicitly refreshed.

## Upgrade

Installation: build your own image; on existing compatible devices, update only the app at 0x10000. See [build](06_FIRMWARE_BUILD.md).

Acceptance: open reports, let loading finish and leave them open. Status can change, but reports/action buttons should not blink periodically. Refresh should retrieve new entries. Normal device locks during real sync/control operations remain valid. Distinguish startup entries from subsequently synchronized timestamps.
