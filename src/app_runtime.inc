#include "app_settings.h"
#define APP_EVENT_PAGE_SIZE 16U
#define APP_JSON_CAPACITY 32768U
typedef struct {uint32_t version,value,hours,paused;uint64_t files,dirs;int64_t last_success;uint32_t known,result;} app_saved_t;
static app_saved_t app_saved;
static int64_t app_log_next;
static bool app_archive_online;
static bool app_save(void)
{
 nvs_handle_t h;if(nvs_open("bernina_ui",NVS_READWRITE,&h)!=ESP_OK)return false;
 esp_err_t e=nvs_set_blob(h,"settings",&app_saved,sizeof(app_saved));if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);return e==ESP_OK;
}
static void app_identify(void)
{
 unsigned char mac[6]={0};esp_read_mac(mac,ESP_MAC_WIFI_STA);
 for(unsigned i=0;i<6;i++)snprintf(app_device+2*i,3,"%02x",mac[i]);
 snprintf(app_boot_id,sizeof(app_boot_id),"%08x%08x",(unsigned)esp_random(),(unsigned)esp_random());
}
static void app_config_boot(void)
{
 memset(&app_saved,0,sizeof(app_saved));app_saved.version=1;app_saved.value=(SYNC_CHECK_INTERVAL_SECONDS+59)/60;app_saved.paused=!SYNC_AUTOMATIC_ENABLED;
 nvs_handle_t h;app_saved_t readback;size_t n=sizeof(readback);uint32_t seconds;
 if(nvs_open("bernina_ui",NVS_READONLY,&h)==ESP_OK){if(nvs_get_blob(h,"settings",&readback,&n)==ESP_OK&&n==sizeof(readback)&&readback.version==1&&readback.hours<=1&&readback.paused<=1&&app_interval_convert(readback.value,readback.hours,&seconds))app_saved=readback;nvs_close(h);}
 app_interval_convert(app_saved.value,app_saved.hours,&app_interval_seconds);app_interval_value=app_saved.value;app_interval_hours=app_saved.hours;
 app_auto_paused=app_saved.paused;if(app_auto_paused)fast_paused=true;
}
static void app_sync_finished(bool ok)
{
 xSemaphoreTake(state_lock,portMAX_DELAY);
 app_saved.result=ok?1:2;
 if(ok){time_t t=time(NULL);if(t>=1735689600)app_saved.last_success=t;
  if(app_meta_known){app_saved.files=app_meta_files;app_saved.dirs=app_meta_directories;app_saved.known=1;}
 }
 bool saved=app_save();xSemaphoreGive(state_lock);
 if(!saved)event_log("Status konnte nicht dauerhaft gespeichert werden");
}
/* Sequential worker transport: no second TLS client during file streaming. */
static cJSON *app_nas_json(const char *resource,const char *body)
{
 char *url=comparison_url(".bernina-events-v1");if(!url)return NULL;size_t n=strlen(url);if(n&&url[n-1]=='/')url[--n]=0;
 const char *query=strchr(resource,'?');
 if(query){char *expanded=realloc(url,n+strlen(query)+1);if(!expanded){free(url);return NULL;}url=expanded;strcpy(url+n,query);}
 esp_http_client_config_t cfg={.url=url,.username=WEBDAV_USER,.password=WEBDAV_PASSWORD,.auth_type=HTTP_AUTH_TYPE_BASIC,.cert_pem=synology_cert_start,.transport_type=HTTP_TRANSPORT_OVER_SSL,.disable_auto_redirect=true,.timeout_ms=7000,.buffer_size=2048};
 esp_http_client_handle_t c=esp_http_client_init(&cfg);free(url);if(!c)return NULL;
 cJSON *out=NULL;char *buffer=NULL;bool complete=false;size_t size=body?strlen(body):0;
 if(body){esp_http_client_set_method(c,HTTP_METHOD_POST);esp_http_client_set_header(c,"Content-Type","application/json");}
 if(esp_http_client_open(c,(int)size)!=ESP_OK)goto done;
 for(size_t written=0;written<size;){int got=esp_http_client_write(c,body+written,(int)(size-written));if(got<=0)goto done;written+=(size_t)got;}
 int64_t length=esp_http_client_fetch_headers(c);
 if(esp_http_client_get_status_code(c)!=200||length<0||length>16384)goto done;
 buffer=heap_caps_malloc((size_t)length+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!buffer)goto done;
 size_t used=0;while(used<(size_t)length){int got=esp_http_client_read(c,buffer+used,(size_t)length-used);if(got<=0)goto done;used+=(size_t)got;}
 buffer[used]=0;complete=esp_http_client_is_complete_data_received(c);
 done:esp_http_client_cleanup(c);if(complete)out=cJSON_Parse(buffer);heap_caps_free(buffer);return out;
}
static void app_events_flush(void)
{
 int64_t now=esp_timer_get_time();
 if(!wifi_has_ip||time(NULL)<1735689600||now<app_log_next)return;
 if(xSemaphoreTake(state_lock,pdMS_TO_TICKS(100))!=pdTRUE)return;
 if(app_archive_busy||app_event_sent>=log_count){xSemaphoreGive(state_lock);return;}
 app_archive_busy=true;
 cJSON *payload=cJSON_CreateObject(),*events=cJSON_AddArrayToObject(payload,"events");
 if(!payload||!events){cJSON_Delete(payload);app_archive_busy=false;xSemaphoreGive(state_lock);app_log_next=now+15000000;return;}
 cJSON_AddStringToObject(payload,"device",app_device);cJSON_AddStringToObject(payload,"boot",app_boot_id);
 uint32_t batch_start=app_event_sent;uint32_t end=app_event_sent+16;if(end>log_count)end=log_count;
 for(uint32_t i=app_event_sent;i<end;i++){cJSON *e=cJSON_CreateObject();cJSON_AddNumberToObject(e,"seq",i);cJSON_AddNumberToObject(e,"ts",app_event_time[i%MAX_LOG]);cJSON_AddStringToObject(e,"message",logs[i%MAX_LOG]);cJSON_AddItemToArray(events,e);}
 xSemaphoreGive(state_lock);
 char *body=cJSON_PrintUnformatted(payload);cJSON_Delete(payload);
 cJSON *reply=body?app_nas_json(".bernina-events-v1",body):NULL;cJSON_free(body);
 bool ok=false;
 if(reply){cJSON *accepted=cJSON_GetObjectItem(reply,"accepted");ok=cJSON_IsNumber(accepted)&&accepted->valuedouble==(double)(end-batch_start);}
 cJSON_Delete(reply);
 xSemaphoreTake(state_lock,portMAX_DELAY);if(ok&&app_event_sent<end)app_event_sent=end;app_archive_online=ok;app_archive_busy=false;xSemaphoreGive(state_lock);
 app_log_next=now+(ok?1000000:15000000);
}
/* Serialize into bounded PSRAM, never an expanding internal-heap print buffer. */
static esp_err_t app_json_unavailable(httpd_req_t *req)
{
 httpd_resp_set_type(req,"application/json; charset=utf-8");
 httpd_resp_set_hdr(req,"Cache-Control","no-store");
 httpd_resp_set_status(req,"503 Service Unavailable");
 return httpd_resp_sendstr(req,"{\"message\":\"Protokoll/Status voruebergehend nicht verfuegbar; erneut versuchen\"}");
}
static esp_err_t app_json(httpd_req_t *req,cJSON *value,const char *status)
{
 char *text=value?heap_caps_malloc(APP_JSON_CAPACITY,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL;
 bool ready=text&&cJSON_PrintPreallocated(value,text,APP_JSON_CAPACITY,false);
 cJSON_Delete(value);
 if(!ready){heap_caps_free(text);return app_json_unavailable(req);}
 httpd_resp_set_type(req,"application/json; charset=utf-8");httpd_resp_set_hdr(req,"Cache-Control","no-store");if(status)httpd_resp_set_status(req,status);
 esp_err_t e=httpd_resp_sendstr(req,text);heap_caps_free(text);return e;
}
static esp_err_t app_message(httpd_req_t *r,const char *status,const char *message){cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"message",message);return app_json(r,o,status);}
static bool app_idle(void){return active_command==CMD_NONE&&!fast_busy&&!update_pending&&!mirror_pending&&!comparison_running&&!app_archive_busy&&!uxQueueMessagesWaiting(command_queue);}
static esp_err_t app_status(httpd_req_t *r)
{
 if(xSemaphoreTake(state_lock,pdMS_TO_TICKS(300))!=pdTRUE)return app_message(r,"503 Service Unavailable","Status beschaeftigt");
 bool busy=active_command!=CMD_NONE||fast_busy||update_pending||mirror_pending||comparison_running||uxQueueMessagesWaiting(command_queue);bool locked=fast_lock||update_lock||mirror_lock||storage_fault||io_fault||usb_test_phase==3;
 cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"version",APP_VERSION);cJSON_AddBoolToObject(o,"busy",busy);cJSON_AddBoolToObject(o,"wifi_connected",wifi_has_ip);
 cJSON_AddNumberToObject(o,"uptime_seconds",esp_timer_get_time()/1000000);cJSON_AddNumberToObject(o,"interval_seconds",app_interval_seconds);cJSON_AddNumberToObject(o,"interval_value",app_interval_value);cJSON_AddStringToObject(o,"interval_unit",app_interval_hours?"hours":"minutes");cJSON_AddBoolToObject(o,"paused",recovery_job.pending?app_auto_paused||!recovery_config.enabled:busy?app_auto_paused:fast_paused);
 cJSON_AddBoolToObject(o,"retry_enabled",recovery_config.enabled);cJSON_AddBoolToObject(o,"retry_pending",recovery_job.pending);cJSON_AddBoolToObject(o,"retry_waiting",recovery_active());cJSON_AddBoolToObject(o,"retry_repair",recovery_job.repair);cJSON_AddNumberToObject(o,"retry_attempts",recovery_failures);cJSON_AddNumberToObject(o,"retry_seconds",recovery_next_us>esp_timer_get_time()?(recovery_next_us-esp_timer_get_time())/1000000:0);cJSON_AddStringToObject(o,"retry_error",recovery_error);
 cJSON *delays=cJSON_AddArrayToObject(o,"retry_delays");for(unsigned n=0;n<4;n++)cJSON_AddItemToArray(delays,cJSON_CreateNumber(recovery_config.seconds[n]));
 cJSON_AddStringToObject(o,"storage",usb_test_phase==2&&app_owned?"APP":usb_test_phase==0&&!locked?"USB":"LOCKED");cJSON_AddBoolToObject(o,"locked",locked);
 cJSON_AddStringToObject(o,"operation",busy?"running":locked?"failed":"idle");cJSON_AddStringToObject(o,"last_result",app_saved.result==1?"completed":app_saved.result==2?"failed":"unknown");
 cJSON_AddNumberToObject(o,"last_success",app_saved.last_success);cJSON_AddBoolToObject(o,"inventory_known",app_saved.known);cJSON_AddBoolToObject(o,"inventory_valid",app_saved.known&&!locked&&!busy);cJSON_AddNumberToObject(o,"files",app_saved.files);cJSON_AddNumberToObject(o,"directories",app_saved.dirs);
 cJSON_AddNumberToObject(o,"progress",mirror_plan.download?(double)mirror_bytes*100.0/mirror_plan.download:0);cJSON_AddNumberToObject(o,"files_done",mirror_completed);cJSON_AddNumberToObject(o,"files_total",mirror_plan.files);cJSON_AddStringToObject(o,"phase",fast_reason);
 cJSON_AddBoolToObject(o,"screensaver",display_ui_matrix_enabled());cJSON_AddNumberToObject(o,"next_check_seconds",fast_next_us>esp_timer_get_time()?(fast_next_us-esp_timer_get_time())/1000000:0);
 cJSON_AddBoolToObject(o,"event_archive_online",app_archive_online);cJSON_AddNumberToObject(o,"events_pending",log_count-app_event_sent);cJSON_AddNumberToObject(o,"events_dropped",app_event_dropped);
 xSemaphoreGive(state_lock);
 esp_netif_ip_info_t ip;char url[64]="";if(wifi_netif&&esp_netif_get_ip_info(wifi_netif,&ip)==ESP_OK)snprintf(url,sizeof(url),"http://" IPSTR "/",IP2STR(&ip.ip));cJSON_AddStringToObject(o,"web_url",url);
 return app_json(r,o,NULL);
}
static bool app_read_key(httpd_req_t *r)
{
 unsigned char key[128]={0};if(httpd_req_get_hdr_value_str(r,"X-Bernina-Key",(char *)key,sizeof(key))!=ESP_OK)return false;
 size_t n=strlen((char *)key),expected=sizeof(WEB_CONTROL_PASSWORD)-1;unsigned diff=n!=expected;
 for(size_t i=0;i<expected;i++){diff|=key[i]^(unsigned char)WEB_CONTROL_PASSWORD[i];}
 mbedtls_platform_zeroize(key,sizeof(key));return diff==0;
}
typedef struct {unsigned sequence;time_t time;char message[192];} app_event_snapshot_t;
static bool app_event_cursor(const char *query,const char *key,unsigned long *value)
{
 char text[24]="0";
 esp_err_t e=httpd_query_key_value(query,key,text,sizeof(text));
 if(e!=ESP_OK&&e!=ESP_ERR_NOT_FOUND)return false;
 if(!*text)return false;
 for(const char *p=text;*p;p++)if(*p<'0'||*p>'9')return false;
 errno=0;char *end;*value=strtoul(text,&end,10);return !errno&&!*end&&*value<=UINT32_MAX;
}
static esp_err_t app_events(httpd_req_t *r)
{
 if(!app_read_key(r))return app_message(r,"403 Forbidden","Passwort fehlt oder ist ungueltig");
 char query[160]={0};unsigned long after=0,local_after=0;
 esp_err_t query_status=httpd_req_get_url_query_str(r,query,sizeof(query));
 if((query_status!=ESP_OK&&query_status!=ESP_ERR_NOT_FOUND)||!app_event_cursor(query,"after",&after)||!app_event_cursor(query,"local_after",&local_after))return app_message(r,"400 Bad Request","Ungueltiger Cursor");
 time_t now=time(NULL),start=0,stop=86400;struct tm local;bool time_ready=app_day_range(now,&start,&stop);
 cJSON *out=NULL;bool can=false;
 xSemaphoreTake(state_lock,portMAX_DELAY);if(!app_control_busy&&app_idle()&&wifi_has_ip&&time_ready){app_archive_busy=true;can=true;}xSemaphoreGive(state_lock);
 if(can){char resource[224];snprintf(resource,sizeof(resource),".bernina-events-v1?device=%s&from=%lld&to=%lld&after=%lu&limit=%u",app_device,(long long)start,(long long)stop,after,APP_EVENT_PAGE_SIZE);out=app_nas_json(resource,NULL);}
 /* An incomplete/invalid page must never advance the archive cursor. */
 if(out){cJSON *rows=cJSON_GetObjectItem(out,"events"),*next=cJSON_GetObjectItem(out,"next"),*more=cJSON_GetObjectItem(out,"more");
  if(!cJSON_IsArray(rows)||cJSON_GetArraySize(rows)>(int)APP_EVENT_PAGE_SIZE||!cJSON_IsNumber(next)||next->valuedouble<after||next->valuedouble>UINT32_MAX||!cJSON_IsBool(more)||(cJSON_IsTrue(more)&&next->valuedouble<=after)){cJSON_Delete(out);out=NULL;}
 }
 bool archived=out!=NULL;
 if(!out){out=cJSON_CreateObject();if(!out||!cJSON_AddArrayToObject(out,"events")||!cJSON_AddNumberToObject(out,"next",after)||!cJSON_AddBoolToObject(out,"more",false))goto unavailable;}
 if(!cJSON_AddBoolToObject(out,"archive_available",archived)||!cJSON_AddBoolToObject(out,"busy",!can))goto unavailable;
 cJSON *pending=cJSON_AddArrayToObject(out,"local");if(!pending)goto unavailable;
 app_event_snapshot_t *snapshot=heap_caps_malloc(APP_EVENT_PAGE_SIZE*sizeof(*snapshot),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!snapshot)goto unavailable;
 /* Snapshot only one small page; no JSON allocation while holding state_lock. */
 xSemaphoreTake(state_lock,portMAX_DELAY);unsigned first=log_count>MAX_LOG?log_count-MAX_LOG:0;
 bool gap=local_after<first||local_after>log_count;
 unsigned at=local_after>log_count?first:(local_after<first?first:(unsigned)local_after),count=0;
 while(at<log_count&&count<APP_EVENT_PAGE_SIZE){unsigned i=at++;time_t t=app_event_time[i%MAX_LOG];if(time_ready&&t&&(t<start||t>=stop))continue;
  snapshot[count].sequence=i;snapshot[count].time=t;memcpy(snapshot[count].message,logs[i%MAX_LOG],sizeof(snapshot[count].message));snapshot[count].message[191]=0;count++;
 }
 bool local_more=at<log_count;xSemaphoreGive(state_lock);
 bool complete=true;
 for(unsigned i=0;i<count&&complete;i++){
  cJSON *e=cJSON_CreateObject();
  if(!e||!cJSON_AddStringToObject(e,"boot",app_boot_id)||!cJSON_AddNumberToObject(e,"seq",snapshot[i].sequence)||!cJSON_AddNumberToObject(e,"ts",snapshot[i].time)||!cJSON_AddStringToObject(e,"message",snapshot[i].message)){cJSON_Delete(e);complete=false;break;}
  if(!cJSON_AddItemToArray(pending,e)){cJSON_Delete(e);complete=false;}
 }
 heap_caps_free(snapshot);if(!complete)goto unavailable;
 if(!cJSON_AddNumberToObject(out,"local_next",at)||!cJSON_AddBoolToObject(out,"local_more",local_more)||!cJSON_AddBoolToObject(out,"local_gap",gap)||!cJSON_AddStringToObject(out,"boot",app_boot_id))goto unavailable;
 char day[16];localtime_r(&now,&local);strftime(day,sizeof(day),"%Y-%m-%d",&local);
 if(!cJSON_AddStringToObject(out,"day",time_ready?day:"unknown"))goto unavailable;
 esp_err_t result=app_json(r,out,NULL);
 if(can){xSemaphoreTake(state_lock,portMAX_DELAY);app_archive_busy=false;xSemaphoreGive(state_lock);}
 return result;
 unavailable:cJSON_Delete(out);
 if(can){xSemaphoreTake(state_lock,portMAX_DELAY);app_archive_busy=false;xSemaphoreGive(state_lock);}
 return app_json_unavailable(r);
}
static esp_err_t app_api_control(httpd_req_t *r)
{
 char action[32]={0};httpd_req_get_hdr_value_str(r,"X-Bernina-Action",action,sizeof(action));
 xSemaphoreTake(state_lock,portMAX_DELAY);bool idle=app_idle();xSemaphoreGive(state_lock);
 if(!idle)return app_message(r,"409 Conflict","Ein Vorgang laeuft. Bitte warten.");
 if(!strcmp(r->uri,"/api/config")){
  if(httpd_req_get_hdr_value_len(r,"X-Bernina-Retry")){
   char enabled[4]={0},delays[64]={0};sync_retry_config_t candidate;
   if(httpd_req_get_hdr_value_len(r,"X-Bernina-Interval")||httpd_req_get_hdr_value_str(r,"X-Bernina-Retry",enabled,sizeof(enabled))!=ESP_OK||httpd_req_get_hdr_value_str(r,"X-Bernina-Retry-Delays",delays,sizeof(delays))!=ESP_OK||(strcmp(enabled,"0")&&strcmp(enabled,"1"))||!sync_retry_delays_parse(delays,candidate.seconds))return app_message(r,"400 Bad Request","Wiederaufnahme: vier aufsteigende Wartezeiten von 1 bis 86400 Sekunden");
   candidate.enabled=enabled[0]=='1';if(!recovery_set_config(&candidate))return app_message(r,"500 Internal Server Error","Wiederaufnahme nicht gespeichert");event_log("Einstellungen fuer NAS-Wiederaufnahme gespeichert");return app_message(r,NULL,"Wiederaufnahme gespeichert; kein Neustart erforderlich");
  }
  char value[16]={0},unit[16]={0};httpd_req_get_hdr_value_str(r,"X-Bernina-Interval",value,sizeof(value));httpd_req_get_hdr_value_str(r,"X-Bernina-Unit",unit,sizeof(unit));
  char *end;unsigned long n=strtoul(value,&end,10);uint32_t seconds;
  if(!*value||*end||n>UINT32_MAX||(strcmp(unit,"minutes")&&strcmp(unit,"hours"))||!app_interval_convert((uint32_t)n,!strcmp(unit,"hours"),&seconds))return app_message(r,"400 Bad Request","Intervall: 1 Minute bis 7 Tage");
  app_saved_t old=app_saved;app_saved.value=n;app_saved.hours=!strcmp(unit,"hours");if(!app_save()){app_saved=old;return app_message(r,"500 Internal Server Error","Speichern fehlgeschlagen");}
  xSemaphoreTake(state_lock,portMAX_DELAY);app_interval_value=n;app_interval_hours=app_saved.hours;app_interval_seconds=seconds;fast_next_us=esp_timer_get_time()+(int64_t)seconds*1000000;xSemaphoreGive(state_lock);char message[96];snprintf(message,sizeof(message),"Abgleichintervall gespeichert: %lu %s",n,app_interval_hours?"Stunden":"Minuten");event_log(message);return app_message(r,NULL,"Intervall gespeichert; kein Neustart erforderlich");
 }
 if(!strcmp(action,"pause")||!strcmp(action,"resume")){
  app_saved_t old=app_saved;app_saved.paused=!strcmp(action,"pause");if(!app_save()){app_saved=old;return app_message(r,"500 Internal Server Error","Speichern fehlgeschlagen");}
  app_auto_paused=app_saved.paused;fast_paused=app_auto_paused;if(!fast_paused)fast_next_us=esp_timer_get_time()+(int64_t)app_interval_seconds*1000000;if(!app_auto_paused)recovery_resume();event_log(app_auto_paused?"Automatik pausiert":"Automatik freigegeben");return app_message(r,NULL,app_auto_paused?"Automatik pausiert":"Automatik freigegeben");
 }
 if(!strcmp(action,"screensaver_on")||!strcmp(action,"screensaver_off")){if(display_ui_set_matrix(!strcmp(action,"screensaver_on"))!=ESP_OK)return app_message(r,"409 Conflict","Anzeige ist nicht bereit");event_log(!strcmp(action,"screensaver_on")?"Bildschirmschoner eingeschaltet":"Bildschirmschoner ausgeschaltet");return app_message(r,NULL,"Bildschirmschoner eingestellt");}
 command_t cmd=CMD_NONE;
 if(!strcmp(action,"app"))cmd=CMD_DISCONNECT;
 else if(!strcmp(action,"sync"))cmd=CMD_FAST_RUN;
 else if(!strcmp(action,"usb"))cmd=CMD_CONNECT;
 else if(!strcmp(action,"repair")){char phrase[40]={0};httpd_req_get_hdr_value_str(r,"X-Bernina-Confirm",phrase,sizeof(phrase));if(strcmp(phrase,"DAS KANN LANGE DAUERN"))return app_message(r,"409 Conflict","Bestaetigungstext fehlt");cmd=CMD_REPAIR_FLOW;}
 else if(!strcmp(action,"reboot")){char phrase[32]={0};httpd_req_get_hdr_value_str(r,"X-Bernina-Confirm",phrase,sizeof(phrase));if(strcmp(phrase,"NEUSTART"))return app_message(r,"409 Conflict","Neustart bestaetigen");cmd=CMD_APP_REBOOT;}
 if(cmd==CMD_NONE)return app_message(r,"400 Bad Request","Unbekannte Aktion");
 if(cmd!=CMD_APP_REBOOT&&(storage_fault||io_fault))return app_message(r,"409 Conflict","Technischer Fehler; Status pruefen");
 if((cmd==CMD_FAST_RUN||cmd==CMD_CONNECT)&&(!app_access_allowed()||update_lock||mirror_lock||((cmd==CMD_CONNECT||cmd==CMD_FAST_RUN)&&fast_lock)))return app_message(r,"409 Conflict","APP fehlt oder Journal sperrt diese Aktion");
 if(cmd==CMD_REPAIR_FLOW&&(update_lock||mirror_lock))return app_message(r,"409 Conflict","Offene Datei-Recovery zuerst beheben");
 if(cmd==CMD_DISCONNECT&&usb_test_phase!=0&&!(usb_test_phase==2&&app_access_allowed()))return app_message(r,"409 Conflict","Besitzwechsel gesperrt");
 xSemaphoreTake(state_lock,portMAX_DELAY);bool queued=app_idle()&&xQueueSend(command_queue,&cmd,0)==pdTRUE;xSemaphoreGive(state_lock);
 if(!queued)return app_message(r,"409 Conflict","Steuerung beschaeftigt");
 char message[80];snprintf(message,sizeof(message),"Aktion angenommen: %s",action);event_log(message);return app_message(r,"202 Accepted","Auftrag angenommen");
}

