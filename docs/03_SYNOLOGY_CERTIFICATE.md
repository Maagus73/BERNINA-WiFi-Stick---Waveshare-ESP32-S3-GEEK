# Eigenes Zertifikat / Private certificate

[Deutsch](#deutsch) · [English](#english)

## Deutsch

Der ESP vertraut einem eingebetteten öffentlichen PEM-Zertifikat und prüft weiterhin Hostname und Gültigkeit. Er benötigt eine gültige Uhrzeit (NTP). Ein neuer HTTPS-Port verändert nicht den DNS-Namen im Zertifikat. Weder `curl -k` noch ausgeschaltete Hostnamenprüfung sind Teil dieser Lösung.

### Bestehendes Zertifikat weiterverwenden

In DSM unter Systemsteuerung → Sicherheit → Zertifikat das passende Zertifikat prüfen/exportieren. Ein Export kann **private Schlüssel enthalten**: diesen nicht in das Repository oder auf den ESP kopieren. Nur den öffentlichen CA-Vertrauensanker verwenden; bei bewusst selbstsigniertem Serverzertifikat entsprechend dessen öffentlichen Zertifikatteil. Die URL muss zu einem DNS-SAN des Zertifikats passen. Keine blosse Umbenennung einer CRT-Datei korrigiert einen falschen SAN.

### Alternative: eigene lokale CA und Serverzertifikat

OpenSSL auf dem Entwicklungsrechner verwenden. Zuerst `certificates/examples/server.cnf` auf den tatsächlich auflösbaren NAS-Namen ändern (CN und SAN); Beispiel `nas.home.arpa`. Die lokale CA nur einmal erstellen; für Erneuerung nicht versehentlich ersetzen. Folgende Befehle gelten **für eine neue CA**, vom Projektstamm aus:

```sh
mkdir -p certificates/local
chmod 700 certificates/local
openssl genrsa -out certificates/local/ca.key 3072
openssl req -new -x509 -sha256 -days 3650 \
  -key certificates/local/ca.key -out certificates/local/ca.crt \
  -config certificates/examples/ca.cnf
openssl genrsa -out certificates/local/server.key 2048
openssl req -new -key certificates/local/server.key \
  -out certificates/local/server.csr -config certificates/examples/server.cnf
openssl x509 -req -sha256 -days 825 \
  -in certificates/local/server.csr \
  -CA certificates/local/ca.crt -CAkey certificates/local/ca.key -CAcreateserial \
  -out certificates/local/server.crt \
  -extfile certificates/examples/server.cnf -extensions v3_server
openssl verify -CAfile certificates/local/ca.crt certificates/local/server.crt
chmod 600 certificates/local/*.key
```

Die CA-Schlüsseldatei bleibt separat und sicher beim Betreiber. In DSM ein Zertifikat **importieren**: `server.crt`, zugehöriger `server.key`, als Zwischen-/Kettenzertifikat `ca.crt`. Unter Zertifikateinstellungen dem Reverse-Proxy-Dienst dieses Zertifikat zuweisen. Den CA-Privatschlüssel nicht hochladen. DSM-Menübezeichnungen können je nach Version abweichen.

### Auf ESP und Rechner vertrauen

```sh
python3 tools/configure_local.py --ca certificates/local/ca.crt
```

Das Werkzeug erzeugt `include/synology_cert.h` und fehlende lokale Konfigurationsheader, ohne vorhandene Zugangsdaten zu überschreiben. Für einen absichtlichen späteren Zertifikatswechsel gibt es `--replace-certificate`. Danach neu bauen. Das Werkzeug prüft die PEM-Kodierung, ersetzt aber keine Prüfung der richtigen CA/Serverkette.

curl erhält `--cacert certificates/local/ca.crt`; der System-Zertifikatsspeicher muss dafür nicht verändert werden. Bei Erneuerung eines Serverzertifikats unter derselben vertrauenswürdigen CA ist normalerweise kein neuer ESP-Vertrauensanker nötig; bei CA-Wechsel dagegen schon. Gültigkeitszeitraum und DNS-Namen weiterhin prüfen.

Quellen: [DSM Zertifikate](https://kb.synology.com/en-us/DSM/help/DSM/AdminCenter/connection_certificate), [OpenSSL x509](https://docs.openssl.org/3.0/man1/openssl-x509/). Die konkrete lokale CA-Konfiguration ist ein Projektbeispiel.


### Lokale Variante – nicht abgenommen

CA und Serverzertifikat können mit denselben OpenSSL-Beispielen lokal erzeugt werden. Für einen Rechner z.B. CN/SAN `stickfiles.home.arpa` setzen und alle URLs/DNS entsprechend ändern. Serverzertifikat und Server-Schlüssel erhält dann der lokale HTTPS-Proxy, nicht DSM. Der ESP erhält unverändert nur den öffentlichen CA-Vertrauensanker. Windows/macOS-Zertifikatsspeicher allein konfigurieren nicht automatisch den ESP.

---

## English

The ESP trusts an embedded public PEM certificate and still verifies hostname and validity. A valid clock (NTP) is required. Changing the HTTPS port does not change the DNS identity. Neither `curl -k` nor disabling hostname verification is part of this setup.

### Reuse an existing certificate

Inspect/export the appropriate certificate in DSM Control Panel → Security → Certificate. Exports can contain **private keys**: never put those in the repository or on the ESP. Use only the public CA trust anchor; for an intentionally self-signed server, use its public certificate as appropriate. The URL must match a DNS SAN. Renaming a CRT file cannot correct a hostname mismatch.

### Alternative: local CA and server certificate

Use OpenSSL on the development computer. First edit CN and SAN in `certificates/examples/server.cnf` to match your resolvable NAS hostname, e.g. `nas.home.arpa`. Create the CA once and do not accidentally replace it during renewal. These commands create a **new CA**, from the project root:

```sh
mkdir -p certificates/local
chmod 700 certificates/local
openssl genrsa -out certificates/local/ca.key 3072
openssl req -new -x509 -sha256 -days 3650 \
  -key certificates/local/ca.key -out certificates/local/ca.crt \
  -config certificates/examples/ca.cnf
openssl genrsa -out certificates/local/server.key 2048
openssl req -new -key certificates/local/server.key \
  -out certificates/local/server.csr -config certificates/examples/server.cnf
openssl x509 -req -sha256 -days 825 \
  -in certificates/local/server.csr \
  -CA certificates/local/ca.crt -CAkey certificates/local/ca.key -CAcreateserial \
  -out certificates/local/server.crt \
  -extfile certificates/examples/server.cnf -extensions v3_server
openssl verify -CAfile certificates/local/ca.crt certificates/local/server.crt
chmod 600 certificates/local/*.key
```

Keep the CA private key separately and securely with the operator. Import `server.crt`, its `server.key`, and `ca.crt` as chain/intermediate certificate in DSM. Assign the certificate to the reverse-proxy service in certificate settings. Do not upload the CA private key. Menu labels may vary by DSM version.

### Trust on the ESP and computer

```sh
python3 tools/configure_local.py --ca certificates/local/ca.crt
```

The tool generates `include/synology_cert.h` and missing local configuration headers without overwriting existing credentials. Use `--replace-certificate` for an intentional later trust-anchor replacement, then rebuild. The tool checks PEM encoding; it does not establish that you chose the correct CA/server chain.

Use curl's `--cacert certificates/local/ca.crt` without modifying the system trust store. Renewing a server certificate under the same trusted CA normally does not require a new ESP trust anchor; replacing the CA does. Continue checking validity and DNS names.

Sources: [DSM certificates](https://kb.synology.com/en-us/DSM/help/DSM/AdminCenter/connection_certificate), [OpenSSL x509](https://docs.openssl.org/3.0/man1/openssl-x509/). This local CA configuration is a project example.

### Local variant — not accepted/tested

The same OpenSSL examples can create local CA/server certificates. For a computer, use e.g. CN/SAN `stickfiles.home.arpa` and update DNS/URLs consistently. Give server certificate/key to the local HTTPS proxy rather than DSM. The ESP still receives only the public CA trust anchor; changing Windows/macOS system trust alone does not configure the ESP.
