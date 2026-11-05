#!/usr/bin/env python3
"""Actual remote plan -> bounded queue -> bundle HTTP -> receiver/SD commit chain."""
from pathlib import Path
import tempfile,subprocess
from support import plan_fixture as fixture
root=Path(__file__).resolve().parents[2]
prefix=fixture.prefix.replace('static void fast_scan(void *p,const char *s,uint64_t a,uint64_t b){(void)p;(void)s;(void)a;(void)b;}', 'static unsigned directory_reports;static void fast_scan(void *p,const char *s,uint64_t a,uint64_t b){(void)p;if(!strcmp(s,"NAS-Ordner anlegen")){assert(a==1&&b==1);directory_reports++;}}')+r'''
#define SYNC_BULK_ENABLED 1
static void app_events_flush(void){}
static int esp_http_client_get_errno(void *);
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_DMA 2
#define MALLOC_CAP_8BIT 4
static void *heap_caps_malloc(size_t n,unsigned caps){assert(n==16384&&(caps==7||caps==5));return malloc(n);}
#define SYNC_BULK_FILES 2U
#define SYNC_BULK_BYTES (8ULL*1024*1024)
#define SYNC_NETWORK_ATTEMPTS 3U
#define FAST_TEMP "sd/BNCACHE/temp"
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
static unsigned retry_waits;static void vTaskDelay(unsigned ticks){assert(ticks==100);retry_waits++;}
static int state_lock,bundle_requests;static uint64_t mirror_bytes,clock_tick;static bool io_fault,storage_fault;
static void xSemaphoreTake(int a,int b){(void)a;(void)b;}static void xSemaphoreGive(int a){(void)a;}
static unsigned diag_ends,diag_tls;
static void event_log(const char *s){assert(s);if(!strncmp(s,"Empfang-Ende ",13))diag_ends++;if(!strncmp(s,"TLS-Diagnose:",13))diag_tls++;}
static bool fast_owned(void *p){(void)p;return permit;}
static void fast_io_fault(void *p){(void)p;io_fault=true;}
static int64_t esp_timer_get_time(void){return clock_tick+=10;}
static void display_ui_transfer(bool a,uint64_t b,uint64_t c,bool d){(void)a;(void)b;(void)c;(void)d;}
static void mirror_file_failure(const dav_tree_entry_t *e,const char *s,int n){(void)e;(void)s;(void)n;}
'''
code=fixture.code
stubs=r'''
typedef struct {void *user;size_t start,size,used;bool part,bundle;char *reply;const char *data;} client;
static void hexhash(const void *p,size_t n,char *out){unsigned char digest[32];CC_SHA256(p,(CC_LONG)n,digest);for(int i=0;i<32;i++)sprintf(out+i*2,"%02x",digest[i]);}
static void *esp_http_client_init(esp_http_client_config_t *cfg){assert(cfg->cert_pem==synology_cert_start&&cfg->disable_auto_redirect);client *c=calloc(1,sizeof(*c));c->user=cfg->user_data;c->data=body;const char *part=strstr(cfg->url,".bernina-plan-v2/");const char *bundle=strstr(cfg->url,".bernina-bundle-v1/");if(part){c->part=true;c->start=strtoull(part+strlen(".bernina-plan-v2/")+65,NULL,10);}c->size=body_size-c->start;if(c->part&&c->size>65536)c->size=65536;
 if(bundle){c->bundle=true;bundle_requests++;uint64_t start=0;unsigned count=0;assert(sscanf(bundle+strlen(".bernina-bundle-v1/")+65,"%llu/%u",(unsigned long long *)&start,&count)==2);c->reply=malloc(10000);int n=sprintf(c->reply,"BUNDLE1\t%s\t%llu\t%u\n",plan_id,(unsigned long long)start,count);for(unsigned i=0;i<count;i++){n+=sprintf(c->reply+n,"FILE\t3\t%s\tsub/file%llu\n","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",(unsigned long long)start+i);memcpy(c->reply+n,(mode==21&&bundle_requests==1)?"bad":"abc",3);n+=3;c->reply[n++]='\n';}n+=sprintf(c->reply+n,"END\t%s\t%u\n",plan_id,count);if(mode==20)n-=10;c->data=c->reply;c->start=0;c->size=n;}
 requests++;active_clients++;assert(active_clients==1);return c;}
static int esp_http_client_set_header(void *c,const char *key,const char *value){assert(c&&key&&value);return 0;}
static int esp_http_client_open(void *c,int n){assert(c&&!n);return 0;}
static int64_t esp_http_client_fetch_headers(void *v){client *c=v;remote_headers_t *h=c->user;strcpy(h->meta.generation,nas_index_generation);strcpy(h->meta.source,sync_policy.source);strcpy(h->meta.policy,sync_policy.policy);strcpy(h->id,plan_id);h->bundle_supported=mode!=22;h->total=body_size;h->offset=c->start;if(c->part)hexhash(body+c->start,c->size,h->part);return c->size;}
static int esp_http_client_get_status_code(void *v){client *c=v;return mode==23&&c->bundle?409:mode==24&&c->bundle&&bundle_requests==1?503:200;}
static int esp_http_client_read(void *v,char *out,size_t n){client *c=v;size_t left=c->size-c->used;if(n>left)n=left;if(c->bundle&&n>13)n=13;memcpy(out,c->data+c->start+c->used,n);c->used+=n;return (int)n;}
static int esp_http_client_get_errno(void *v){(void)v;return 0;}
static bool esp_http_client_is_complete_data_received(void *v){client *c=v;return c->used==c->size;}
static void esp_http_client_cleanup(void *v){client *c=v;free(c->reply);active_clients--;free(c);}
static void makebody(void){int n=snprintf(body,sizeof(body),"BPLAN2\t%s\tDELTA\t3\t9\t1504\t3\nD\t0\t-\tsub\n",nas_index_generation);for(int i=0;i<3;i++)n+=sprintf(body+n,"F\t3\t%s\tsub/file%d\n","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",i);for(int i=0;i<1500;i++)n+=sprintf(body+n,"V\t3\t%s\tsub/same%04d\n",nas_index_generation,i);n+=sprintf(body+n,"END\t1504\t%s\n",nas_index_generation);body_size=n;hexhash(body,body_size,plan_id);}
int main(int argc,char **argv){assert(argc==2&&!chdir(argv[1]));memset(nas_index_generation,'a',64);memset(sync_policy.source,'b',64);memset(sync_policy.policy,'c',129);sync_policy.policy[64]=':';makebody();
 for(int run=0;run<6;run++){mode=run?19+run:0;char folder[32];sprintf(folder,"case%d",mode);assert(!mkdir(folder,0700)&&!chdir(folder));assert(!mkdir("sd",0700)&&!mkdir("sd/BNCACHE",0700));diag_ends=diag_tls=0;retry_waits=0;bundle_requests=requests=copies=directory_reports=0;io_fault=false;fast_copying=false;fast_result=(fast_sync_result_t){0};char id[65];uint64_t length,files,bytes,differences;bool full;bool valid=remote_validate(true,id,&length,&files,&bytes,&full,&differences);if(mode==22){assert(!valid&&!bundle_requests&&!copies);}else{assert(valid&&files==3&&bytes==9&&remote_directory_total==1);fast_result.files=files;fast_result.bytes=bytes;bool ok=remote_apply(id,length);assert(ok==(mode==0||mode==21||mode==24));assert(!copies&&!io_fault&&!active_clients&&!bundle.count&&directory_reports==1);if(ok){assert(fast_result.completed==3&&fast_result.received==9);assert(bundle_requests==((mode==21||mode==24)?3:2));assert(diag_ends==diag_tls&&diag_ends==(unsigned)(mode==24?2:bundle_requests));for(int i=0;i<3;i++){char path[64];sprintf(path,"sd/sub/file%d",i);FILE *f=fopen(path,"rb");assert(f);char text[4]={0};assert(fread(text,1,3,f)==3&&!memcmp(text,"abc",3));assert(!fclose(f));}}if(mode==20)assert(bundle_requests==SYNC_NETWORK_ATTEMPTS);if(mode==23)assert(bundle_requests==1&&!retry_waits);if(mode==21||mode==24)assert(retry_waits==20);if(mode==20)assert(retry_waits==60);}
 assert(!chdir(".."));}
 puts("PASS Test26 actual plan/queue/HTTPS/receiver chain: bounded batches and plan-page flush, exact catalog ordinals, one TLS client, file hash/SD commits, whole-batch retry without double counters, footer failure rejection, missing server capability before mutations and permanent HTTP failure");}
'''
