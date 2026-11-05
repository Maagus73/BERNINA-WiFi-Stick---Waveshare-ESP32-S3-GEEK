#!/usr/bin/env python3
"""Actual HTTP stream logs correlate with ESP response IDs without leaking paths."""
import json
from support.bundle_fixture import *
from contextlib import redirect_stdout
class DiagnosticTest(BundleServerTest):
 def test_correlated_complete_and_changed_source(self):
  h,files=self.prepare();pid=h['X-Bernina-Plan'];pin={'X-Bernina-Generation':h['X-Bernina-Generation'],'X-Bernina-Resource-Check':'1'}
  log=io.StringIO()
  with redirect_stdout(log):
   status,headers,data=self.request(path=f'.bernina-bundle-v1/{pid}/0/1',headers=pin)
  rows=[json.loads(x) for x in log.getvalue().splitlines() if x.startswith('{')]
  self.assertEqual(status,200);self.assertEqual([x['event'] for x in rows],['bundle_start','bundle_complete'])
  for row in rows:self.assertEqual(row['transfer'],headers['X-Bernina-Transfer']);self.assertNotIn('path',row)
  self.assertEqual(rows[-1]['wire_bytes'],len(data));self.assertEqual(rows[-1]['expected_wire'],len(data));self.assertEqual(rows[-1]['completed_files'],1)
  (self.source/files[0][0]).write_bytes(b'external-change')
  log=io.StringIO()
  with redirect_stdout(log):
   conn=http.client.HTTPConnection(*self.server.server_address,timeout=10);conn.request('GET',f'/bernina/.bernina-bundle-v1/{pid}/0/1',headers={'Authorization':self.auth,**pin});response=conn.getresponse()
   with self.assertRaises(http.client.IncompleteRead):response.read()
   conn.close()
  rows=[json.loads(x) for x in log.getvalue().splitlines() if x.startswith('{')]
  self.assertEqual(rows[-1]['event'],'bundle_aborted');self.assertEqual(rows[-1]['code'],'file_changed');self.assertLess(rows[-1]['wire_bytes'],rows[-1]['expected_wire']);self.assertEqual(rows[-1]['completed_files'],0)
if __name__=='__main__':unittest.main()
