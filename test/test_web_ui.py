#!/usr/bin/env python3
from pathlib import Path
import json,subprocess,tempfile
root=Path(__file__).resolve().parents[1];js=(root/'tools/web-ui/sync.js').read_text();html=(root/'tools/web-ui/index.html').read_text()
for src,target in [('sync.js','sync_page_script.inc'),('index.html','sync_page_html.inc')]:assert ''.join(json.loads(x) for x in (root/'src'/target).read_text().splitlines())==(root/'tools/web-ui'/src).read_text()
assert 'Version 1.0.3' in html and 'Test35' not in html and 'Reset' not in html
harness=r'''
const assert=require('node:assert/strict');let calls=[],confirmOK=true,phrase='',statusFixture={busy:false,locked:false,wifi_connected:true,storage:'APP',uptime_seconds:90000,interval_seconds:600,interval_value:10,interval_unit:'minutes',paused:false,inventory_known:true,inventory_valid:true,files:10,directories:2,last_success:1700000000,last_result:'completed',progress:0,files_done:0,files_total:0,event_archive_online:true,events_dropped:0,screensaver:false,next_check_seconds:60};
const nodes=new Map();function elem(id){if(!nodes.has(id))nodes.set(id,{value:'',textContent:'',checked:false,open:false,hidden:false,disabled:false,addEventListener(){},setAttribute(){},hasAttribute(){return false;}});return nodes.get(id);}
const document={getElementById:elem,activeElement:null,querySelectorAll:()=>[],createElement:()=>({click(){}})};
function confirm(){return confirmOK;}function prompt(){return phrase;}function addEventListener(){}function setInterval(){return 1;}function clearInterval(){}
async function fetch(url,opts){calls.push({url,opts});let body=url==='/api/status'?statusFixture:url.startsWith('/api/events')?{day:'2026-10-26',events:[{boot:'b',seq:1,ts:1700000000,message:'12:00:00: hello'}],local:[],next:1,more:false,archive_available:true,local_next:1,local_more:false,boot:'b'}:{message:'accepted'};return {ok:true,json:async()=>body};}
'''
checks=r'''
(async()=>{await new Promise(setImmediate);assert(calls.every(x=>x.url==='/api/status'));assert(elem('network').textContent.includes('1 Tage'));assert(elem('timer').textContent.includes('10 Minuten'));
 elem('password').value='local-only';phrase='wrong';let before=calls.length;await action('repair');assert.equal(calls.length,before);phrase='DAS KANN LANGE DAUERN';await action('repair');let repair=calls.find(x=>x.url==='/api/action');assert.equal(repair.opts.headers['X-Bernina-Confirm'],phrase);assert.equal(repair.opts.body,'local-only');assert.equal(repair.opts.headers['X-Bernina-Action'],'repair');
 elem('interval').value='2';elem('unit').value='hours';await saveConfig();const cfg=calls.find(x=>x.url==='/api/config');assert.equal(cfg.opts.headers['X-Bernina-Interval'],'2');assert.equal(cfg.opts.headers['X-Bernina-Unit'],'hours');
 before=calls.length;elem('unit').value='hours';elem('interval').value='169';await saveConfig();assert.equal(calls.length,before);
 elem('retryEnabled').checked=true;['30','60','120','300'].forEach((v,n)=>elem('retry'+n).value=v);await saveRetry();const retryCall=calls.find(x=>x.opts?.headers?.['X-Bernina-Retry']);assert.equal(retryCall.opts.headers['X-Bernina-Retry-Delays'],'30,60,120,300');assert.equal(retryCall.opts.body,'local-only');before=calls.length;elem('retry1').value='2';await saveRetry();assert.equal(calls.length,before);
 await loadEvents();await loadEvents();assert.equal(eventRows.size,1);assert(elem('events').textContent.includes('hello'));assert(calls.filter(x=>x.url.startsWith('/api/events')).every(x=>x.opts.headers['X-Bernina-Key']==='local-only'));
 assert(!calls.some(x=>x.url.includes('local-only')));console.log('PASS actual one-page JS: read-only polling, status units, exact repair phrase, shared protected actions/config, invalid config rejection, event pagination/dedup and password outside URL');})().catch(e=>{console.error(e);process.exitCode=1});
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.js';p.write_text(harness+js+checks);subprocess.run(['node',str(p)],check=True)
