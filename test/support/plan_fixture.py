#!/usr/bin/env python3
"""Actual ESP plan transport/validation/execution code with controlled HTTPS fixtures."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2];s=(root/'src/remote_plan_adapter.inc').read_text();code=s[:s.index('static bool remote_cache_ready(')]
prefix=r'''
#include "fast_sync.h"
#include "remote_plan.h"
#include <CommonCrypto/CommonDigest.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#define SD_PATH "sd"
#define ESP_OK 0
#define HTTP_AUTH_TYPE_BASIC 1
#define HTTP_TRANSPORT_OVER_SSL 1
#define HTTP_EVENT_ON_HEADER 1
#define SYNC_REBUILD_THRESHOLD 25U
#define WEBDAV_USER "fixture"
#define WEBDAV_PASSWORD "fixture"
typedef int esp_err_t;typedef void *esp_http_client_handle_t;
typedef struct {char generation[65],source[65],policy[130];bool invalid;} sync_meta_t;
typedef struct {void *user_data;int event_id;const char *header_key,*header_value;} esp_http_client_event_t;
typedef struct {const char *url,*username,*password,*cert_pem;int auth_type,transport_type,timeout_ms,buffer_size;bool disable_auto_redirect;esp_err_t (*event_handler)(esp_http_client_event_t *);void *user_data;} esp_http_client_config_t;
typedef CC_SHA256_CTX mbedtls_sha256_context;
static void mbedtls_sha256_init(mbedtls_sha256_context *c){(void)c;}
static int mbedtls_sha256_starts(mbedtls_sha256_context *c,int n){(void)n;return CC_SHA256_Init(c)?0:-1;}
static int mbedtls_sha256_update(mbedtls_sha256_context *c,const void *p,size_t n){return CC_SHA256_Update(c,p,(CC_LONG)n)?0:-1;}
static int mbedtls_sha256_finish(mbedtls_sha256_context *c,void *p){return CC_SHA256_Final(p,c)?0:-1;}
static void mbedtls_sha256_free(mbedtls_sha256_context *c){(void)c;}
static struct {char generation[65],source[65],policy[130];} sync_policy;
static struct {char generation[65];} fast_record;
static char nas_index_generation[65],synology_cert_start[]="CA",body[400000],plan_id[65];
static size_t body_size;static int mode,requests,copies,nas_last_http,active_clients;static bool permit=true,fast_copying;
static fast_sync_result_t fast_result;
static bool app_access_allowed(void){return permit;}
static bool sync_hex(const char *s){if(strlen(s)!=64)return false;for(;*s;s++)if(!strchr("0123456789abcdef",*s))return false;return true;}
static bool sync_meta_valid(const sync_meta_t *m){return !m->invalid&&sync_hex(m->source)&&sync_hex(m->generation)&&strlen(m->policy)==129;}
static int sync_meta_event(esp_http_client_event_t *e){(void)e;return 0;}
static bool mirror_digest(const void *p,size_t n,unsigned char *out){CC_SHA256(p,(CC_LONG)n,out);return true;}
static uint32_t comparison_upper(uint32_t c){return c>='a'&&c<='z'?c-32:c;}
static bool sd_index_reserved(const char *p){return !strncasecmp(p,"BNCACHE",7)&&(p[7]==0||p[7]=='/');}
static bool sync_sd_ignored(const char *p,bool d){(void)d;return !strcmp(p,"protected")||!strncmp(p,"protected/",10);}
static bool fast_file_hash(const char *p,unsigned char out[32]){FILE *f=fopen(p,"rb");if(!f)return false;CC_SHA256_CTX c;CC_SHA256_Init(&c);unsigned char b[16384];size_t n;while((n=fread(b,1,sizeof(b),f)))CC_SHA256_Update(&c,b,(CC_LONG)n);bool ok=!ferror(f);if(fclose(f))ok=false;CC_SHA256_Final(out,&c);return ok;}
static void fast_scan(void *p,const char *s,uint64_t a,uint64_t b){(void)p;(void)s;(void)a;(void)b;}
static void fast_phase(void *p,const char *s){(void)p;(void)s;}
static void fast_progress(void *p,uint64_t a,uint64_t b,uint64_t c,uint64_t d){(void)p;(void)a;(void)b;(void)c;(void)d;fast_copying=true;}
static bool fast_copy(void *p,const dav_tree_entry_t *e){(void)p;assert(!active_clients);char path[1200];snprintf(path,sizeof(path),"sd/%s",e->path);FILE *f=fopen(path,"wb");if(!f)return false;bool ok=fwrite("abc",1,3,f)==3;if(fclose(f))ok=false;copies++;return ok;}
static void fast_http_close(void){}
static void *fast_allocate(size_t n){return mode==5?NULL:malloc(n);}static void fast_release(void *p){free(p);}
static void display_ui_cooperate(void *p){(void)p;}
static char *comparison_url(const char *p){char *out=malloc(strlen(p)+40);sprintf(out,"https://fixture/%s/",p);return out;}
static bool nas_index_pin(void *c){assert(c);return true;}
static void *esp_http_client_init(esp_http_client_config_t *);
static int esp_http_client_set_header(void *,const char *,const char *);
static int esp_http_client_open(void *,int);
static int64_t esp_http_client_fetch_headers(void *);
static int esp_http_client_get_status_code(void *);
static int esp_http_client_read(void *,char *,size_t);
static bool esp_http_client_is_complete_data_received(void *);
static void esp_http_client_cleanup(void *);

#include "sync_retry.h"
static struct {int tls,tls_code,tls_flags;} sync_diagnostic;
static void sync_diag_event(esp_http_client_event_t *e){(void)e;}
static void sync_diag_set(sync_failure_t k,const char *s){(void)k;(void)s;}
static int sync_http_open(void *c,int n,const char *s){(void)s;return esp_http_client_open(c,n);}
static int64_t sync_http_headers(void *c,const char *s){(void)s;return esp_http_client_fetch_headers(c);}
static int sync_http_read(void *c,char *p,int n,const char *s){(void)s;return esp_http_client_read(c,p,n);}
'''
stubs=r'''
typedef struct {void *user;size_t start,size,used;bool part;} client;
static void hexhash(const void *p,size_t n,char *out){unsigned char digest[32];CC_SHA256(p,(CC_LONG)n,digest);for(int i=0;i<32;i++)sprintf(out+i*2,"%02x",digest[i]);}
static void *esp_http_client_init(esp_http_client_config_t *cfg){assert(cfg->cert_pem==synology_cert_start&&cfg->disable_auto_redirect&&cfg->transport_type==HTTP_TRANSPORT_OVER_SSL);client *c=calloc(1,sizeof(*c));c->user=cfg->user_data;const char *id=strstr(cfg->url,".bernina-plan-v2/");if(id){c->part=true;c->start=strtoull(id+strlen(".bernina-plan-v2/")+65,NULL,10);}c->size=body_size-c->start;if(c->part&&c->size>65536)c->size=65536;requests++;active_clients++;return c;}
static int esp_http_client_set_header(void *c,const char *key,const char *value){assert(c&&key&&value);return 0;}
static int esp_http_client_open(void *c,int n){assert(c&&!n);return 0;}
static int64_t esp_http_client_fetch_headers(void *v){client *c=v;remote_headers_t *h=c->user;strcpy(h->meta.generation,nas_index_generation);strcpy(h->meta.source,sync_policy.source);strcpy(h->meta.policy,sync_policy.policy);strcpy(h->id,plan_id);h->total=body_size;h->offset=c->start;if(c->part)hexhash(body+c->start,c->size,h->part);if(mode==1)h->meta.source[0]='e';if(mode==6&&c->part)h->offset++;if(mode==7&&c->part)h->total++;if(mode==8)h->id[0]=h->id[0]=='a'?'b':'a';if(mode==2&&c->part)h->part[0]=h->part[0]=='a'?'b':'a';return c->size;}
static int esp_http_client_get_status_code(void *v){(void)v;return mode==3?503:200;}
static int esp_http_client_read(void *v,char *out,size_t n){client *c=v;if(mode==4&&c->used)return 0;size_t left=c->size-c->used;if(n>left)n=left;memcpy(out,body+c->start+c->used,n);c->used+=n;return (int)n;}
static bool esp_http_client_is_complete_data_received(void *v){client *c=v;return c->used==c->size;}
static void esp_http_client_cleanup(void *v){active_clients--;free(v);}
static void makebody(void){int n=snprintf(body,sizeof(body),"BPLAN2\t%s\tDELTA\t1\t3\t1502\t1\nD\t0\t-\tsub\nF\t3\t%s\tsub/file\n",nas_index_generation,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");for(int i=0;i<1500;i++)n+=snprintf(body+n,sizeof(body)-n,"V\t3\t%s\tsub/same%04d\n",nas_index_generation,i);n+=snprintf(body+n,sizeof(body)-n,"END\t1502\t%s\n",nas_index_generation);body_size=n;hexhash(body,body_size,plan_id);}
int main(int argc,char **argv){assert(argc==2&&!chdir(argv[1]));assert(!mkdir("sd",0700));memset(nas_index_generation,'a',64);memset(sync_policy.source,'b',64);memset(sync_policy.policy,'c',129);sync_policy.policy[64]=':';makebody();
 char id[65];uint64_t length,files,bytes,differences;bool full;
 assert(remote_validate(true,id,&length,&files,&bytes,&full,&differences));assert(!full&&files==1&&bytes==3&&length==body_size&&copies==0&&access("sd/sub",F_OK)!=0);fast_result.files=files;fast_result.bytes=bytes;
 assert(remote_apply(id,length)&&copies==1&&fast_result.completed==1&&requests>=3&&access("sd/sub/file",F_OK)==0);
 copies=0;assert(!remote_validate(false,id,&length,&files,&bytes,&full,&differences));
 for(mode=1;mode<=8;mode++){copies=0;bool valid=remote_validate(true,id,&length,&files,&bytes,&full,&differences);if(mode==2||mode==6||mode==7){assert(valid);assert(!remote_apply(id,length));}else assert(!valid);assert(copies==0);}
 mode=0;permit=false;assert(!remote_validate(true,id,&length,&files,&bytes,&full,&differences));permit=true;
 remote_context_t c={0};remote_plan_t parser={0};c.parser=&parser;c.keys=calloc(REMOTE_SLOTS,sizeof(*c.keys));dav_entry_t e={.kind=DAV_DIRECTORY};assert(remote_emit(&c,'D',"folder",&e));assert(!remote_emit(&c,'D',"FOLDER",&e));assert(!remote_emit(&c,'D',"missing/child",&e));assert(!remote_emit(&c,'D',"BNCACHE",&e));assert(!remote_emit(&c,'D',"protected",&e));free(c.keys);
 puts("PASS Test25 actual ESP plan adapter: full prevalidation before mutations, bounded canonical RAM index, multi-block authenticated transport with digest/offset/total, sequential mkdir/copy, no SD index, bad metadata/hash/status/truncation/APP rejection, FAT alias/missing-parent/exclusion protection");}
'''
