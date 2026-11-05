#!/usr/bin/env python3
from support.nas_fixture import *
from urllib.parse import unquote
import io
class BundleServerTest(ServerTest):
 def prepare(self):
  status,h,plan=self.request(path='.bernina-plan-v2');self.assertEqual(status,200);self.assertEqual(h['X-Bernina-Bundle'],'1')
  files=[]
  for line in plan.decode().splitlines()[1:-1]:
   row=line.split('\t')
   if row[0]=='F':files.append((unquote(row[3]),int(row[1]),row[2],row[3]))
  return h,files
 def test_single_stream_binary_unicode_empty_and_slicing(self):
  (self.source/'empty-file').write_bytes(b'');(self.source/'binary').write_bytes(bytes(range(256))*1000)
  for i in range(70):(self.source/f'f{i:03}').write_bytes(b'a\n\x00FILE\tEND\n'+bytes([i]))
  h,files=self.prepare();pid=h['X-Bernina-Plan'];pin={'X-Bernina-Generation':h['X-Bernina-Generation'],'X-Bernina-Resource-Check':'1'}
  before={p:p.read_bytes() for p in self.source.iterdir() if p.is_file()}
  for start,count in [(0,64),(64,len(files)-64)]:
   status,ch,body=self.request(path=f'.bernina-bundle-v1/{pid}/{start}/{count}',headers=pin);self.assertEqual(status,200);self.assertEqual(ch.get('Connection'),'close');self.assertEqual(int(ch['Content-Length']),len(body));stream=io.BytesIO(body)
   self.assertEqual(stream.readline(),f'BUNDLE1\t{pid}\t{start}\t{count}\n'.encode())
   for path,size,digest,encoded in files[start:start+count]:
    self.assertEqual(stream.readline(),f'FILE\t{size}\t{digest}\t{encoded}\n'.encode());data=stream.read(size);self.assertEqual(data,before[self.source/path]);self.assertEqual(hashlib.sha256(data).hexdigest(),digest);self.assertEqual(stream.read(1),b'\n')
   self.assertEqual(stream.readline(),f'END\t{pid}\t{count}\n'.encode());self.assertEqual(stream.read(),b'')
  self.assertEqual(before,{p:p.read_bytes() for p in before})
  self.assertEqual(self.request(path=f'.bernina-bundle-v1/{pid}/0/129',headers=pin)[0],400)
  self.assertEqual(self.request(path=f'.bernina-bundle-v1/{pid}/999/1',headers=pin)[0],416)
 def test_changed_source_terminates_without_footer_or_second_response(self):
  h,files=self.prepare();pid=h['X-Bernina-Plan'];(self.source/files[0][0]).write_bytes(b'changed outside')
  conn=http.client.HTTPConnection(*self.server.server_address,timeout=10);conn.request('GET',f'/bernina/.bernina-bundle-v1/{pid}/0/1',headers={'Authorization':self.auth,'X-Bernina-Generation':h['X-Bernina-Generation'],'X-Bernina-Resource-Check':'1'})
  response=conn.getresponse();self.assertEqual(response.status,200)
  with self.assertRaises(http.client.IncompleteRead) as raised:response.read()
  self.assertNotIn(b'HTTP/1.1 503',raised.exception.partial);self.assertNotIn(f'END\t{pid}'.encode(),raised.exception.partial);conn.close()