static esp_err_t app_api(httpd_req_t *r)
{
 if(r->method==HTTP_GET){if(!strncmp(r->uri,"/api/status",11))return app_status(r);return app_events(r);}
 if(!control_authenticated(r))return app_message(r,"403 Forbidden","Passwort fehlt oder ist ungueltig");
 xSemaphoreTake(state_lock,portMAX_DELAY);bool idle=app_idle()&&!app_control_busy;if(idle)app_control_busy=true;xSemaphoreGive(state_lock);
 if(!idle)return app_message(r,"409 Conflict","Ein Vorgang laeuft. Bitte warten.");
 esp_err_t result=app_api_control(r);
 xSemaphoreTake(state_lock,portMAX_DELAY);app_control_busy=false;xSemaphoreGive(state_lock);return result;
}

static void app_repair_flow(void)
{
 fast_paused=true;fast_return_usb=false;
 if(!perform_app_handoff()){app_sync_finished(false);event_log("Reparatur: APP-Uebergabe fehlgeschlagen; USB bleibt gesperrt");return;}
 fast_repair_worker();
 if(fast_result.success&&!fast_lock&&!storage_fault&&!io_fault){command_t next=CMD_CONNECT;if(xQueueSend(command_queue,&next,0)!=pdTRUE)event_log("Reparatur erfolgreich; USB-Rueckgabe erneut anfordern");}
}
