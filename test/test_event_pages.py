#!/usr/bin/env python3
"""Exercise the actual event endpoint with bounded heap and allocation failures."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1];source=(root/'src/app_runtime.inc').read_text()
def extract(name):
 m=re.search(r'^static [^\n]+?\b'+name+r'\([^)]*\)',source,re.M);a=source.index('{',m.end());i=a+1;depth=1
 while depth:depth+=int(source[i]=='{')-int(source[i]=='}');i+=1
 return source[m.start():i]+'\n'
prefix=r'''
#include "cJSON.h"
#include "app_settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <assert.h>
#define APP_EVENT_PAGE_SIZE 16U
#define APP_JSON_CAPACITY 32768U
#define MAX_LOG 128
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND -1
#define portMAX_DELAY 0
static size_t used,peak,budget=32768;static unsigned allocations,fail_at;static bool psram_fail;
typedef struct{size_t n;max_align_t alignment;}allocation_header;
static void *bounded_alloc(size_t n){allocations++;if(allocations==fail_at||n>budget-used)return NULL;allocation_header *h=malloc(sizeof(*h)+n);assert(h);h->n=n;used+=n;if(used>peak)peak=used;return h+1;}
static void bounded_free(void *p){if(p){allocation_header *h=(allocation_header *)p-1;used-=h->n;free(h);}}
static void *heap_caps_malloc(size_t n,unsigned caps){assert(caps==3);return psram_fail?NULL:malloc(n);}
static void heap_caps_free(void *p){free(p);}
typedef int esp_err_t;typedef struct{const char *query;}httpd_req_t;
static int response_status;static char response[65536];
static void httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;assert(strstr(s,"json"));}
static void httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v){(void)r;(void)k;(void)v;}
static void httpd_resp_set_status(httpd_req_t *r,const char *s){(void)r;response_status=atoi(s);}
static int httpd_resp_sendstr(httpd_req_t *r,const char *s){(void)r;assert(s&&strlen(s)<sizeof(response));strcpy(response,s);return 0;}
static int httpd_req_get_url_query_str(httpd_req_t *r,char *q,size_t n){if(!r->query)return ESP_ERR_NOT_FOUND;if(strlen(r->query)>=n)return -2;strcpy(q,r->query);return ESP_OK;}
static int httpd_query_key_value(const char *q,const char *key,char *out,size_t n){size_t k=strlen(key);while(*q){const char *end=strchr(q,'&');if(!end)end=q+strlen(q);if((size_t)(end-q)>k&&!strncmp(q,key,k)&&q[k]=='='){size_t len=end-q-k-1;if(len>=n)return -2;memcpy(out,q+k+1,len);out[len]=0;return 0;}q=*end?end+1:end;}return ESP_ERR_NOT_FOUND;}
static bool authorized=true,idle=true,wifi_has_ip=true,app_control_busy,app_archive_busy,archive_fail,archive_invalid;static int state_lock;static unsigned held;
static void xSemaphoreTake(int a,int b){(void)a;(void)b;assert(!held);held++;}static void xSemaphoreGive(int a){(void)a;assert(held==1);held--;}
static bool app_read_key(httpd_req_t *r){(void)r;return authorized;}
static bool app_idle(void){return idle&&!app_archive_busy;}
static unsigned log_count=128;static time_t app_event_time[MAX_LOG];static char logs[MAX_LOG][192],app_device[]="001122334455",app_boot_id[]="0123456789abcdef";
static cJSON *app_nas_json(const char *resource,const char *body){assert(!body&&!held&&app_archive_busy);assert(strstr(resource,"limit=16"));if(archive_fail)return NULL;
 unsigned long after=strtoul(strstr(resource,"after=")+6,NULL,10);cJSON *o=cJSON_CreateObject(),*rows=cJSON_AddArrayToObject(o,"events");
 if(!o||!rows){cJSON_Delete(o);return NULL;}
 for(unsigned i=0;i<16;i++){cJSON *e=cJSON_CreateObject();if(!e||!cJSON_AddStringToObject(e,"boot","nasboot")||!cJSON_AddNumberToObject(e,"seq",after+i)||!cJSON_AddNumberToObject(e,"ts",time(NULL))||!cJSON_AddStringToObject(e,"message",logs[i])){cJSON_Delete(e);cJSON_Delete(o);return NULL;}cJSON_AddItemToArray(rows,e);}
 if(!cJSON_AddNumberToObject(o,"next",archive_invalid?after:after+16)||!cJSON_AddBoolToObject(o,"more",true)){cJSON_Delete(o);return NULL;}return o;
}
typedef struct {unsigned sequence;time_t time;char message[192];} app_event_snapshot_t;
'''
message='static esp_err_t app_message(httpd_req_t *r,const char *status,const char *message){cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"message",message);return app_json(r,o,status);}\n'
checks=r'''
static void request(const char *q){assert(!used&&!held&&!app_archive_busy);httpd_req_t r={q};response_status=200;response[0]=0;assert(app_events(&r)==0);assert(!used&&!held&&!app_archive_busy);}
static cJSON *reply(void){size_t old=budget;budget=(size_t)-1;cJSON *o=cJSON_Parse(response);budget=old;assert(o);return o;}
int main(void){setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1);tzset();cJSON_Hooks hooks={bounded_alloc,bounded_free};cJSON_InitHooks(&hooks);
 for(unsigned i=0;i<128;i++){app_event_time[i]=time(NULL);snprintf(logs[i],sizeof(logs[i]),"12:00:00: %03u – Pruefung mit Umlauten äöü, Anfuehrungszeichen \\\" und Details zur Datei",i);}
 unsigned local=0;
 for(unsigned page=0;page<8;page++){char q[80];snprintf(q,sizeof(q),"after=%u&local_after=%u",page*16,local);request(q);assert(response_status==200);cJSON *o=reply();assert(cJSON_GetArraySize(cJSON_GetObjectItem(o,"events"))==16);assert(cJSON_GetArraySize(cJSON_GetObjectItem(o,"local"))==16);local=(unsigned)cJSON_GetNumberValue(cJSON_GetObjectItem(o,"local_next"));assert(local==(page+1)*16);assert(cJSON_IsTrue(cJSON_GetObjectItem(o,"local_more"))==(page<7));cJSON_Delete(o);assert(!used);}
 assert(peak<32768);printf("PASS bounded event pages: all 128 local entries, NAS 16/page; internal JSON peak %zu bytes\n",peak);
 authorized=false;request(NULL);assert(response_status==403);authorized=true;
 const char *bad[]={"after=-1","after=4294967296","after=999999999999999999999","after=abc","local_after=2x","after="};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){request(bad[i]);assert(response_status==400);}
 archive_invalid=true;request("after=32&local_after=0");cJSON *o=reply();assert(!cJSON_IsTrue(cJSON_GetObjectItem(o,"archive_available")));assert(cJSON_GetNumberValue(cJSON_GetObjectItem(o,"next"))==32);cJSON_Delete(o);archive_invalid=false;
 archive_fail=true;request("after=32&local_after=200");o=reply();assert(cJSON_IsTrue(cJSON_GetObjectItem(o,"local_gap")));assert(cJSON_GetNumberValue(cJSON_GetObjectItem(o,"local_next"))==16);cJSON_Delete(o);archive_fail=false;
 psram_fail=true;request("after=0");assert(response_status==503);o=reply();assert(cJSON_IsString(cJSON_GetObjectItem(o,"message")));cJSON_Delete(o);psram_fail=false;
 /* Every injected local-JSON allocation failure yields a complete 503 response. */
 idle=false;
 for(unsigned n=1;n<150;n++){allocations=0;fail_at=n;request("after=0");assert(response_status==200||response_status==503);fail_at=0;o=reply();assert(o);cJSON_Delete(o);assert(!used);}
 fail_at=0;httpd_req_t r={0};o=cJSON_CreateObject();char *large=malloc(40001);memset(large,'x',40000);large[40000]=0;budget=100000;cJSON_AddStringToObject(o,"x",large);free(large);response_status=200;app_json(&r,o,NULL);assert(response_status==503&&!used);
 puts("PASS explicit JSON 503 on allocation/serialization failures, cursor validation, invalid archive retains cursor, reboot/gap handling and no leaked locks/allocations");}
'''
sdk=Path.home()/'.platformio/packages/framework-espidf/components/json/cJSON'
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(prefix+extract('app_json_unavailable')+extract('app_json')+message+extract('app_event_cursor')+extract('app_events')+checks)
 subprocess.run(['clang','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(root/'src'),'-I'+str(sdk),str(p/'test.c'),str(root/'src/app_settings.c'),str(sdk/'cJSON.c'),'-lm','-o',str(p/'run')],check=True)
 subprocess.run([str(p/'run')],check=True)
