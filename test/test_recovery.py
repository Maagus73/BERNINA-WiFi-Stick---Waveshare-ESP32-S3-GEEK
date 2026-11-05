#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
prefix=r'''
#include "sync_retry.h"
#include "recovery_config.h"
#include "display_status.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
typedef int esp_err_t;typedef void *esp_http_client_handle_t;typedef unsigned nvs_handle_t;
typedef struct {int event_id;const char *header_key,*header_value;void *data;} esp_http_client_event_t;
#define HTTP_EVENT_ON_HEADER 1
#define ESP_PLATFORM 1
#define HTTP_EVENT_ERROR 2
#define HTTP_EVENT_DISCONNECTED 3
#define MBEDTLS_ERR_X509_CERT_VERIFY_FAILED -9984
typedef void *esp_tls_error_handle_t;
static int tls_sim_code,tls_sim_flags,tls_sim_error;
static int esp_tls_get_and_clear_last_error(void *h,int *c,int *f){(void)h;*c=tls_sim_code;*f=tls_sim_flags;return tls_sim_error;}

#define ESP_OK 0
#define ESP_ERR_NO_MEM 12
#define NVS_READWRITE 1
#define NVS_READONLY 0
#define pdTRUE 1
#define FAST_VERSION 7
#define FAST_MARKER "marker"
typedef int command_t;enum{CMD_CONNECT=1,CMD_AUTO_RETRY=2,MIRROR_IDLE=0};
static bool app_auto_paused,fast_paused,fast_lock,fast_armed_run,storage_fault,io_fault,update_lock,mirror_lock,remote_repair,owned=true,app_control_busy,app_archive_busy,fast_busy,wifi_has_ip=true,fast_return_usb,display_compare_failed;
static unsigned usb_test_phase,waiting;static int command_queue,mirror_state,nas_last_http,queued;static int64_t ticks=1000000;
static struct {const char *error;bool success;} fast_result;
static struct {char source[65],policy[130];} sync_policy;
typedef struct {uint32_t version,cid[7];char source[65],policy[130];unsigned char checksum[32];}fast_record_t;
static fast_record_t fast_record;
static bool persist=true,probe=true,handoff=true,worker_success=true;static unsigned repairs,runs,probes,usb_handoffs;
static unsigned char saved_job[1024],saved_config[1024];static size_t job_size,config_size;
static bool sync_hex(const char *s){if(strlen(s)!=64)return false;for(;*s;s++)if(!strchr("0123456789abcdef",*s))return false;return true;}
static void event_log(const char *s){assert(s);}static void fast_note(const char *s){assert(s);}
static int64_t esp_timer_get_time(void){return ticks;}
static int uxQueueMessagesWaiting(int q){(void)q;return waiting;}
static int xQueueSend(int q,command_t *c,int t){(void)q;(void)t;queued=*c;return pdTRUE;}
static int nvs_open(const char *s,int m,unsigned *h){assert(!strcmp(s,"bernina_retry"));(void)m;*h=1;return persist?0:-1;}
static int nvs_set_blob(unsigned h,const char *k,const void *p,size_t n){(void)h;assert(n<1024);if(!strcmp(k,"job")){memcpy(saved_job,p,n);job_size=n;}else{memcpy(saved_config,p,n);config_size=n;}return 0;}
static int nvs_commit(unsigned h){(void)h;return persist?0:-1;}static void nvs_close(unsigned h){(void)h;}
static int nvs_get_blob(unsigned h,const char *k,void *p,size_t *n){(void)h;size_t size=!strcmp(k,"job")?job_size:config_size;if(!size||size>*n)return -1;memcpy(p,!strcmp(k,"job")?saved_job:saved_config,size);*n=size;return 0;}
static bool app_access_allowed(void){return owned;}
static void update_card_id(uint32_t cid[7]){memset(cid,0,28);}
static bool mirror_digest(const void *p,size_t n,unsigned char out[32]){(void)p;(void)n;memset(out,0,32);return true;}
static void fast_io_fault(void *p){(void)p;io_fault=true;}
static int network_status=503,network_open,network_read=-1;static int64_t network_length;static bool complete;
static int esp_http_client_open(void *c,int n){(void)c;(void)n;return network_open;}
static int esp_http_client_get_errno(void *c){(void)c;return 104;}
static int64_t esp_http_client_fetch_headers(void *c){(void)c;return network_length;}
static int esp_http_client_get_status_code(void *c){(void)c;return network_status;}
static int esp_http_client_read(void *c,char *p,int n){(void)c;(void)p;(void)n;return network_read;}
static bool esp_http_client_is_complete_data_received(void *c){(void)c;return complete;}
static bool ensure_network_time(void){return true;}
static bool sync_recovery_probe(const char *,const char *);
static bool perform_app_handoff(void){usb_handoffs++;return handoff;}
static void fast_repair_worker(void);static void fast_worker(void);
'''
declarations=r'''typedef struct {sync_failure_t kind;int http,esp,socket,tls,tls_code,tls_flags;char stage[40],server[65];} sync_diagnostic_t;
static sync_diagnostic_t sync_diagnostic;
static sync_retry_config_t recovery_config={RECOVERY_ENABLED,{RECOVERY_DELAY_1_SECONDS,RECOVERY_DELAY_2_SECONDS,RECOVERY_DELAY_3_SECONDS,RECOVERY_DELAY_MAX_SECONDS}};
static struct {uint32_t version,pending,repair;char source[65],policy[130];} recovery_job;
static uint32_t recovery_failures;
static int64_t recovery_next_us;
static char recovery_error[256];
static bool recovery_executing;
'''
checks=r'''
static bool sync_recovery_probe(const char *s,const char *p){assert(!strcmp(s,sync_policy.source));if(*p)assert(!strcmp(p,sync_policy.policy));probes++;if(!probe)sync_diag_set(SYNC_FAIL_TRANSIENT,"NAS nicht erreichbar");return probe;}
static void fast_worker(void){runs++;fast_result.success=worker_success;if(worker_success){fast_lock=false;recovery_clear();queued=CMD_CONNECT;}else{fast_lock=true;sync_diag_set(SYNC_FAIL_TRANSIENT,"Verbindung unterbrochen");recovery_failure();}}
static void fast_repair_worker(void){repairs++;fast_worker();}
static void fail(void){sync_diag_reset();sync_diag_set(SYNC_FAIL_TRANSIENT,"NAS nicht erreichbar");queued=0;recovery_failure();}
int main(void){
 display_input_t display={.initialized=true,.retry_waiting=true};assert(display_classify(&display)==DISPLAY_RETRY);display.fault=true;assert(display_classify(&display)==DISPLAY_ERROR);display.fault=false;display.busy=true;display.app_confirmed=true;display.transferring=true;assert(display_classify(&display)==DISPLAY_SYNC);
 uint32_t v[4];assert(sync_retry_delays_parse("30,60,120,300",v));assert(!sync_retry_delays_parse("30,20,120,300",v));assert(!sync_retry_delays_parse("1,2,3,86401",v));assert(!sync_retry_delays_parse("1,2,3,4,5",v));assert(!sync_retry_delays_parse("-1,2,3,4",v));assert(!sync_retry_delays_parse("1,2,3,9999999999999",v));
 assert(sync_retry_delay(&recovery_config,1)==30&&sync_retry_delay(&recovery_config,2)==60&&sync_retry_delay(&recovery_config,3)==120&&sync_retry_delay(&recovery_config,4000)==300);
 assert(sync_retry_http_class(503,"")==SYNC_FAIL_TRANSIENT&&sync_retry_http_class(401,"")==SYNC_FAIL_PERMANENT&&sync_retry_http_class(503,"source_root_changed")==SYNC_FAIL_PERMANENT);
 sync_http_open(NULL,0,"test");assert(sync_http_headers(NULL,"headers")==0&&sync_diagnostic.kind==SYNC_FAIL_TRANSIENT);network_status=401;sync_diag_reset();sync_http_headers(NULL,"headers");assert(sync_diagnostic.kind==SYNC_FAIL_PERMANENT);
 sync_diag_reset();esp_http_client_event_t event={HTTP_EVENT_ON_HEADER,"X-Bernina-Error","source_root_changed"};sync_diag_event(&event);network_status=503;sync_http_headers(NULL,"headers");assert(sync_diagnostic.kind==SYNC_FAIL_PERMANENT);
 sync_diag_reset();tls_sim_code=MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;tls_sim_error=99;event=(esp_http_client_event_t){HTTP_EVENT_ERROR,NULL,NULL,(void *)1};sync_diag_event(&event);assert(sync_diagnostic.kind==SYNC_FAIL_PERMANENT&&sync_diagnostic.tls_code==MBEDTLS_ERR_X509_CERT_VERIFY_FAILED);tls_sim_code=tls_sim_error=0;
 sync_diag_reset();char b;sync_http_read(NULL,&b,1,"read");assert(sync_diagnostic.kind==SYNC_FAIL_TRANSIENT);network_open=ESP_ERR_NO_MEM;sync_http_open(NULL,0,"open");assert(sync_diagnostic.kind==SYNC_FAIL_LOCAL);network_open=0;
 memset(sync_policy.source,'a',64);memset(sync_policy.policy,'b',129);sync_policy.policy[64]=':';
 fast_record.version=FAST_VERSION;FILE *f=fopen(FAST_MARKER,"wb");assert(f&&fwrite(&fast_record,1,sizeof(fast_record),f)==sizeof(fast_record)&&!fclose(f));
 fail();assert(recovery_active()&&!recovery_job.repair&&queued==CMD_CONNECT&&recovery_next_us==ticks+30000000);assert(*recovery_error);
 for(unsigned i=0;i<8;i++){queued=0;assert(recovery_schedule()&&!queued);ticks=recovery_next_us;assert(recovery_schedule()&&queued==CMD_AUTO_RETRY);probe=false;unsigned old=usb_handoffs;recovery_worker();assert(usb_handoffs==old&&recovery_active());}assert(recovery_next_us-ticks==300000000);
 app_auto_paused=true;ticks=recovery_next_us;queued=0;assert(recovery_schedule()&&!queued);recovery_worker();assert(!runs);app_auto_paused=false;recovery_resume();assert(recovery_next_us>ticks);
 recovery_config.enabled=0;assert(recovery_schedule());recovery_worker();assert(!runs);recovery_config.enabled=1;
 probe=true;recovery_worker();assert(runs==1&&!repairs&&!recovery_job.pending&&queued==CMD_CONNECT);
 fast_lock=fast_armed_run=true;fail();assert(recovery_job.repair&&queued==0);owned=false;assert(!recovery_checkpoint_valid());owned=true;
 memset(&recovery_job,0,sizeof(recovery_job));recovery_failures=0;recovery_boot();assert(recovery_job.pending&&recovery_job.repair&&recovery_active());probe=true;recovery_worker();assert(repairs==1&&!fast_lock&&!recovery_job.pending&&queued==CMD_CONNECT);
 fast_armed_run=false;f=fopen(FAST_MARKER,"wb");assert(f&&fputc('x',f)!=EOF&&!fclose(f));fail();assert(recovery_job.repair&&!queued);recovery_manual_start();f=fopen(FAST_MARKER,"wb");assert(f&&fwrite(&fast_record,1,sizeof(fast_record),f)==sizeof(fast_record)&&!fclose(f));
 fail();app_control_busy=true;ticks=recovery_next_us;queued=0;assert(recovery_schedule()&&!queued);app_control_busy=false;waiting=1;assert(recovery_schedule()&&!queued);waiting=0;
 storage_fault=true;fail();assert(!recovery_job.pending&&*recovery_error);storage_fault=false;
 sync_diag_reset();sync_diag_set(SYNC_FAIL_PERMANENT,"Zertifikat ungueltig");recovery_failure();assert(!recovery_job.pending&&strstr(recovery_error,"Zertifikat"));
 fail();recovery_executing=true;sync_policy.source[0]='c';assert(!recovery_policy_allowed()&&sync_diagnostic.kind==SYNC_FAIL_PERMANENT);sync_policy.source[0]='a';recovery_executing=false;
 recovery_manual_start();persist=false;fail();assert(!recovery_job.pending);persist=true;
 sync_retry_config_t c={1,{1,2,3,4}};assert(recovery_set_config(&c));recovery_config.enabled=0;recovery_boot();assert(recovery_config.enabled&&recovery_config.seconds[3]==4);
 puts("PASS recovery: real transport diagnostics, bounded validation/backoff, indefinite retry, validated-marker USB return, interrupted-write repair, durable reboot intent, pause/disable, scheduler serialization, source/SD/permanent-error stops and persistence failures");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(prefix+declarations+(root/'src/recovery_adapter.inc').read_text()+checks)
 subprocess.run(['clang','-std=gnu11','-fsanitize=address,undefined','-I'+str(root/'src'),'-I'+str(root/'include'),str(p/'test.c'),str(root/'src/sync_retry.c'),str(root/'src/display_status.c'),'-o',str(p/'run')],check=True)
 subprocess.run([str(p/'run')],cwd=d,check=True)
