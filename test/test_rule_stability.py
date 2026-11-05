#!/usr/bin/env python3
"""Real server: unchanged rule text preserves pinned plans despite metadata changes."""
from support.nas_fixture import *
import io,json
from contextlib import redirect_stdout
class RuleStabilityTest(ServerTest):
 def configured(self):
  self.nas=self.root/'nas.txt';self.sd=self.root/'sd.txt';self.nas.write_text('/hidden/\n');self.sd.write_text('/.Trashes/\n')
  self.server.index.rule_paths=(self.nas,self.sd)
  status,self.headers,_=self.request(path='.bernina-plan-v2');self.assertEqual(status,200)
  self.pin={'X-Bernina-Generation':self.headers['X-Bernina-Generation'],'X-Bernina-Resource-Check':'1'}
  self.bundle=f".bernina-bundle-v1/{self.headers['X-Bernina-Plan']}/0/1"
 def test_metadata_only_keeps_plan_and_source_without_refresh(self):
  self.configured();idx=self.server.index;generation=idx.token;policy=idx.nas_rules.digest+':'+idx.sd_rules.digest;refreshes=idx.refreshes
  for kind in ('touch','mode','replace','rewrite'):
   if kind=='touch':st=self.nas.stat();os.utime(self.nas,ns=(st.st_atime_ns,st.st_mtime_ns+1000000000))
   if kind=='mode':self.nas.chmod(0o600 if self.nas.stat().st_mode&0o077 else 0o644)
   if kind=='replace':new=self.root/'new';new.write_bytes(self.nas.read_bytes());new.replace(self.nas)
   if kind=='rewrite':self.nas.write_bytes(self.nas.read_bytes())
   log=io.StringIO()
   with redirect_stdout(log):status,h,body=self.request(path=self.bundle,headers=self.pin)
   self.assertEqual(status,200);self.assertIn('rules_metadata_only',log.getvalue());self.assertEqual(idx.token,generation);self.assertEqual(idx.nas_rules.digest+':'+idx.sd_rules.digest,policy);self.assertEqual(idx.refreshes,refreshes);self.assertEqual(h['X-Bernina-Plan'],self.headers['X-Bernina-Plan'])
 def test_real_change_never_installs_new_rules_under_old_plan(self):
  self.configured();idx=self.server.index;old_signatures=idx.rule_signatures;old_digest=idx.sd_rules.digest
  self.sd.write_text('/.Trashes/\n/new/\n')
  log=io.StringIO()
  with redirect_stdout(log):status,h,_=self.request(path=self.bundle,headers=self.pin)
  self.assertEqual(status,503);self.assertEqual(h['X-Bernina-Error'],'exclusion_config_changed');self.assertIn('rules_content_changed',log.getvalue());self.assertIn('"rules": "sd"',log.getvalue());self.assertEqual(idx.sd_rules.digest,old_digest);self.assertEqual(idx.rule_signatures,old_signatures)
  status,new,_=self.request();self.assertEqual(status,200);self.assertNotEqual(new['X-Bernina-Generation'],self.headers['X-Bernina-Generation'])
 def test_unstable_and_invalid_rules_fail_closed(self):
  self.configured();idx=self.server.index;original=idx.rule_stats();changed=[list(x) for x in original];changed[0][4]+=1;changed=tuple(changed)
  with patch.object(idx,'rule_stats',side_effect=[original,changed]*3):
   with self.assertRaises(m.Unready):idx.read_stable_rules()
  self.assertEqual(idx.rule_signatures,original)
  for value in (b'/../bad\n',b'#'+b'a'*8192,b'\xff'):
   self.nas.write_bytes(value);self.assertEqual(self.request(path=self.bundle,headers=self.pin)[0],503)
  self.nas.unlink();self.assertEqual(self.request(path=self.bundle,headers=self.pin)[0],503)
 def test_user_threshold_2500_and_upper_bound(self):
  self.configured()
  self.assertEqual(self.request(path='.bernina-plan-v2',headers={'X-Bernina-Threshold':'2500'})[0],200)
  self.assertEqual(self.request(path='.bernina-plan-v2',headers={'X-Bernina-Threshold':'20001'})[0],400)
 def test_crlf_matches_historical_policy_digest(self):
  self.configured();digest=self.server.index.nas_rules.digest;self.nas.write_bytes(b'/hidden/\r\n');self.assertEqual(self.request(path=self.bundle,headers=self.pin)[0],200);self.assertEqual(self.server.index.nas_rules.digest,digest)
if __name__=='__main__':unittest.main()
