#pragma once
// DE: tools/configure_local.py erzeugt synology_cert.h aus dem oeffentlichen PEM-Vertrauensanker.
// EN: tools/configure_local.py generates synology_cert.h from your public PEM trust anchor.
static const char synology_cert_start[] =
 "-----BEGIN CERTIFICATE-----\n"
 "YOUR_PUBLIC_CA_CERTIFICATE_BASE64\n"
 "-----END CERTIFICATE-----\n";
