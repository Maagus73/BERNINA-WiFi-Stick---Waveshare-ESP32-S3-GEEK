#!/usr/bin/env python3
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'src/app_runtime.inc').read_text()
def fn(name):
 m=re.search(r'^static [^\n]+?\b'+name+r'\([^)]*\)',s,re.M);a=s.index('{',m.end());d=1;i=a+1
 while d:d+=int(s[i]=='{')-int(s[i]=='}');i+=1
 return s[m.start():i]+'\n'
typecode=s[:s.index('static app_saved_t')]
code=r'''
#include "app_settings.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <assert.h>
#include <stdio.h>
#define ESP_OK 0
#define NVS_READWRITE 1
#define NVS_READONLY 2
#define SYNC_CHECK_INTERVAL_SECONDS 600
#define SYNC_AUTOMATIC_ENABLED 1
#define portMAX_DELAY 0
typedef int nvs_handle_t;typedef int esp_err_t;
'''+typecode+r'''
static app_saved_t app_saved,stored,pending;static bool exists,fail_commit,fast_paused,app_auto_paused,app_interval_hours;static uint32_t app_interval_seconds,app_interval_value;static int state_lock;
static bool app_meta_known=true;static uint64_t app_meta_files=12,app_meta_directories=3;
static int nvs_open(const char *n,int mode,int *h){assert(!strcmp(n,"bernina_ui"));if(mode==2&&!exists)return -1;*h=1;return 0;}
static int nvs_set_blob(int h,const char *k,const void *p,size_t n){assert(h==1&&!strcmp(k,"settings")&&n==sizeof(pending));memcpy(&pending,p,n);return 0;}
static int nvs_commit(int h){assert(h==1);if(fail_commit)return -1;stored=pending;exists=true;return 0;}
static int nvs_get_blob(int h,const char *k,void *p,size_t *n){assert(h==1&&!strcmp(k,"settings")&&*n==sizeof(stored));memcpy(p,&stored,*n);return 0;}
static void nvs_close(int h){assert(h==1);}
static void xSemaphoreTake(int a,int b){(void)a;(void)b;}static void xSemaphoreGive(int a){(void)a;}static void event_log(const char *m){assert(m);}
'''
checks=r'''
int main(void){app_config_boot();assert(app_interval_seconds==600&&app_interval_value==10&&!app_interval_hours);app_saved.value=2;app_saved.hours=1;app_saved.paused=1;assert(app_save());memset(&app_saved,0,sizeof(app_saved));app_config_boot();assert(app_interval_seconds==7200&&app_interval_hours&&fast_paused&&app_auto_paused);app_sync_finished(true);assert(stored.known&&stored.files==12&&stored.dirs==3&&stored.last_success>1735689600&&stored.result==1);app_config_boot();assert(app_saved.files==12);fail_commit=true;app_saved.value=5;assert(!app_save());fail_commit=false;app_config_boot();assert(app_saved.value==2);stored.version=99;app_config_boot();assert(app_interval_seconds==600);puts("PASS actual NVS persistence: namespace isolation, validated boot defaults, interval/unit/pause roundtrip, inventory/last-success, commit failure and incompatible record rejection");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code+fn('app_save')+fn('app_config_boot')+fn('app_sync_finished')+checks);subprocess.run(['clang','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'src'),str(p/'test.c'),str(root/'src/app_settings.c'),'-o',str(p/'run')],check=True);subprocess.run([str(p/'run')],check=True)
