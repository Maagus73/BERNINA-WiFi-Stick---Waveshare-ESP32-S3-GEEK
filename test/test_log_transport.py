#!/usr/bin/env python3
"""Actual queue/JSON/TLS transport with cJSON and ASan; no live network."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1];text=(root/'src/app_runtime.inc').read_text()
def extract(name):
 m=re.search(r'^static [^\n]+?\b'+name+r'\([^)]*\)',text,re.M);a=text.index('{',m.end());d=1;i=a+1
 while d:
  d+=int(text[i]=='{')-int(text[i]=='}');i+=1
 return text[m.start():i]+'\n'
code=r'''
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#define MAX_LOG 128
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define WEBDAV_USER "test"
#define WEBDAV_PASSWORD "test-only"
#define HTTP_AUTH_TYPE_BASIC 1
#define HTTP_TRANSPORT_OVER_SSL 1
#define HTTP_METHOD_POST 1
#define ESP_OK 0
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
static int state_lock,mode,active_clients;
static bool wifi_has_ip=true,app_archive_busy,app_archive_online;static uint32_t app_event_sent,log_count;static int64_t app_log_next;
static time_t app_event_time[MAX_LOG];static char logs[MAX_LOG][192],app_device[]="001122334455",app_boot_id[]="0123456789abcdef";
static const char synology_cert_start[]="certificate";
static int xSemaphoreTake(int a,int b){(void)a;(void)b;return 1;}static void xSemaphoreGive(int a){(void)a;}
static int64_t esp_timer_get_time(void){return 100000000;}
static char *comparison_url(const char *p){assert(!strcmp(p,".bernina-events-v1"));return strdup("https://nas/.bernina-events-v1/");}
static void *heap_caps_malloc(size_t n,unsigned c){(void)c;return mode==3?NULL:malloc(n);}static void heap_caps_free(void *p){free(p);}
typedef struct {const char *url,*username,*password,*cert_pem;int auth_type,transport_type,timeout_ms,buffer_size;bool disable_auto_redirect;}esp_http_client_config_t;
typedef struct{char payload[8193],reply[128];size_t received,at;bool post;}client;typedef client *esp_http_client_handle_t;
static client *esp_http_client_init(esp_http_client_config_t *c){assert(c->cert_pem==synology_cert_start&&c->disable_auto_redirect);assert(!strstr(c->url,"%3F"));client *p=calloc(1,sizeof(*p));active_clients++;return p;}
static void esp_http_client_set_method(client *c,int m){assert(m==1);c->post=true;}static void esp_http_client_set_header(client *c,const char *k,const char *v){(void)c;(void)k;(void)v;}
static int esp_http_client_open(client *c,int n){(void)c;(void)n;return 0;}
static int esp_http_client_write(client *c,const char *p,int n){int take=n>1?n/2:n;memcpy(c->payload+c->received,p,take);c->received+=take;return take;}
static int64_t esp_http_client_fetch_headers(client *c){if(c->post){cJSON *p=cJSON_Parse(c->payload);assert(p);cJSON *rows=cJSON_GetObjectItem(p,"events");assert(cJSON_GetArraySize(rows)==3);assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(p,"device")),app_device));cJSON_Delete(p);snprintf(c->reply,sizeof(c->reply),"{\"accepted\":%d}",mode==1?0:3);}else strcpy(c->reply,"{\"events\":[],\"next\":0,\"more\":false}");return mode==4?16385:(int64_t)strlen(c->reply);}
static int esp_http_client_get_status_code(client *c){(void)c;return mode==2?503:200;}
static int esp_http_client_read(client *c,char *p,size_t n){if(mode==5)return 0;size_t left=strlen(c->reply)-c->at;if(n>left)n=left;memcpy(p,c->reply+c->at,n);c->at+=n;return n;}
static bool esp_http_client_is_complete_data_received(client *c){return c->at==strlen(c->reply);}static void esp_http_client_cleanup(client *c){active_clients--;free(c);}
'''
checks=r'''
int main(void){for(unsigned i=0;i<3;i++){snprintf(logs[i],sizeof(logs[i]),"12:00:00: event %u",i);app_event_time[i]=time(NULL);}log_count=3;
 for(mode=0;mode<6;mode++){app_event_sent=0;app_log_next=0;app_events_flush();assert(app_event_sent==(mode==0?3:0));assert(!app_archive_busy&&!active_clients&&app_archive_online==(mode==0));}
 mode=0;cJSON *r=app_nas_json(".bernina-events-v1?device=001122334455&from=1&to=2",NULL);assert(r&&cJSON_HasObjectItem(r,"events"));cJSON_Delete(r);assert(!active_clients);puts("PASS actual event queue and TLS JSON transport: bounded payload, partial writes, exact ACK before release, HTTP/ACK/allocation failures retain queue, no dangling JSON pointer and unescaped query parameters");}
'''
sdk=Path.home()/'.platformio/packages/framework-espidf/components/json/cJSON'
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code+extract('app_nas_json')+extract('app_events_flush')+checks);subprocess.run(['clang','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(sdk),str(p/'test.c'),str(sdk/'cJSON.c'),'-lm','-o',str(p/'run')],check=True);subprocess.run([str(p/'run')],check=True)
