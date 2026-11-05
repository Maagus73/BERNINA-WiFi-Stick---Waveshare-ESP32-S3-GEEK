#!/usr/bin/env python3
"""Actual clear engine: protected ancestors never rmdir; other failures reported."""
from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parents[2]
from support.filesystem_fixture import source as original_source
namespace={'source': original_source}
source=namespace['source'].replace('return !strcmp(p,"keep")','return strstr(p,"@eaDir")||!strcmp(p,"keep")')
source=source.replace('int main(int argc',r'''
static int failures;
static void failure(void *v,const char *path,const char *op,int code){(void)v;assert(strstr(path,"broken")&&strstr(op,"entfernen")&&code==EIO);failures++;}
int guarded_rmdir(const char *p){assert(!strstr(p,"protected"));if(strstr(p,"broken")){errno=EIO;return -1;}return rmdir(p);}
int main(int argc''').replace('#include <unistd.h>','#include <unistd.h>\n#include <errno.h>')
source=source.replace(' puts("PASS Test24.2',r'''
 setup("excluded-parent");assert(!mkdir("sd/protected",0700)&&!mkdir("sd/protected/deep",0700)&&!mkdir("sd/protected/deep/@eaDir",0700));put("sd/protected/deep/@eaDir/thumb","keep");put("sd/protected/ordinary","remove");assert(!mkdir("sd/disposable",0700));put("sd/disposable/data","remove");f=(fixture){0};o=options(&f);r=(fast_sync_result_t){0};assert(fast_sync_clear("sd",&o,&r));assert(!access("sd/protected/deep/@eaDir/thumb",F_OK));assert(access("sd/protected/ordinary",F_OK)&&access("sd/disposable",F_OK));assert(!chdir(".."));
 setup("real-failure");assert(!mkdir("sd/broken",0700));f=(fixture){0};o=options(&f);o.failure=failure;r=(fast_sync_result_t){0};assert(!fast_sync_clear("sd",&o,&r)&&r.error&&failures==1);assert(!chdir(".."));
 puts("PASS Test29 protected ancestors retained without rmdir, ordinary siblings removed, real rmdir failure includes operation/path/errno");
 puts("PASS Test24.2''')
