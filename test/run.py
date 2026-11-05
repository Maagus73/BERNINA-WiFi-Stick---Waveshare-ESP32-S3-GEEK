#!/usr/bin/env python3
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[1]
tests=['test_recovery.py','test_package_config.py','test_log_transport.py','test_event_pages.py','test_event_ui.py','test_protected_core.py','test_settings.py','test_control.py','test_persistence.py','test_web_ui.py','test_transfer.py','test_plan_parser.py','test_exclusions.py','test_repair.py','test_reconcile.py','test_rule_stability.py','test_event_transport.py','test_file_metadata.py','test_event_archive.py']
for name in tests:
 print('RUN '+name,flush=True);subprocess.run([sys.executable,str(root/'test'/name)],cwd=root,check=True)
print('PASS Version 1.0.3: all '+str(len(tests))+' test groups',flush=True)
