#!/usr/bin/env python3
"""Actual framed receiver, SHA256 and filesystem under sanitizers and I/O faults."""
from pathlib import Path
import tempfile,subprocess,hashlib
from urllib.parse import quote
root=Path(__file__).resolve().parents[1]
code=r'''
#include "bulk_copy.h"
#include <CommonCrypto/CommonDigest.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
static int mode,faults;static FILE *input;static size_t offset;static uint64_t ticks;static CC_SHA256_CTX sha;
static bool owned(void *v){(void)v;return mode!=1&&!(mode==15&&offset>300);}
static int read_stream(void *v,unsigned char *p,size_t n){(void)v;if(mode==2&&offset>150)return -1;if(mode==17&&n>1)n=1;else if(n>37)n=37;size_t got=fread(p,1,n,input);offset+=got;return ferror(input)?-1:(int)got;}
static bool complete(void *v){(void)v;if(mode==16)return false;return fgetc(input)==EOF&&!ferror(input);}
static bool start(void *v){(void)v;return CC_SHA256_Init(&sha);}
static bool update(void *v,const unsigned char *p,size_t n){(void)v;return CC_SHA256_Update(&sha,p,(CC_LONG)n);}
static bool finish(void *v,unsigned char *p){(void)v;return CC_SHA256_Final(p,&sha);}
static void fault(void *v){(void)v;faults++;}
static uint64_t clock_us(void *v){(void)v;return ticks+=10;}
static ssize_t real_write_fault(int fd,const void *p,size_t n){if(mode==10){errno=EIO;return -1;}return write(fd,p,n);}
static int real_fsync_fault(int fd){if(mode==11){errno=EIO;return -1;}return fsync(fd);}
static int real_close_fault(int fd){int r=close(fd);if(mode==12){errno=EIO;return -1;}return r;}
static int real_rename_fault(const char *a,const char *b){if(mode==13){errno=EIO;return -1;}return rename(a,b);}
static int real_unlink_fault(const char *p){if(mode==14){errno=EIO;return -1;}return unlink(p);}
#define write real_write_fault
#define fsync real_fsync_fault
#define close real_close_fault
#define rename real_rename_fault
#define unlink real_unlink_fault
#include "BULK_SOURCE"
#undef write
#undef fsync
#undef close
#undef rename
#undef unlink
int main(int argc,char **argv){assert(argc==2);char path[2048];snprintf(path,sizeof(path),"%s/meta",argv[1]);FILE *meta=fopen(path,"r");assert(meta);bulk_entry_t entries[6];char names[6][1024],hash[65];for(int i=0;i<6;i++){unsigned long long size;assert(fscanf(meta,"%llu\t%64s\t%1023[^\n]\n",&size,hash,names[i])==3);entries[i].path=names[i];entries[i].size=size;for(int j=0;j<32;j++){unsigned n;assert(sscanf(hash+j*2,"%2x",&n)==1);entries[i].hash[j]=n;}}assert(!fclose(meta));
 const char *files[]={"wire","wire","wire","truncated","badhash","badname","badfooter","badstart","badcount","trailing","wire","wire","wire","wire","wire","wire","wire","wire"};
 for(mode=0;mode<18;mode++){char folder[2048];snprintf(folder,sizeof(folder),"%s/case%d",argv[1],mode);assert(!mkdir(folder,0700)&&!chdir(folder));assert(!mkdir("sd",0700)&&!mkdir("sd/sub",0700)&&!mkdir("cache",0700));snprintf(path,sizeof(path),"%s/%s",argv[1],files[mode]);input=fopen(path,"rb");assert(input);offset=0;faults=0;bulk_ops_t ops={.owned=owned,.read=read_stream,.complete=complete,.hash_start=start,.hash_update=update,.hash_finish=finish,.fault=fault,.clock_us=clock_us,.allocate=malloc,.allocate_write=malloc,.release=free};bulk_result_t r;bulk_receive("sd","cache/temp","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",0,entries,6,&ops,&r);assert(r.success==(mode==0||mode==17));if(mode>=10&&mode<=14)assert(faults>0);else assert(!faults);
  if(r.success){assert(r.completed==6&&r.read_us>0&&r.write_us>0&&r.finish_us>0);assert(r.write_calls==9&&r.write_bytes==r.bytes&&r.read_calls>r.write_calls);for(int i=0;i<6;i++){snprintf(path,sizeof(path),"sd/%s",names[i]);FILE *f=fopen(path,"rb");assert(f);CC_SHA256_CTX check;CC_SHA256_Init(&check);unsigned char bytes[8192],digest[32];size_t total=0,n;while((n=fread(bytes,1,sizeof(bytes),f))){CC_SHA256_Update(&check,bytes,(CC_LONG)n);total+=n;}CC_SHA256_Final(digest,&check);assert(total==entries[i].size&&!memcmp(digest,entries[i].hash,32));assert(!fclose(f));}}
  assert(!fclose(input));assert(!chdir(argv[1]));
 }
 puts("PASS Test26 actual receiver: binary/UTF8/empty/buffer-boundary files, fragmented stream, exact plan/start/count/frame/hash/footer/HTTP completion, corrupt/truncated/trailing rejection, APP loss and write/flush/close/rename/unlink fault latching, measured phase counters");}
'''.replace('BULK_SOURCE',str(root/'src/bulk_copy.c'))
with tempfile.TemporaryDirectory(prefix='bernina26-bulk-') as d:
 p=Path(d);plan='a'*64;names=['zero','one','sub/ä name','below','exact','above'];sizes=[0,1,16383,16384,16385,60000];wire=f'BUNDLE1\t{plan}\t0\t6\n'.encode();meta=[];first_payload=None
 for name,size in zip(names,sizes):
  data=(bytes(range(256))*((size+255)//256))[:size];digest=hashlib.sha256(data).hexdigest();meta.append(f'{size}\t{digest}\t{name}\n');wire+=f'FILE\t{size}\t{digest}\t{quote(name,safe="/")}\n'.encode()
  if size and first_payload is None:first_payload=len(wire)
  wire+=data+b'\n'
 wire+=f'END\t{plan}\t6\n'.encode();(p/'meta').write_text(''.join(meta));(p/'wire').write_bytes(wire)
 bad=bytearray(wire);bad[first_payload]^=1;(p/'badhash').write_bytes(bad)
 for name,data in [('truncated',wire[:-8]),('badname',wire.replace(b'\tzero\n',b'\tevil\n',1)),('badfooter',wire[:-2]+b'7\n'),('badstart',wire.replace(f'{plan}\t0\t6'.encode(),f'{plan}\t1\t6'.encode(),1)),('badcount',wire.replace(f'{plan}\t0\t6'.encode(),f'{plan}\t0\t7'.encode(),1)),('trailing',wire+b'extra')]: (p/name).write_bytes(data)
 (p/'test.c').write_text(code);subprocess.run(['clang','-std=gnu11','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(root/'src'),str(p/'test.c')]+[str(root/'src'/n) for n in ['nas_index.c','dav_listing.c','dav_unicode.c']]+['-o',str(p/'run')],check=True);subprocess.run([str(p/'run'),d],check=True)
