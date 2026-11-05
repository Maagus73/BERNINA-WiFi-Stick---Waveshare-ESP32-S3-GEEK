#include "sync_rules.h"
static struct {
 sync_rules_t nas,sd;
 char source[65],candidate[65],generation[65],policy[130];
 bool loaded;
} sync_policy;
typedef struct {char source[65],generation[65],policy[130];bool invalid;uint64_t files,directories;bool files_set,dirs_set;} sync_meta_t;
static bool sync_hex(const char *s){if(strlen(s)!=64)return false;for(unsigned i=0;i<64;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;return true;}
static esp_err_t sync_meta_event(esp_http_client_event_t *event)
{
 sync_diag_event(event);
 sync_meta_t *meta=event->user_data;if(!meta||event->event_id!=HTTP_EVENT_ON_HEADER||!event->header_key||!event->header_value)return ESP_OK;
 if(!strcasecmp(event->header_key,"X-Bernina-Files")||!strcasecmp(event->header_key,"X-Bernina-Directories")){
  uint64_t n=0;bool valid=*event->header_value;
  for(const char *p=event->header_value;*p;p++){if(*p<'0'||*p>'9'||n>1000000){valid=false;break;}n=n*10+(*p-'0');}
  if(valid){if(!strcasecmp(event->header_key,"X-Bernina-Files")){meta->files=n;meta->files_set=true;}else{meta->directories=n;meta->dirs_set=true;}}
 }
 char *dest=NULL;size_t capacity=0;
 if(!strcasecmp(event->header_key,"X-Bernina-Source")){dest=meta->source;capacity=sizeof(meta->source);}
 else if(!strcasecmp(event->header_key,"X-Bernina-Generation")){dest=meta->generation;capacity=sizeof(meta->generation);}
 else if(!strcasecmp(event->header_key,"X-Bernina-Policy")){dest=meta->policy;capacity=sizeof(meta->policy);}
 if(dest){if(strlen(event->header_value)>=capacity||(*dest&&strcmp(dest,event->header_value)))meta->invalid=true;else strcpy(dest,event->header_value);}
 return ESP_OK;
}
static bool sync_meta_valid(const sync_meta_t *meta)
{
 if(meta->invalid||!sync_hex(meta->source)||!sync_hex(meta->generation)||strlen(meta->policy)!=129||meta->policy[64]!=':')return false;
 char digest[65];memcpy(digest,meta->policy,64);digest[64]=0;return sync_hex(digest)&&sync_hex(meta->policy+65);
}
static bool sync_source_allowed(const char *source)
{
 nvs_handle_t handle;if(nvs_open("bernina_idx",NVS_READWRITE,&handle)!=ESP_OK)return false;
 char saved[65];size_t size=sizeof(saved);esp_err_t result=nvs_get_str(handle,"source",saved,&size);nvs_close(handle);
 bool allowed=result==ESP_OK?!strcmp(saved,source):result==ESP_ERR_NVS_NOT_FOUND&&!mirror_lock&&!update_lock;
 if(!allowed){xSemaphoreTake(state_lock,portMAX_DELAY);snprintf(sync_policy.candidate,sizeof(sync_policy.candidate),"%s",source);xSemaphoreGive(state_lock);}
 return allowed;
}
static bool sync_rule_digest(const char *text,size_t size,const sync_meta_t *meta,bool nas)
{
 unsigned char bytes[32];char digest[65];mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);
 bool ok=mbedtls_sha256_starts(&sha,0)==0&&mbedtls_sha256_update(&sha,(const unsigned char *)text,size)==0&&mbedtls_sha256_finish(&sha,bytes)==0;mbedtls_sha256_free(&sha);
 if(!ok)return false;
 for(unsigned i=0;i<32;i++)snprintf(digest+i*2,3,"%02x",bytes[i]);
 return !strncmp(digest,meta->policy+(nas?0:65),64);
}
static bool sync_fetch_rules(const char *resource,sync_rules_t *rules,sync_meta_t *meta,bool require_app)
{
 char *url=comparison_url(resource);if(!url)return false;size_t n=strlen(url);if(n&&url[n-1]=='/')url[n-1]=0;
 esp_http_client_config_t config={.url=url,.username=WEBDAV_USER,.password=WEBDAV_PASSWORD,.auth_type=HTTP_AUTH_TYPE_BASIC,.cert_pem=synology_cert_start,.transport_type=HTTP_TRANSPORT_OVER_SSL,.disable_auto_redirect=true,.timeout_ms=30000,.buffer_size=2048,.event_handler=sync_meta_event,.user_data=meta};
 esp_http_client_handle_t client=esp_http_client_init(&config);free(url);if(!client)return false;
 bool ok=false;char *text=malloc(SYNC_RULE_TEXT+1);if(!text)goto done;
 if((require_app&&!app_access_allowed())||(require_app&&*sync_policy.generation&&esp_http_client_set_header(client,"X-Bernina-Generation",sync_policy.generation)!=ESP_OK)||sync_http_open(client,0,"NAS-Regeln verbinden")!=ESP_OK)goto done;
 int64_t length=sync_http_headers(client,"NAS-Regeln Antwort");nas_last_http=esp_http_client_get_status_code(client);
 if(nas_last_http!=200||length<0||length>SYNC_RULE_TEXT)goto done;
 size_t used=0;while(used<(size_t)length){if(require_app&&!app_access_allowed())goto done;int got=sync_http_read(client,text+used,(size_t)length-used,"NAS-Regeln lesen");if(got<=0)goto done;used+=(unsigned)got;}
 ok=esp_http_client_is_complete_data_received(client)&&sync_meta_valid(meta)&&sync_rule_digest(text,used,meta,!strcmp(resource,".bernina-nas-excludes-v1"))&&sync_rules_parse(rules,text,used);
 done:free(text);esp_http_client_cleanup(client);return ok;
}
static bool sync_get_rules(const char *resource,sync_rules_t *rules,sync_meta_t *meta){return sync_fetch_rules(resource,rules,meta,true);}
static bool sync_recovery_probe(const char *source,const char *policy){
 struct {sync_rules_t rules;sync_meta_t meta;} *p=heap_caps_calloc(1,sizeof(*p),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!p){sync_diag_set(SYNC_FAIL_LOCAL,"Pruefspeicher fehlt");return false;}
 bool ok=sync_fetch_rules(".bernina-nas-excludes-v1",&p->rules,&p->meta,false);
 if(ok)ok=sync_source_allowed(p->meta.source)&&(!*source||!strcmp(source,p->meta.source))&&(!*policy||!strcmp(policy,p->meta.policy));
 if(!ok&&sync_diagnostic.kind==SYNC_FAIL_NONE)sync_diag_set(SYNC_FAIL_PERMANENT,"Quelle, Regeln oder Metadaten ungueltig");
 heap_caps_free(p);return ok;
}
static bool sync_load_policy(void)
{
 sync_policy.loaded=false;sync_policy.generation[0]=0;xSemaphoreTake(state_lock,portMAX_DELAY);sync_policy.candidate[0]=0;xSemaphoreGive(state_lock);sync_meta_t nas={0},sd={0};
 if(!sync_get_rules(".bernina-nas-excludes-v1",&sync_policy.nas,&nas)||!sync_source_allowed(nas.source))return false;
 strcpy(sync_policy.generation,nas.generation);
 if(!sync_get_rules(".bernina-sd-excludes-v1",&sync_policy.sd,&sd)||strcmp(nas.source,sd.source)||strcmp(nas.generation,sd.generation)||strcmp(nas.policy,sd.policy))return false;
 app_meta_known=nas.files_set&&nas.dirs_set;app_meta_files=nas.files;app_meta_directories=nas.directories;
 strcpy(sync_policy.source,nas.source);strcpy(sync_policy.policy,nas.policy);xSemaphoreTake(state_lock,portMAX_DELAY);sync_policy.candidate[0]=0;sync_policy.loaded=true;xSemaphoreGive(state_lock);return true;
}
static bool sync_policy_unchanged(const char *generation)
{
 char policy[130],source[65];strcpy(policy,sync_policy.policy);strcpy(source,sync_policy.source);
 return sync_load_policy()&&!strcmp(policy,sync_policy.policy)&&!strcmp(source,sync_policy.source)&&!strcmp(generation,sync_policy.generation);
}
static bool sync_sd_ignored(const char *path,bool directory)
{
 // Recovery artefacts must remain visible even under a broad user wildcard.
 char prefix[6]={0};for(unsigned i=0;i<5&&path[i];i++)prefix[i]=path[i]>='a'&&path[i]<='z'?path[i]-32:path[i];
 if(!strcmp(prefix,".B16-")||!strcmp(prefix,".B19-")||!strcmp(prefix,".B20-"))return false;
 return sync_policy.loaded&&(sync_rules_match(&sync_policy.sd,path,directory)||sync_rules_match(&sync_policy.nas,path,directory));
}
static esp_err_t sync_adopt_source(httpd_req_t *req); // defined after the worker adapters
