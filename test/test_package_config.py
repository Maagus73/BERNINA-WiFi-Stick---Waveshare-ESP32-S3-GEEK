#!/usr/bin/env python3
"""Parse deployment YAML and prevent accidentally publishing a stale web bundle."""
from pathlib import Path
import subprocess,hashlib
root=Path(__file__).resolve().parents[1]
paths=sorted((root/'integrations/home_assistant').glob('*.yaml'))+sorted((root/'tools/nas-indexdienst').glob('*.yaml'))
subprocess.run(['ruby','-ryaml','-e','ARGV.each { |p| YAML.parse_file(p) }',*map(str,paths)],check=True)
embedded=[root/'src/sync_page_html.inc',root/'src/sync_page_script.inc']
before=[hashlib.sha256(p.read_bytes()).digest() for p in embedded]
subprocess.run(['python3',str(root/'tools/embed_web_ui.py')],cwd=root,check=True)
assert before==[hashlib.sha256(p.read_bytes()).digest() for p in embedded],'Run tools/embed_web_ui.py before packaging'
assert (root/'version.txt').read_text().strip()=='1.0.3'
print('PASS deployment YAML syntax and generated web assets match source')
