#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
code=r'''
#include "remote_plan.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned seen;static bool reject;
static bool emit(void *v,char action,const char *path,const dav_entry_t *e){(void)v;assert(strchr("DFVXRK",action)&&*path);if(action=='F')assert(e->size==3);seen++;return !reject;}
static const char gen[]="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static bool parse(const char *text,unsigned chunk){remote_plan_t *p=calloc(1,sizeof(*p));remote_plan_init(p,emit,NULL);bool ok=true;size_t length=strlen(text);for(size_t i=0;i<length&&ok;i+=chunk){size_t n=length-i;if(n>chunk)n=chunk;ok=remote_plan_feed(p,text+i,n);}ok=ok&&remote_plan_finish(p);free(p);return ok;}
int main(void){char body[2048];snprintf(body,sizeof(body),"BPLAN2\t%s\tDELTA\t1\t3\t5\t2\nX\t0\t%s\told\nR\t0\t-\tfolder\nD\t0\t-\tnew\nF\t3\t%s\tnew/%%C3%%A4.txt\nV\t3\t%s\tsame\nEND\t5\t%s\n",gen,gen,gen,gen,gen);
 for(unsigned chunk=1;chunk<4096;chunk=chunk*2+1){seen=0;assert(parse(body,chunk)&&seen==5);}
 char bad[2048];strcpy(bad,body);bad[strlen(bad)-1]=0;assert(!parse(bad,7));strcpy(bad,body);char *end=strstr(bad,"END\t5");end[4]='4';assert(!parse(bad,23));strcpy(bad,body);strstr(bad,"DELTA\t1\t3")[6]='2';assert(!parse(bad,31));
 snprintf(bad,sizeof(bad),"BPLAN2\t%s\tFULL\t0\t0\t1\t1\nX\t0\t%s\told\nEND\t1\t%s\n",gen,gen,gen);assert(!parse(bad,11));
 snprintf(bad,sizeof(bad),"BPLAN2\t%s\tDELTA\t1\t3\t2\t1\nF\t3\t%s\tnew\nD\t0\t-\tfolder\nEND\t2\t%s\n",gen,gen,gen);assert(!parse(bad,7));
 snprintf(bad,sizeof(bad),"BPLAN2\t%s\tDELTA\t0\t0\t1\t0\nK\t0\t-\tfolder\nEND\t1\t%s\n",gen,gen);assert(parse(bad,5));
 snprintf(bad,sizeof(bad),"BPLAN2\t%s\tFULL\t0\t0\t1\t1\nK\t0\t-\tfolder\nEND\t1\t%s\n",gen,gen);assert(!parse(bad,5));
 reject=true;assert(!parse(body,1));puts("PASS Test25 actual streaming parser: all chunk boundaries, full/delta and verify-only entries, UTF8 paths, callback denial, count/size/truncation and operation-order rejection");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code);subprocess.run(['clang','-std=gnu11','-fsanitize=address,undefined','-I'+str(root/'src'),str(p/'test.c')]+[str(root/'src'/n) for n in ['remote_plan.c','nas_index.c','dav_unicode.c','dav_listing.c']]+['-o',str(p/'run')],check=True);subprocess.run([str(p/'run')],check=True)
