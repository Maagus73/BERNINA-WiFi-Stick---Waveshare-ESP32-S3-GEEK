#!/usr/bin/env python3
from support.bundle_fixture import *
import io,json,os
from contextlib import redirect_stdout
class DriftTest(BundleServerTest):
 def test_metadata_drift_verified_and_content_change_denied(self):
  h,files=self.prepare();pid=h['X-Bernina-Plan'];pin={'X-Bernina-Generation':h['X-Bernina-Generation'],'X-Bernina-Resource-Check':'1'};path=self.source/files[0][0];raw=path.read_bytes();st=path.stat();os.utime(path,ns=(st.st_atime_ns,st.st_mtime_ns+1000000))
  log=io.StringIO()
  with redirect_stdout(log):status,_,body=self.request(path=f'.bernina-bundle-v1/{pid}/0/1',headers=pin)
  self.assertEqual(status,200);self.assertIn(b'END\t',body);self.assertIn('file_metadata_verified',log.getvalue());self.assertEqual(raw,path.read_bytes())
  path.write_bytes(b'z'*len(raw))
  conn=http.client.HTTPConnection(*self.server.server_address,timeout=10);conn.request('GET',f'/bernina/.bernina-bundle-v1/{pid}/0/1',headers={'Authorization':self.auth,**pin});reply=conn.getresponse()
  with self.assertRaises(http.client.IncompleteRead):reply.read()
  conn.close()
if __name__=='__main__':unittest.main()
