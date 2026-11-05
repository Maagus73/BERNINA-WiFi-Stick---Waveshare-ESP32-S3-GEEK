#!/usr/bin/env python3
"""Actual repair comparison and bundle queue; skip maps retain F catalog ordinals."""
from pathlib import Path
import subprocess,tempfile
from support import transfer_fixture as fixture
root=Path(__file__).resolve().parents[1]
stubs=fixture.stubs[:fixture.stubs.index('static void makebody(')]
main=r'''
static void makebody(void){int n=sprintf(body,"BPLAN2\t%s\tFULL\t3\t9\t4\t3\nD\t0\t-\tsub\n",nas_index_generation);for(int i=0;i<3;i++)n+=sprintf(body+n,"F\t3\tba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\tsub/file%d\n",i);n+=sprintf(body+n,"END\t4\t%s\n",nas_index_generation);body_size=n;hexhash(body,body_size,plan_id);}
int main(int argc,char **argv){assert(argc==2&&!chdir(argv[1]));memset(nas_index_generation,'a',64);memset(sync_policy.source,'b',64);memset(sync_policy.policy,'c',129);sync_policy.policy[64]=':';makebody();
 for(unsigned mask=0;mask<8;mask++){char folder[32];sprintf(folder,"case%u",mask);assert(!mkdir(folder,0700)&&!chdir(folder));assert(!mkdir("sd",0700)&&!mkdir("sd/BNCACHE",0700)&&!mkdir("sd/sub",0700));unsigned correct=0;
 for(unsigned i=0;i<3;i++){if(mask&(1U<<i))correct++;if((mask&(1U<<i))||i!=1){char path[64];sprintf(path,"sd/sub/file%u",i);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(mask&(1U<<i)?"abc":"bad",1,3,f)==3&&!fclose(f));}}
 mode=0;bundle_requests=requests=copies=directory_reports=0;io_fault=storage_fault=false;fast_result=(fast_sync_result_t){0};remote_repair=true;repair_verified=0;
 char id[65];uint64_t length,files,bytes,differences;bool full;assert(remote_validate(false,id,&length,&files,&bytes,&full,&differences)&&full&&repair_inventory.keys);fast_result.files=files;fast_result.bytes=bytes;
 assert(remote_apply(id,length));assert(repair_verified==correct&&fast_result.completed==3&&fast_result.received==9&&!io_fault&&!storage_fault);assert(!copies&&!active_clients);if(mask==7)assert(!bundle_requests);else assert(bundle_requests);
 assert(remote_key(&repair_inventory,"SUB",true,false));assert(remote_key(&repair_inventory,"SUB/FILE0",false,false));assert(!remote_key(&repair_inventory,"EXTRA",false,false));
 for(unsigned i=0;i<3;i++){char path[64];sprintf(path,"sd/sub/file%u",i);FILE *f=fopen(path,"rb");char b[4]={0};assert(f&&fread(b,1,3,f)==3&&!strcmp(b,"abc")&&!fclose(f));}
 fast_release(repair_inventory.keys);memset(&repair_inventory,0,sizeof(repair_inventory));remote_repair=false;assert(!chdir(".."));}
 puts("PASS Test35 actual repair adapter: all eight skip/download masks, same-size different content, missing files, SHA-confirmed reuse, no duplicate counters, exact noncontiguous bundle ordinals, retained canonical inventory and all-matching zero-download case");}
'''
with tempfile.TemporaryDirectory(prefix='bernina35-') as d:
 p=Path(d);(p/'test.c').write_text(fixture.prefix+fixture.code+stubs+main);subprocess.run(['clang','-std=gnu11','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(root/'src'),str(p/'test.c')]+[str(root/'src'/n) for n in ['bulk_copy.c','remote_plan.c','nas_index.c','dav_listing.c','dav_unicode.c']]+['-o',str(p/'run')],check=True);subprocess.run([str(p/'run'),d],check=True)
