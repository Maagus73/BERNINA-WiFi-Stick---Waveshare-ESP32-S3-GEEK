# Verwendete Hardware

Waveshare ESP32-S3-GEEK, 16 MiB Flash, 2 MiB PSRAM, microSD über SPI und ST7789-Display. Die PlatformIO-Boarddefinition `esp32-s3-devkitc-1` ist die Build-Basis; tatsächliche Pinbelegung und Flashgrösse sind explizit konfiguriert.

| Anschluss | GPIO |
|---|---|
| SD SCK / MOSI / MISO / CS | 36 / 35 / 37 / 34 |
| LCD Backlight / DC / Reset | 7 / 8 / 9 |
| LCD CS / MOSI / CLK | 10 / 11 / 12 |

SD-SPI bleibt bei 20 MHz. Netzwerk-/SD-Puffer und Display-Implementierung folgen dem getesteten Test35-Stand; die erfolgreiche interne DMA-Pufferstrategie bleibt bestehen. Die LCD-Ausrichtung wurde nicht erneut verändert.

USB-Massenspeicher und ESP-Dateisystem dürfen nicht gleichzeitig auf die Karte zugreifen. Schreiben setzt bestätigtes APP voraus. Offene Journale sperren die USB-Anmeldung. Ein ausstehender Vorgang wird nicht allein durch einen UI-Knopf als erfolgreich erklärt.

Die bestehende Partitionstabelle `partitions_16mb.csv` bleibt erhalten. Für ein Update von Test35 nur das App-Image an 0x10000 verwenden und NVS nicht löschen. Private Referenzarchive sind nicht Bestandteil dieses Pakets.
