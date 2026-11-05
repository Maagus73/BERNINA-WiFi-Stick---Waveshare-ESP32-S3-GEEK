#!/usr/bin/env python3
"""DE: Lokale Konfiguration vorbereiten. EN: Prepare local configuration."""
from pathlib import Path
import argparse,json,re,ssl
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description='Prepare local settings and embed a PUBLIC PEM trust anchor; never a private key.')
parser.add_argument('--ca',required=True,type=Path,help='Public PEM CA/trust certificate')
parser.add_argument('--replace-certificate',action='store_true',help='Explicitly replace an existing local certificate header')
args=parser.parse_args();text=args.ca.read_text(encoding='ascii')
if 'PRIVATE KEY' in text:parser.error('Private keys must never be embedded in firmware.')
blocks=re.findall(r'-----BEGIN CERTIFICATE-----[\s\S]*?-----END CERTIFICATE-----',text)
if not blocks:parser.error('No PEM certificate found.')
try:
 for block in blocks:ssl.PEM_cert_to_DER_cert(block)
except ValueError:parser.error('Invalid PEM certificate encoding.')
header=root/'include/synology_cert.h'
if header.exists() and not args.replace_certificate:parser.error('Local certificate already exists; use --replace-certificate only for an intentional update.')
header.write_text('#pragma once\n// Local public trust anchor; generated, gitignored.\nstatic const char synology_cert_start[] =\n'+''.join('    '+json.dumps(line+'\n')+'\n' for block in blocks for line in block.splitlines())+';\n')
for stem in ('secrets','nas_index_config'):
 target=root/'include'/f'{stem}.h'
 if not target.exists():target.write_bytes((root/'include'/f'{stem}.example.h').read_bytes())
print('DE: Lokale Header erstellt. secrets.h und nas_index_config.h jetzt anpassen.')
print('EN: Local headers prepared. Edit secrets.h and nas_index_config.h before building.')
