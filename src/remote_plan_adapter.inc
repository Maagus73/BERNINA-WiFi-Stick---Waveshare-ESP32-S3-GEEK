#ifdef ESP_PLATFORM
#include "esp_tls.h"
#endif
#include "remote_plan.h"
#define REMOTE_SLOTS 32768U
typedef struct {uint64_t a,b;
 bool directory;
 } remote_key_t;
 
typedef struct {
 sync_meta_t meta;
 char id[65],part[65];
 uint64_t total,offset;
 char server_error[65], transfer_id[33];
 int tls_error, tls_code, tls_flags;
 bool invalid,bundle_supported;
 
} remote_headers_t;
 
typedef struct {
 remote_plan_t *parser;
 remote_key_t *keys;
 unsigned entries;
 uint64_t directories;
 bool apply;
 
} remote_context_t;
 
static uint64_t remote_directory_total;
static bool remote_repair;
static remote_context_t repair_inventory;
static uint64_t repair_verified;

#if SYNC_BULK_ENABLED
static bool bundle_queue(const char *,const dav_entry_t *);
static bool bundle_flush(void);
static void bundle_reset(void);
#endif
static esp_err_t remote_event(esp_http_client_event_t *event)
{
 remote_headers_t *h=event->user_data;
 esp_http_client_event_t copy=*event;
 copy.user_data=&h->meta;
 sync_meta_event(&copy);
#ifdef ESP_PLATFORM
 h->tls_error=sync_diagnostic.tls;h->tls_code=sync_diagnostic.tls_code;h->tls_flags=sync_diagnostic.tls_flags;
#endif
 
 if(event->event_id!=HTTP_EVENT_ON_HEADER||!event->header_key||!event->header_value)return ESP_OK;
 
 if(!strcasecmp(event->header_key,"X-Bernina-Transfer")){
  size_t n=strlen(event->header_value);bool safe=n==32;
  for(size_t i=0;i<n&&safe;i++)if(!((event->header_value[i]>='0'&&event->header_value[i]<='9')||(event->header_value[i]>='a'&&event->header_value[i]<='f')))safe=false;
  if(safe)strcpy(h->transfer_id,event->header_value);
 }
 if(!strcasecmp(event->header_key,"X-Bernina-Error")){
  size_t n=strlen(event->header_value);
  if(n&&n<sizeof(h->server_error)){
   bool safe=true;for(size_t i=0;i<n;i++)if(!((event->header_value[i]>='a'&&event->header_value[i]<='z')||event->header_value[i]=='_'))safe=false;
   if(safe)strcpy(h->server_error,event->header_value);
  }
 }
 if(!strcasecmp(event->header_key,"X-Bernina-Bundle")){
  if(strcmp(event->header_value,"1"))h->invalid=true;else h->bundle_supported=true;
 }
 char *out=NULL;
 if(!strcasecmp(event->header_key,"X-Bernina-Plan"))out=h->id;
 else if(!strcasecmp(event->header_key,"X-Bernina-Part"))out=h->part;
 
 if(out){if(strlen(event->header_value)!=64||(*out&&strcmp(out,event->header_value)))h->invalid=true;
 else strcpy(out,event->header_value);
 }
 uint64_t *number=NULL;
 if(!strcasecmp(event->header_key,"X-Bernina-Total"))number=&h->total;
 else if(!strcasecmp(event->header_key,"X-Bernina-Offset"))number=&h->offset;
 
 if(number){const char *s=event->header_value;
 if(!*s)h->invalid=true;
 uint64_t value=0;
 for(;
 *s;
 s++){if(*s<'0'||*s>'9'||value>100000000){h->invalid=true;
 break;
 }value=value*10+(unsigned)(*s-'0');
 }*number=value;
 }
 return ESP_OK;
 
}
static bool remote_meta(const remote_headers_t *h)
{
 return !h->invalid&&sync_meta_valid(&h->meta)&&sync_hex(h->id)&&!strcmp(h->meta.generation,nas_index_generation)&&!strcmp(h->meta.source,sync_policy.source)&&!strcmp(h->meta.policy,sync_policy.policy);
 
}
static bool remote_key(remote_context_t *c,const char *path,bool directory,bool insert)
{
 unsigned char digest[32];
 if(!mirror_digest(path,strlen(path),digest))return false;
 uint64_t a,b;
 memcpy(&a,digest,8);
 memcpy(&b,digest+8,8);
 if(!a&&!b)a=1;
 
 unsigned slot=(unsigned)a&(REMOTE_SLOTS-1);
 
 for(unsigned i=0;
 i<REMOTE_SLOTS;
 i++,slot=(slot+1)&(REMOTE_SLOTS-1)){
  remote_key_t *k=&c->keys[slot];
 if(!k->a&&!k->b){if(!insert)return false;
 if(c->entries>=20000)return false;
 k->a=a;
 k->b=b;
 k->directory=directory;
 c->entries++;
 return true;
 }
  if(k->a==a&&k->b==b)return !insert&&k->directory==directory;
 
 }
 return false;
 
}
static bool remote_emit(void *v,char action,const char *path,const dav_entry_t *entry)
{
 remote_context_t *c=v;
 if(!app_access_allowed())return false;
 
 char nfc[DAV_PATH_LIMIT],fat[DAV_PATH_LIMIT];
 bool invalid;
 
 bool directory=entry->kind==DAV_DIRECTORY;
 
 if(!dav_normalize_path(path,nfc,fat,sizeof(fat),comparison_upper,&invalid)||invalid||sd_index_reserved(path)||sync_sd_ignored(path,directory)||!strncmp(fat,".B16-",5)||!strncmp(fat,".B19-",5)||!strncmp(fat,".B20-",5)||entry->size>2147483647ULL)return false;
 
 if(!c->apply){
  if(action=='D')c->directories++;
  if(strchr("DFVK",action)){
   char parent[DAV_PATH_LIMIT];
 strcpy(parent,fat);
 char *slash=strrchr(parent,'/');
 if(slash){*slash=0;
 if(!remote_key(c,parent,true,false))return false;
 }
   if(!remote_key(c,fat,directory,true))return false;
 
  }
  if(c->parser->index.count%128==0)fast_scan(NULL,"Docker-Plan pruefen",c->parser->index.count,c->parser->operations);
 
  return true;
 
 }
 char full[DAV_PATH_LIMIT+64];
 if(snprintf(full,sizeof(full),"%s/%s",SD_PATH,path)>=(int)sizeof(full))return false;
 
 if(action=='V'||action=='K')return true;
 
 if(action=='X'){if(unlink(full)&&errno!=ENOENT)return false;
 fast_result.deleted++;
 return true;
 }
 if(action=='R'){if(rmdir(full)&&errno!=ENOENT&&errno!=ENOTEMPTY&&errno!=EEXIST)return false;
 return true;
 }
 if(action=='D'){if(mkdir(full,0700)){struct stat st;
 if(errno!=EEXIST||stat(full,&st)||!S_ISDIR(st.st_mode))return false;
 }c->directories++;
 fast_scan(NULL,"NAS-Ordner anlegen",c->directories,remote_directory_total);
 return true;
 }
#if SYNC_BULK_ENABLED
 if(remote_repair){
  struct stat st;
  if(!stat(full,&st)){
   if(!S_ISREG(st.st_mode)){fast_result.error="Reparatur: Zieltyp-Konflikt";return false;}
   if((uint64_t)st.st_size==entry->size){
    unsigned char hash[32];char expected[67];
    if(!fast_file_hash(full,hash)){fast_result.error="Reparatur: SD-Datei nicht lesbar";return false;}
    expected[0]='"';for(unsigned i=0;i<32;i++)snprintf(expected+1+2*i,3,"%02x",hash[i]);expected[65]='"';expected[66]=0;
    if(!strcmp(expected,entry->etag)){
     if(!bundle_flush())return false;
     fast_result.completed++;fast_result.received+=entry->size;repair_verified++;
     fast_progress(NULL,fast_result.completed,fast_result.files,fast_result.received,fast_result.bytes);
     return true;
    }
   }
  }else if(errno!=ENOENT){fast_result.error="Reparatur: SD-Stat fehlgeschlagen";return false;}
 }
 return bundle_queue(path,entry);
#else
 dav_tree_entry_t file={.path=(char *)path,.etag=(char *)entry->etag,.kind=DAV_FILE,.size=entry->size,.size_known=true};
 
 if(!fast_copying)fast_phase(NULL,"Docker-Plan: NAS-Dateien kopieren");
 
 fast_progress(NULL,fast_result.completed,fast_result.files,fast_result.received,fast_result.bytes);
 
 if(!fast_copy(NULL,&file))return false;
 
 fast_result.completed++;
 fast_result.received+=entry->size;
 fast_progress(NULL,fast_result.completed,fast_result.files,fast_result.received,fast_result.bytes);
 return true;
#endif
 
}
static esp_http_client_handle_t remote_open(const char *resource,remote_headers_t *headers,bool create,bool baseline)
{
 char *url=comparison_url(resource);
 if(!url)return NULL;
 size_t n=strlen(url);
 if(n&&url[n-1]=='/')url[n-1]=0;
 
 esp_http_client_config_t config={.url=url,.username=WEBDAV_USER,.password=WEBDAV_PASSWORD,.auth_type=HTTP_AUTH_TYPE_BASIC,.cert_pem=synology_cert_start,.transport_type=HTTP_TRANSPORT_OVER_SSL,.disable_auto_redirect=true,.timeout_ms=30000,.buffer_size=2048,.event_handler=remote_event,.user_data=headers};
 
 esp_http_client_handle_t client=esp_http_client_init(&config);
 free(url);
 if(!client)return NULL;
 
 bool ok=nas_index_pin(client);
 
 if(create){char threshold[20];
 snprintf(threshold,sizeof(threshold),"%u",SYNC_REBUILD_THRESHOLD);
 ok=ok&&esp_http_client_set_header(client,"X-Bernina-Threshold",threshold)==ESP_OK&&esp_http_client_set_header(client,"X-Bernina-Base",baseline?fast_record.generation:"")==ESP_OK&&esp_http_client_set_header(client,"X-Bernina-Full",baseline?"0":"1")==ESP_OK;
 }
 if(!ok||!app_access_allowed()||sync_http_open(client,0,"NAS-Plan/Paket verbinden")!=ESP_OK){esp_http_client_cleanup(client);
 return NULL;
 }
 return client;
 
}
#if SYNC_BULK_ENABLED
#include "bundle_copy_adapter.inc"
#endif
static bool remote_validate(bool baseline,char id[65],uint64_t *length,uint64_t *files,uint64_t *bytes,bool *full,uint64_t *differences)
{
 remote_directory_total=0;
 remote_headers_t headers={0};
 esp_http_client_handle_t client=remote_open(".bernina-plan-v2",&headers,true,baseline);
 if(!client)return false;
 
 int64_t size=sync_http_headers(client,"NAS-Plan/Paket Antwort");
 nas_last_http=esp_http_client_get_status_code(client);
 
 remote_context_t c={0};
 c.parser=calloc(1,sizeof(*c.parser));
 c.keys=fast_allocate(REMOTE_SLOTS*sizeof(*c.keys));
 bool ok=false;
 
 if(!c.parser||!c.keys||size<=0||size>64*1024*1024||nas_last_http!=200||!remote_meta(&headers))goto done;
 
#if SYNC_BULK_ENABLED
 if(!headers.bundle_supported){fast_result.error="NAS-Sammeltransfer-API fehlt; neuen Docker-Dienst installieren";goto done;}
#endif
 memset(c.keys,0,REMOTE_SLOTS*sizeof(*c.keys));
 remote_plan_init(c.parser,remote_emit,&c);
 
 mbedtls_sha256_context sha;
 mbedtls_sha256_init(&sha);
 ok=mbedtls_sha256_starts(&sha,0)==0;
 uint64_t received=0;
 
 while(ok){char buffer[2048];
 display_ui_cooperate(NULL);
 if(!app_access_allowed()){ok=false;
 break;
 }int got=sync_http_read(client,buffer,sizeof(buffer),"NAS-Plan lesen");
 if(got<0){ok=false;
 break;
 }if(!got)break;
 received+=(unsigned)got;
 ok=mbedtls_sha256_update(&sha,(unsigned char *)buffer,(size_t)got)==0&&remote_plan_feed(c.parser,buffer,(size_t)got);
 }
 unsigned char digest[32]={0};
 char hex[65];
 if(ok)ok=mbedtls_sha256_finish(&sha,digest)==0;
 mbedtls_sha256_free(&sha);
 
 for(unsigned i=0;
 i<32;
 i++)snprintf(hex+2*i,3,"%02x",digest[i]);
 
 ok=ok&&received==(uint64_t)size&&esp_http_client_is_complete_data_received(client)&&remote_plan_finish(c.parser)&&(baseline||c.parser->full)&&!strcmp(c.parser->generation,nas_index_generation)&&!strcmp(hex,headers.id);
 
 if(ok){if(remote_repair){repair_inventory=c;repair_inventory.parser=NULL;c.keys=NULL;}
 remote_directory_total=c.directories;
 strcpy(id,headers.id);
 *length=received;
 *files=c.parser->files;
 *bytes=c.parser->bytes;
 *full=c.parser->full;*differences=c.parser->differences;
 }
 done:free(c.parser);
 fast_release(c.keys);
 esp_http_client_cleanup(client);
 return ok;
 
}
static bool remote_apply(const char *id,uint64_t length)
{
#if SYNC_BULK_ENABLED
 bundle_reset();strcpy(bundle.plan,id);
#endif
 remote_context_t c={.apply=true};
 c.parser=calloc(1,sizeof(*c.parser));
 unsigned char *buffer=fast_allocate(65536);
 if(!c.parser||!buffer){free(c.parser);
 fast_release(buffer);
 return false;
 }
 remote_plan_init(c.parser,remote_emit,&c);
 mbedtls_sha256_context sha;
 mbedtls_sha256_init(&sha);
 bool ok=mbedtls_sha256_starts(&sha,0)==0;
 
 for(uint64_t offset=0;
 offset<length&&ok;
 ){
  fast_http_close();
 /* One TLS client at a time, then file keep-alive within this plan block. */
  char resource[128];
 snprintf(resource,sizeof(resource),".bernina-plan-v2/%s/%" PRIu64,id,offset);
 remote_headers_t h={0};
 esp_http_client_handle_t client=remote_open(resource,&h,false,false);
 if(!client){ok=false;
 break;
 }
  int64_t size=sync_http_headers(client,"NAS-Plan/Paket Antwort");
 nas_last_http=esp_http_client_get_status_code(client);
 size_t used=0;
 
  ok=size>0&&size<=65536&&(uint64_t)size<=length-offset&&nas_last_http==200&&remote_meta(&h)&&!strcmp(h.id,id)&&sync_hex(h.part)&&h.total==length&&h.offset==offset;
 
  while(ok&&used<(size_t)size){if(!app_access_allowed()){ok=false;
 break;
 }int got=sync_http_read(client,(char *)buffer+used,(size_t)size-used,"NAS-Planblock lesen");
 if(got<=0){ok=false;
 break;
 }used+=(unsigned)got;
 display_ui_cooperate(NULL);
 }
  ok=ok&&esp_http_client_is_complete_data_received(client);
 esp_http_client_cleanup(client);
 
  unsigned char digest[32];
 char hex[65];
 if(ok)ok=mirror_digest(buffer,used,digest);
 
  if(ok){for(unsigned i=0;
 i<32;
 i++)snprintf(hex+2*i,3,"%02x",digest[i]);
 ok=!strcmp(hex,h.part)&&mbedtls_sha256_update(&sha,buffer,used)==0&&remote_plan_feed(c.parser,(char *)buffer,used);
#if SYNC_BULK_ENABLED
 if(ok)ok=bundle_flush();
#endif
 }
  offset+=used;
 
 }
 unsigned char digest[32];
 char hex[65];
 if(ok)ok=mbedtls_sha256_finish(&sha,digest)==0;
 
 if(ok){for(unsigned i=0;
 i<32;
 i++)snprintf(hex+2*i,3,"%02x",digest[i]);
 ok=!strcmp(hex,id)&&remote_plan_finish(c.parser);
 }
 mbedtls_sha256_free(&sha);
#if SYNC_BULK_ENABLED
 bundle_reset();
#endif
 fast_http_close();
 free(c.parser);
 fast_release(buffer);
 return ok;
 
}
static bool remote_cache_ready(void)
{
 if(!app_access_allowed())return false;
 
 const char marker[]="BERNINA SD INDEX OWNED V1\n";
 struct stat st;
 const char *folder=SD_PATH "/BNCACHE",*owner=SD_PATH "/BNCACHE/OWNER.BIN";
 
 if(stat(folder,&st)){
  if(errno!=ENOENT||mkdir(folder,0700))return false;
 
  int fd=open(owner,O_CREAT|O_EXCL|O_WRONLY,0600);
 if(fd<0)return false;
 
  bool ok=write(fd,marker,sizeof(marker))==(ssize_t)sizeof(marker);
 if(fsync(fd)){fast_io_fault(NULL);
 ok=false;
 }if(close(fd)){fast_io_fault(NULL);
 ok=false;
 }return ok;
 
 }
 if(!S_ISDIR(st.st_mode))return false;
 FILE *f=fopen(owner,"rb");
 if(!f)return false;
 char text[sizeof(marker)+1];
 size_t n=fread(text,1,sizeof(text),f);
 bool ok=n==sizeof(marker)&&!memcmp(text,marker,n)&&!ferror(f);
 if(fclose(f)){fast_io_fault(NULL);
 ok=false;
 }return ok;
 
}
static bool remote_checkpoint(const char *id)
{
 fast_record_t record={.version=FAST_VERSION};
 update_card_id(record.cid);
 strcpy(record.generation,nas_index_generation);
 strcpy(record.source,sync_policy.source);
 strcpy(record.policy,sync_policy.policy);
 
 if(!mirror_digest(id,strlen(id),record.baseline)||!mirror_digest(&record,offsetof(fast_record_t,checksum),record.checksum)||!app_access_allowed())return false;
 
 FILE *f=fopen(FAST_MARKER,"wb");
 if(!f)return false;
 bool ok=fwrite(&record,1,sizeof(record),f)==sizeof(record);
 
 if(fflush(f)||fsync(fileno(f))){fast_io_fault(NULL);
 ok=false;
 }if(fclose(f)){fast_io_fault(NULL);
 ok=false;
 }
 if(ok)fast_record=record;
 return ok;
 
}
static void remote_clear_failure(void *unused,const char *path,const char *operation,int code)
{
 (void)unused;char reason[224];
 snprintf(reason,sizeof(reason),"%s; errno %d",operation,code);
 dav_tree_entry_t entry={.path=(char *)path};mirror_file_failure(&entry,reason,0);
 event_log(reason);event_log(path);
}
static int repair_present(void *unused,const char *path,bool directory)
{
 (void)unused;char nfc[DAV_PATH_LIMIT],fat[DAV_PATH_LIMIT];bool invalid;
 if(!dav_normalize_path(path,nfc,fat,sizeof(fat),comparison_upper,&invalid)||invalid)return -1;
 return remote_key(&repair_inventory,fat,directory,false)?1:0;
}
static bool remote_run(bool baseline)
{
 if(!comparison_sd_discard())return false;
 
 strcpy(nas_index_generation,sync_policy.generation);
 char id[65];
 uint64_t length=0,files=0,bytes=0,differences=0;
 bool full=false;
 
 fast_phase(NULL,"Docker berechnet Plan; Plan sequenziell pruefen");
 
 if(!remote_validate(baseline&&!fast_force,id,&length,&files,&bytes,&full,&differences)){if(!fast_result.error)fast_result.error="Docker-Plan fehlt, unvollstaendig oder ungueltig";
 return false;
 }
 fast_result.files=files;
 fast_result.bytes=bytes;
 fast_result.rebuild=full;
 fast_result.differences=differences;
 
 if(!nas_index_checkpoint()||!fast_arm(NULL)||!remote_cache_ready())return false;
 
 if(full&&!remote_repair){fast_phase(NULL,"Vollaufbau: SD-Nutzdaten entfernen");
 fast_sync_ops_t ops={.owned=fast_owned,.ignored=fast_ignored,.allocate=fast_allocate,.release=fast_release,.cooperate=display_ui_cooperate,.fault=fast_io_fault,.failure=remote_clear_failure,.scan=fast_scan};
 if(!fast_sync_clear(SD_PATH,&ops,&fast_result))return false;
 }
 if(!fast_space(NULL,bytes+65536)){fast_result.error="SD-Speicher reicht nicht";
 return false;
 }
 fast_phase(NULL,"Docker-Plan sequenziell ausfuehren");
 mbedtls_sha256_init(&fast_transport.single.sha);
 bool ok=remote_apply(id,length);
 mbedtls_sha256_free(&fast_transport.single.sha);
 
 fast_copying=false;
 display_ui_transfer(false,0,0,false);
 
 if(ok&&remote_repair){
  fast_phase(NULL,"Reparatur: NAS pruefen, nur SD-Zusatzdaten entfernen");
  fast_sync_ops_t ops={.owned=fast_owned,.ignored=fast_ignored,.present=repair_present,.allocate=fast_allocate,.release=fast_release,.cooperate=display_ui_cooperate,.fault=fast_io_fault,.failure=remote_clear_failure,.scan=fast_scan};
  ok=sync_policy_unchanged(nas_index_generation)&&fast_sync_reconcile(SD_PATH,&ops,&fast_result);
 }
 if(ok){fast_phase(NULL,"NAS-Abschluss pruefen und Checkpoint speichern");
 ok=sync_policy_unchanged(nas_index_generation)&&remote_checkpoint(id);
 }
 if(!ok&&!fast_result.error)fast_result.error="Plan-Ausfuehrung oder Abschluss fehlgeschlagen";
 return ok;
 
}
