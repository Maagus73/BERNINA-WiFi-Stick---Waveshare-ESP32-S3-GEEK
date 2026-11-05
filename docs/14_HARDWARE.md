# Hardware

[Deutsch](#deutsch) · [English](#english)

## Deutsch

Bestätigte Basis ist der Waveshare ESP32-S3-GEEK mit 16 MiB Flash, 2 MiB Quad-PSRAM, microSD über SPI und ST7789-Display. Die PlatformIO-Boardkennung `esp32-s3-devkitc-1` ist die Build-Basis, nicht die Behauptung identischer Verdrahtung beliebiger DevKits.

| Connection | GPIO |
|---|---|
| SD SCK / MOSI / MISO / CS | 36 / 35 / 37 / 34 |
| LCD backlight / DC / reset | 7 / 8 / 9 |
| LCD CS / MOSI / clock | 10 / 11 / 12 |

SD läuft mit 20 MHz SPI. Interne DMA-Puffer sind für den erprobten schnellen Schreibpfad wesentlich. LCD-Ausrichtung, Fortschritt und Matrix stammen aus dem getesteten Stand. Andere Hardware benötigt angepasste Pins/Speicher-/USB-Prüfung; nur die Boardkennung auszutauschen reicht nicht.

USB-Hostzugriff ist lesend. ESP-Schreiben setzt bestätigtes APP voraus. Ein offenes Journal sperrt USB. PSRAM ist kein dauerhafter Zustand, NVS und SD-Cache haben andere Aufgaben. Keine gleichzeitigen Direktzugriffe auf FAT erzwingen. Partitionstabelle und APP-Adresse aus dem Projekt beibehalten.

---

## English

The accepted hardware is a Waveshare ESP32-S3-GEEK with 16 MiB flash, 2 MiB Quad PSRAM, SPI microSD and ST7789 display. PlatformIO's `esp32-s3-devkitc-1` identifier is the build base, not a claim that arbitrary DevKits share this wiring.

| Connection | GPIO |
|---|---|
| SD SCK / MOSI / MISO / CS | 36 / 35 / 37 / 34 |
| LCD backlight / DC / reset | 7 / 8 / 9 |
| LCD CS / MOSI / clock | 10 / 11 / 12 |

SD uses 20-MHz SPI. Internal DMA buffers are important for the tested faster write path. LCD orientation, progress and Matrix follow the accepted implementation. Other hardware requires pin, memory and USB validation; changing the board ID alone is insufficient.

USB host access is read-only. ESP writes require confirmed APP ownership. An open journal blocks USB. PSRAM is not durable state; NVS and SD cache serve different purposes. Never force simultaneous FAT access. Preserve the project's partition layout and app address.
