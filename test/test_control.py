#!/usr/bin/env python3
"""Run actual API control and repair orchestration against controlled SDK/NVS."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1];text=(root/'src/app_runtime.inc').read_text()
def extract(name):
 m=re.search(r'^static [^\n]+?\b'+name+r'\([^)]*\)',text,re.M);assert m,name
 start=m.start();a=text.index('{',m.end());depth=1;i=a+1
 while depth:
  if text[i]=='{':depth+=1
  if text[i]=='}':depth-=1
  i+=1
 return text[start:i]+'\n'
prefix=r'''
#include "app_settings.h"
#include "sync_retry.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
typedef int esp_err_t;typedef int command_t;typedef struct{const char *uri;int method;}httpd_req_t;
#define HTTP_GET 0
#define ESP_OK 0
#define pdTRUE 1
#define portMAX_DELAY 0
enum{CMD_NONE,CMD_DISCONNECT,CMD_FAST_RUN,CMD_CONNECT,CMD_REPAIR_FLOW,CMD_APP_REBOOT};
static int state_lock,active_command,queued,waiting;static void *command_queue=(void *)1;
static bool fast_busy,update_pending,mirror_pending,comparison_running,app_archive_busy,app_control_busy;
static bool storage_fault,io_fault,update_lock,mirror_lock,fast_lock,owned=true,persist=true,authorized=true,fast_paused,fast_return_usb,app_auto_paused;
static unsigned usb_test_phase=2;static uint32_t app_interval_seconds=600,app_interval_value=10;static bool app_interval_hours;static int64_t fast_next_us;
typedef struct{uint32_t value,hours,paused;}app_saved_t;static app_saved_t app_saved={10,0,0};
static struct{bool success;}fast_result;
static const char *action,*phrase,*value,*unit,*message,*retry,*delays;static int status;
static void xSemaphoreTake(int a,int b){(void)a;(void)b;}static void xSemaphoreGive(int a){(void)a;}
static int uxQueueMessagesWaiting(void *q){(void)q;return waiting;}
static int xQueueSend(void *q,command_t *c,int timeout){(void)q;(void)timeout;queued=*c;return 1;}
static int64_t esp_timer_get_time(void){return 1000000;}
static void recovery_resume(void){}
static bool recovery_set_config(const sync_retry_config_t *c){return sync_retry_config_valid(c)&&persist;}
static size_t httpd_req_get_hdr_value_len(httpd_req_t *r,const char *k){(void)r;return !strcmp(k,"X-Bernina-Retry")&&retry?strlen(retry):!strcmp(k,"X-Bernina-Interval")&&value?strlen(value):0;}
static bool app_save(void){return persist;}
static bool app_access_allowed(void){return owned;}
static bool control_authenticated(httpd_req_t *r){(void)r;return authorized;}
static int display_ui_set_matrix(bool enabled){(void)enabled;return 0;}
static void event_log(const char *s){assert(s);}
static int httpd_req_get_hdr_value_str(httpd_req_t *r,const char *key,char *out,size_t n){(void)r;const char *s=!strcmp(key,"X-Bernina-Action")?action:!strcmp(key,"X-Bernina-Confirm")?phrase:!strcmp(key,"X-Bernina-Retry")?retry:!strcmp(key,"X-Bernina-Retry-Delays")?delays:!strcmp(key,"X-Bernina-Interval")?value:unit;if(!s)return -1;snprintf(out,n,"%s",s);return 0;}
static esp_err_t app_message(httpd_req_t *r,const char *s,const char *m){(void)r;message=m;status=s?atoi(s):200;return status;}
static esp_err_t app_status(httpd_req_t *r){(void)r;return 200;}static esp_err_t app_events(httpd_req_t *r){(void)r;return 200;}
static bool handoff=true,repair_ok=true;static unsigned repairs;static bool handoff_failure_recorded;
static void app_sync_finished(bool ok){handoff_failure_recorded=!ok;}
static bool perform_app_handoff(void){return handoff;}
static void fast_repair_worker(void){repairs++;fast_result.success=repair_ok;fast_lock=!repair_ok;}
'''
checks=r'''
static void run(const char *a){action=a;queued=0;status=0;message=NULL;httpd_req_t r={.uri="/api/action",.method=1};app_api(&r);assert(!app_control_busy);}
int main(void){phrase="";run("app");assert(status==202&&queued==CMD_DISCONNECT);authorized=false;run("repair");assert(status==403&&!queued);authorized=true;
 run("repair");assert(status==409&&!queued);phrase="DAS KANN LANGE DAUERN";run("repair");assert(status==202&&queued==CMD_REPAIR_FLOW);
 fast_lock=true;run("sync");assert(status==409&&!queued);run("usb");assert(status==409&&!queued);run("repair");assert(status==202&&queued==CMD_REPAIR_FLOW);fast_lock=false;
 fast_busy=true;run("repair");assert(status==409&&!queued);fast_busy=false;storage_fault=true;run("app");assert(status==409);storage_fault=false;owned=false;run("sync");assert(status==409);owned=true;
 run("pause");assert(status==200&&app_auto_paused&&fast_paused);run("resume");assert(status==200&&!app_auto_paused&&!fast_paused);persist=false;run("pause");assert(status==500&&!app_saved.paused);persist=true;
 httpd_req_t config={.uri="/api/config",.method=1};value="2";unit="hours";app_api(&config);assert(status==200&&app_interval_seconds==7200&&app_interval_hours);value="169";app_api(&config);assert(status==400&&app_interval_seconds==7200);value="5";unit="minutes";persist=false;app_api(&config);assert(status==500&&app_interval_seconds==7200);persist=true;
 value=NULL;retry="1";delays="30,60,120,300";app_api(&config);assert(status==200);delays="30,20,120,300";app_api(&config);assert(status==400);delays="30,60,120,300";persist=false;app_api(&config);assert(status==500);persist=true;retry="2";app_api(&config);assert(status==400);retry=NULL;
 handoff=false;queued=repairs=0;app_repair_flow();assert(!queued&&!repairs&&handoff_failure_recorded);handoff=true;repair_ok=false;app_repair_flow();assert(!queued&&repairs==1&&fast_lock);repair_ok=true;app_repair_flow();assert(queued==CMD_CONNECT&&!fast_lock&&repairs==2);
 puts("PASS actual API: authentication, idle/journal/ownership gates, exact repair phrase, persistent interval conversion/rollback, pause/resume, repair handoff and USB only on success");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(prefix+extract('app_idle')+extract('app_api_control')+extract('app_api')+extract('app_repair_flow')+checks);subprocess.run(['clang','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'src'),str(p/'test.c'),str(root/'src/app_settings.c'),str(root/'src/sync_retry.c'),'-o',str(p/'run')],check=True);subprocess.run([str(p/'run')],check=True)
