#include "sd_index.h"
// Test21: one bounded manifest stream, same TLS CA/auth and APP ownership gate.
static char nas_index_generation[65];
static bool nas_index_pin(esp_http_client_handle_t client)
{
 return !*nas_index_generation||(esp_http_client_set_header(client,"X-Bernina-Generation",nas_index_generation)==ESP_OK&&esp_http_client_set_header(client,"X-Bernina-Resource-Check","1")==ESP_OK);
}
static bool nas_index_fetch(nas_index_emit_t emit,void *context,char token[65])
{
 if(!app_access_allowed()||!wifi_has_ip||!ensure_network_time())return false;
 char *url=comparison_url(".bernina-index-v1");if(!url)return false;
 size_t len=strlen(url);if(len&&url[len-1]=='/')url[len-1]=0;
 sync_meta_t meta={0};
 esp_http_client_config_t config={.url=url,.username=WEBDAV_USER,.password=WEBDAV_PASSWORD,
  .auth_type=HTTP_AUTH_TYPE_BASIC,.cert_pem=synology_cert_start,.transport_type=HTTP_TRANSPORT_OVER_SSL,
  .disable_auto_redirect=true,.timeout_ms=30000,.buffer_size=2048,.event_handler=sync_meta_event,.user_data=&meta};
 esp_http_client_handle_t client=esp_http_client_init(&config);free(url);if(!client)return false;
 nas_index_parser_t *parser=calloc(1,sizeof(*parser));bool ok=false;
 if(!parser)goto done;
 nas_index_init(parser,emit,context);
 if(!nas_index_pin(client)||esp_http_client_set_header(client,"Accept-Encoding","identity")!=ESP_OK||esp_http_client_open(client,0)!=ESP_OK)goto done;
 int64_t length=esp_http_client_fetch_headers(client);nas_last_http=esp_http_client_get_status_code(client);
 if(length<=0||nas_last_http!=200)goto done;
 uint64_t received=0;
 for(;;){char bytes[2048];display_ui_cooperate(NULL);if(!app_access_allowed())goto done;int n=esp_http_client_read(client,bytes,sizeof(bytes));if(n<0)goto done;if(!n)break;received+=(unsigned)n;if(!nas_index_feed(parser,bytes,(size_t)n))goto done;}
 ok=received==(uint64_t)length&&esp_http_client_is_complete_data_received(client)&&nas_index_finish(parser)&&(!*nas_index_generation||!strcmp(nas_index_generation,parser->token));
 if(ok)ok=sync_meta_valid(&meta)&&!strcmp(meta.generation,parser->token)&&sync_policy.loaded&&!strcmp(meta.source,sync_policy.source)&&!strcmp(meta.policy,sync_policy.policy);
 if(ok)strcpy(token,parser->token);
 done:free(parser);esp_http_client_cleanup(client);return ok;
}
static bool nas_index_add(void *context,const char *path,const dav_entry_t *entry)
{
 comparison_result_t *r=context;
 if(sync_policy.loaded&&sync_rules_match(&sync_policy.sd,path,entry->kind==DAV_DIRECTORY)){r->error=DAV_ERR_RESERVED;return false;}
 if(r->tree.disk&&sd_index_reserved(path)){r->error=DAV_ERR_RESERVED;return false;}
 char parent[DAV_PATH_LIMIT];strcpy(parent,path);char *slash=strrchr(parent,'/');if(slash)*slash=0;else *parent=0;
 r->error=comparison_add(r,0,parent,entry);
 if(fast_busy&&r->tree.count[0]%128==0)fast_scan(NULL,"NAS-Manifest empfangen",r->tree.count[0],0);
 return r->error==DAV_OK;
}
static dav_error_t nas_index_compare(comparison_result_t *r)
{
 *nas_index_generation=0;
 if(!sync_load_policy())return *sync_policy.candidate?DAV_ERR_SOURCE:DAV_ERR_HTTP_INCOMPLETE;
 strcpy(nas_index_generation,sync_policy.generation);
 bool received=nas_index_fetch(nas_index_add,r,nas_index_generation);r->status=nas_last_http;
 if(!received)return r->error==DAV_OK?DAV_ERR_HTTP_INCOMPLETE:r->error;
 // No omitted ancestors: every non-root entry must have a directory parent.
 for(dav_tree_entry_t *e DAV_AUTO=dav_tree_first(&r->tree,0);e;e=dav_tree_next(&r->tree,e)){
  char parent[DAV_PATH_LIMIT];strcpy(parent,e->path);char *slash=strrchr(parent,'/');if(slash)*slash=0;else *parent=0;
  const dav_tree_entry_t *p DAV_AUTO=dav_entry_hold(mirror_find(&r->tree,0,parent));if(!p||p->kind!=DAV_DIRECTORY)return DAV_ERR_PATH;
  if(e->kind==DAV_DIRECTORY){e->scanned=true;r->tree.scanned[0]++;}
 }
 return DAV_OK;
}
static bool nas_index_checkpoint(void)
{
 // Diagnostic comparison checkpoint only. Never authorises SD deletion or skips a scan.
 if(!*nas_index_generation||!app_access_allowed())return false;
 nvs_handle_t h;if(nvs_open("bernina_idx",NVS_READWRITE,&h)!=ESP_OK)return false;
 char old[65];size_t size=sizeof(old);esp_err_t e=nvs_get_str(h,"compared",old,&size);
 if(e!=ESP_OK||strcmp(old,nas_index_generation)){e=nvs_set_str(h,"compared",nas_index_generation);if(e==ESP_OK)e=nvs_commit(h);}
 if(e==ESP_OK){size=sizeof(old);e=nvs_get_str(h,"compared",old,&size);if(e==ESP_OK&&strcmp(old,nas_index_generation))e=ESP_FAIL;}
 if(e==ESP_OK)e=nvs_set_str(h,"source",sync_policy.source);
 if(e==ESP_OK)e=nvs_commit(h);
 if(e==ESP_OK){size=sizeof(old);e=nvs_get_str(h,"source",old,&size);if(e==ESP_OK&&strcmp(old,sync_policy.source))e=ESP_FAIL;}
 nvs_close(h);return e==ESP_OK;
}
