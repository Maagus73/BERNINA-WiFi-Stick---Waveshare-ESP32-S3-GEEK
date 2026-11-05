#!/usr/bin/env python3
from support.nas_fixture import *
import json,time,sqlite3
class ArchiveTest(ServerTest):
 def send_events(self,events,auth=True):
  conn=http.client.HTTPConnection(*self.server.server_address,timeout=10);body=json.dumps({'device':'001122334455','boot':'0123456789abcdef','events':events});conn.request('POST','/bernina/.bernina-events-v1',body,{'Authorization':self.auth if auth else '', 'Content-Type':'application/json'});r=conn.getresponse();result=r.status,r.read();conn.close();return result
 def get_events(self,after=0,limit=100):
  now=int(time.time());return self.request(path=f'.bernina-events-v1?device=001122334455&from={now-86400}&to={now+1}&after={after}&limit={limit}')
 def test_archive_dedup_paging_and_no_source_access(self):
  rows=[{'seq':i,'ts':int(time.time()),'message':'Prüfung '+str(i)} for i in range(16)]
  with patch.object(self.server.index,'refresh',side_effect=AssertionError('no scan for event archive')):
   self.assertEqual(self.send_events(rows)[0],200);self.assertEqual(self.send_events(rows)[0],200)
   status,_,body=self.get_events(limit=5);self.assertEqual(status,200);first=json.loads(body);self.assertEqual(len(first['events']),5);self.assertTrue(first['more']);second=json.loads(self.get_events(after=first['next'])[2]);self.assertEqual(len(second['events']),11);self.assertFalse(second['more'])
  self.assertEqual((self.source/'ä file.txt').read_bytes(),b'abc');self.assertEqual(self.server.index.refreshes,0)
 def test_validation_auth_and_unavailable_time(self):
  self.assertEqual(self.send_events([{'seq':0,'ts':0,'message':'Start ohne Uhrzeit'}])[0],200)
  self.assertEqual(self.send_events([{'seq':1,'ts':0,'message':'x'}],auth=False)[0],401)
  for e in [{'seq':-1,'ts':0,'message':'x'},{'seq':1,'ts':int(time.time())+10000,'message':'x'},{'seq':1,'ts':0,'message':'x'*513}]:self.assertEqual(self.send_events([e])[0],400)
  self.assertEqual(self.send_events([])[0],400);data=json.loads(self.get_events()[2]);self.assertEqual(data['events'][0]['ts'],0)
 def test_current_day_uses_event_time_and_persisted_state(self):
  now=int(time.time());old={'seq':1,'ts':now-2*86400,'message':'older day'};current={'seq':2,'ts':now,'message':'current day'};self.assertEqual(self.send_events([old,current])[0],200)
  self.assertEqual([e['message'] for e in json.loads(self.get_events()[2])['events']],['current day'])
  self.server.index.db.commit();db=sqlite3.connect(self.root/'state/index.sqlite');self.assertEqual(db.execute('SELECT count(*) FROM events').fetchone()[0],2);db.close()
if __name__=='__main__':unittest.main()
