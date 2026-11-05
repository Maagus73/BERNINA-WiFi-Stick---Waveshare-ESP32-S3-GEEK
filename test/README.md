# Aktuelle Regressionstests

`python3 test/run.py` führt die freigegebenen Testgruppen für Version 1.0.0 aus. Benötigt Python 3, Clang/ASan/UBSan, Node.js und Ruby. Testserver binden nur temporär an Loopback und arbeiten in temporären Verzeichnissen; kein produktiver NAS-Zugriff.

`support/` enthält benötigte isolierte Fixtures und Prüfsummen kritischer Test35-Funktionen. Historische private Testskripte sind nicht Bestandteil dieses Pakets. Änderungen an Sicherheitsfunktionen erfordern eine bewusste Prüfung; Prüfsummen nicht bloss an einen fehlgeschlagenen Test anpassen.
