#!/usr/bin/env python3
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]
for source,target in [('tools/web-ui/index.html','src/sync_page_html.inc'),('tools/web-ui/sync.js','src/sync_page_script.inc')]:
 text=(root/source).read_text();(root/target).write_text(''.join(json.dumps(line,ensure_ascii=False)+'\n' for line in text.splitlines(keepends=True)))
