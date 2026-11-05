#!/usr/bin/env python3
"""Run the real UI through pending requests, partial pages, failures and rollover."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];js=(root/'tools/web-ui/sync.js').read_text()
harness=r'''
const assert=require('node:assert/strict');let calls=[],downloads=[],blobs=[],responder,tick;const writes=new Map();
const nodes=new Map();function elem(id){if(!nodes.has(id))nodes.set(id,{value:'',_text:'',get textContent(){return this._text},set textContent(v){this._text=v;writes.set(id,(writes.get(id)||0)+1)},checked:false,open:false,hidden:false,disabled:false,scrollTop:0,addEventListener(){},setAttribute(k,v){this[k]=v},hasAttribute(){return false;}});return nodes.get(id);}
const document={getElementById:elem,activeElement:null,querySelectorAll:()=>[elem('appAction'),elem('pauseAction')],createElement:()=>({click(){downloads.push(this.download)}})};
const URL={createObjectURL(b){blobs.push(b);return 'blob:test'},revokeObjectURL(){}};
function addEventListener(){}function setInterval(fn){tick=fn;return 1;}function clearInterval(){}
const statusFixture={busy:false,locked:false,wifi_connected:true,storage:'USB',uptime_seconds:86519,interval_seconds:600,interval_value:10,interval_unit:'minutes',paused:false,inventory_known:true,inventory_valid:true,files:3754,directories:268,last_success:1793089812,last_result:'completed',progress:0,files_done:0,files_total:0,event_archive_online:true,events_dropped:0,screensaver:true,next_check_seconds:60};
async function fetch(url,options){calls.push({url,options});return {ok:true,json:async()=>url==='/api/status'?statusFixture:url==='/api/action'?{message:'accepted'}:await responder(url,options)};}
function row(seq,boot='b'){return {boot,seq,ts:1793089812+seq,message:'09:30:12: event '+seq};}
function page(seq,extra={}){return {day:'2026-10-27',boot:'b',events:[row(seq)],local:[],next:seq,more:false,local_next:seq,local_more:false,archive_available:true,...extra};}
function resetLogs(){lastDay='';lastBoot='';cursor=localCursor=0;eventRows.clear();eventLoaded=eventComplete=eventLoading=false;logControls();}
'''
checks=r'''
(async()=>{
 await new Promise(setImmediate);downloadEvents();assert.equal(downloads.length,0);assert(elem('logDownload').disabled);
 elem('password').value='test-only';let release;responder=()=>new Promise(r=>release=r);const pending=loadEvents();
 await new Promise(setImmediate);assert(eventLoading);assert(elem('logRefresh').disabled);assert(elem('logDownload').disabled);assert.match(elem('logInfo').textContent,/geladen/);
 assert(!elem('appAction').disabled&&!elem('pauseAction').disabled);await post('/api/action',{'X-Bernina-Action':'pause'});assert(!elem('appAction').disabled&&eventLoading);const count=calls.length;await loadEvents();assert.equal(calls.length,count);release(page(1));await pending;assert(!eventLoading&&!elem('logRefresh').disabled);assert(!elem('logDownload').disabled);assert.equal(eventRows.size,1);
 downloadEvents();assert.match(await blobs.at(-1).text(),/Tagesprotokoll/);assert(!downloads.at(-1).includes('teilliste'));
 responder=()=>{throw new TypeError('Load failed')};await loadEvents();assert.equal(eventRows.size,1);assert(!eventLoading);assert.match(elem('logInfo').textContent,/unterbrochen.*Bisher/);downloadEvents();assert.match(await blobs.at(-1).text(),/UNVOLLSTÄNDIGER ZWISCHENSTAND/);assert.match(downloads.at(-1),/teilliste/);
 responder=()=>{throw new DOMException('aborted','AbortError')};await loadEvents();assert.match(elem('logInfo').textContent,/Zeitlimit/);assert.equal(eventRows.size,1);
 responder=()=>page(2);await loadEvents();assert(eventComplete);assert.equal(eventRows.size,2);
 // First response after midnight is discarded and re-requested from both zero cursors.
 const start=calls.length;let n=0;responder=()=>page(++n===1?99:3,{day:'2026-10-28'});await loadEvents();assert.equal(eventRows.size,1);assert(eventRows.has('b:3'));assert.match(calls[start+1].url,/after=0&local_after=0/);
 // Boot changed: discard the stale local-cursor page and load the new boot from zero.
 n=0;responder=()=>page(3,{day:'2026-10-28',boot:'newboot',events:[],local:[row(0,'newboot')],local_next:1});await loadEvents();assert(eventRows.has('newboot:0'));assert.equal(localCursor,1);
 const before=cursor;responder=()=>page(before,{day:'2026-10-28',boot:'newboot',more:true});await loadEvents();assert.equal(cursor,before);assert.match(elem('logInfo').textContent,/unvollständig/);
 resetLogs();let requests=0;responder=()=>page(++requests,{more:true});await loadEvents();assert.equal(requests,32);assert(!eventComplete);assert.match(elem('logInfo').textContent,/noch unvollständig/);assert.equal(eventRows.size,32);
 responder=()=>page(33);await loadEvents();assert(eventComplete);assert.equal(eventRows.size,33);
 resetLogs();responder=()=>page(0,{events:[],archive_available:false,busy:false});await loadEvents();assert(eventLoaded);assert(elem('logDownload').disabled);const saved=downloads.length;downloadEvents();assert.equal(downloads.length,saved);
 // Open reports must not turn status polling into log polling or rewrite the displayed log.
 elem('reports').open=true;const logRequests=calls.filter(x=>x.url.startsWith('/api/events')).length;const textWrites=writes.get('events')||0;
 for(let i=0;i<4;i++)await tick();
 assert.equal(calls.filter(x=>x.url.startsWith('/api/events')).length,logRequests);assert.equal(writes.get('events')||0,textWrites);assert(!elem('appAction').disabled);
 const statusWrites=writes.get('state');await refreshStatus();assert.equal(writes.get('state'),statusWrites);
 // Unknown boot timestamps stay honest and at the start of their boot sequence.
 resetLogs();eventRows.set('b:2',{...row(2),message:'09:30:12: spaeter'});eventRows.set('b:0',{...row(0),ts:0,received:1793089812,message:'--:--:--: Start'});eventRows.set('b:1',{...row(1),ts:0,message:'--:--:--: NTP gestartet',order:1793089813});
 renderEvents();const lines=elem('events').textContent.split('\n');assert.match(lines[0],/^Empfang .*Start vor Zeitsynchronisierung.* Start$/);assert.match(lines[1],/^Start vor Zeitsynchronisierung.*NTP gestartet$/);assert.match(lines[2],/^\d{2}:\d{2}:\d{2} spaeter$/);
 elem('events').scrollTop=123;const rendered=writes.get('events');renderEvents();assert.equal(writes.get('events'),rendered);assert.equal(elem('events').scrollTop,123);
 console.log('PASS open report stays quiet, action buttons remain available while loading, existing DOM/scroll preserved, startup times labeled honestly and ordered before later boot events');
 console.log('PASS real log UI: pending feedback, no placeholder download, failure retains rows, marked partial export, timeout, day reset, reboot cursor, no-progress rejection and bounded pagination');
})().catch(e=>{console.error(e);process.exitCode=1});
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'ui.js';p.write_text(harness+js+checks);subprocess.run(['node',str(p)],check=True)
