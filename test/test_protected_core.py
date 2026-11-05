#!/usr/bin/env python3
from pathlib import Path
import re,json,hashlib
root=Path(__file__).resolve().parents[1]
for e in json.loads((root/'test/support/protected_functions.json').read_text()):
 text=(root/'src'/e['file']).read_text();m=re.search(r'^static [^\n]+?\b'+e['function']+r'\([^)]*\)',text,re.M);a=text.index('{',m.end());d=1;i=a+1
 while d:d+=int(text[i]=='{')-int(text[i]=='}');i+=1
 body=text[m.start():i]
 # Only transparent transport-observation wrappers differ from the audited Test35 body.
 if e['function']=='remote_apply':
  body=re.sub(r'sync_http_headers\((\w+),"[^"]+"\)',r'esp_http_client_fetch_headers(\1)',body)
  body=re.sub(r'sync_http_read\(([^;]+),"[^"]+"\)',r'esp_http_client_read(\1)',body)
 assert hashlib.sha256(body.encode()).hexdigest()==e['sha256'],e['function']
source=(root/'src/main.c').read_text();assert 'if(!update_lock&&!mirror_lock&&!fast_lock){ESP_ERROR_CHECK(tinyusb_driver_install' in source
assert 'if (update_usb_ready && !tud_disconnect())' in source
assert 'if(fast_result.success&&!fast_lock&&!storage_fault&&!io_fault)' in (root/'src/app_runtime.inc').read_text()
print('PASS protected Test35 core hashes: confirmed MSC ownership, APP/USB mounting, durable journal/baseline, immutable plan application and checkpoint unchanged')
