/* Recovery is orchestration only. Existing mount, arm, repair and checkpoint guards remain authoritative. */
static void sync_diag_reset(void){memset(&sync_diagnostic,0,sizeof(sync_diagnostic));}
static void sync_diag_set(sync_failure_t kind,const char *stage){
 if(sync_diagnostic.kind==SYNC_FAIL_LOCAL||sync_diagnostic.kind==SYNC_FAIL_PERMANENT)return;
 sync_diagnostic.kind=kind;snprintf(sync_diagnostic.stage,sizeof(sync_diagnostic.stage),"%s",stage);
}
static void sync_diag_event(esp_http_client_event_t *event){
#ifdef ESP_PLATFORM
 if((event->event_id==HTTP_EVENT_ERROR||event->event_id==HTTP_EVENT_DISCONNECTED)&&event->data){
  int code=0,flags=0;int e=esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)event->data,&code,&flags);
  if(e||code||flags){sync_diagnostic.tls=e;sync_diagnostic.tls_code=code;sync_diagnostic.tls_flags=flags;if(flags||code==MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)sync_diag_set(SYNC_FAIL_PERMANENT,"Zertifikatspruefung");}
 }
 if(event->event_id==HTTP_EVENT_ERROR)sync_diag_set(SYNC_FAIL_TRANSIENT,"HTTPS-Transport");
#endif
 if(event->event_id==HTTP_EVENT_ON_HEADER&&event->header_key&&event->header_value&&!strcasecmp(event->header_key,"X-Bernina-Error")){
  size_t n=strlen(event->header_value);bool safe=n&&n<sizeof(sync_diagnostic.server);
  for(size_t i=0;i<n&&safe;i++)if((event->header_value[i]<'a'||event->header_value[i]>'z')&&event->header_value[i]!='_')safe=false;
  if(safe)snprintf(sync_diagnostic.server,sizeof(sync_diagnostic.server),"%s",event->header_value);
 }
}
static esp_err_t sync_http_open(esp_http_client_handle_t c,int length,const char *stage){
 sync_diag_reset();snprintf(sync_diagnostic.stage,sizeof(sync_diagnostic.stage),"%s",stage);
 esp_err_t e=esp_http_client_open(c,length);sync_diagnostic.esp=e;
 if(e!=ESP_OK){sync_diagnostic.socket=esp_http_client_get_errno(c);sync_diag_set(e==ESP_ERR_NO_MEM?SYNC_FAIL_LOCAL:SYNC_FAIL_TRANSIENT,stage);}
 return e;
}
static int64_t sync_http_headers(esp_http_client_handle_t c,const char *stage){
 int64_t n=esp_http_client_fetch_headers(c);sync_diagnostic.http=esp_http_client_get_status_code(c);nas_last_http=sync_diagnostic.http;
 if(n<0||sync_diagnostic.http!=200){sync_diagnostic.socket=esp_http_client_get_errno(c);sync_diag_set(sync_retry_http_class(n<0&&sync_diagnostic.http==200?0:sync_diagnostic.http,sync_diagnostic.server),stage);}
 return n;
}
static int sync_http_read(esp_http_client_handle_t c,char *data,int size,const char *stage){
 int n=esp_http_client_read(c,data,size);
 if(n<=0&&!esp_http_client_is_complete_data_received(c)){sync_diagnostic.socket=esp_http_client_get_errno(c);sync_diag_set(SYNC_FAIL_TRANSIENT,stage);}
 return n;
}
static bool recovery_active(void){return recovery_job.pending&&recovery_config.enabled&&!app_auto_paused;}
static bool recovery_save_job(void){
 nvs_handle_t h;if(nvs_open("bernina_retry",NVS_READWRITE,&h)!=ESP_OK)return false;
 esp_err_t e=nvs_set_blob(h,"job",&recovery_job,sizeof(recovery_job));if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);return e==ESP_OK;
}
static void recovery_clear(void){
 if(recovery_job.pending){recovery_job.pending=0;if(!recovery_save_job())event_log("Wiederaufnahme: Status nicht dauerhaft gespeichert");}
 recovery_failures=0;recovery_next_us=0;
}
static void recovery_resume(void){if(recovery_active()){fast_paused=true;recovery_next_us=esp_timer_get_time()+(int64_t)sync_retry_delay(&recovery_config,1)*1000000;}}
static void recovery_manual_start(void){recovery_clear();recovery_error[0]=0;}
static bool recovery_set_config(const sync_retry_config_t *candidate){
 if(!sync_retry_config_valid(candidate))return false;
 struct {uint32_t version;sync_retry_config_t config;} record={SYNC_RETRY_CONFIG_VERSION,*candidate};
 nvs_handle_t h;if(nvs_open("bernina_retry",NVS_READWRITE,&h)!=ESP_OK)return false;
 esp_err_t e=nvs_set_blob(h,"config",&record,sizeof(record));if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);
 if(e!=ESP_OK)return false;
 recovery_config=*candidate;recovery_resume();return true;
}
static bool recovery_policy_valid(const char *policy){
 if(!*policy)return true;
 if(strlen(policy)!=129||policy[64]!=':')return false;
 char first[65];memcpy(first,policy,64);first[64]=0;return sync_hex(first)&&sync_hex(policy+65);
}
static void recovery_boot(void){
 if(!sync_retry_config_valid(&recovery_config))recovery_config=(sync_retry_config_t){0,{30,60,120,300}};
 nvs_handle_t h;if(nvs_open("bernina_retry",NVS_READONLY,&h)!=ESP_OK)return;
 struct {uint32_t version;sync_retry_config_t config;} config;size_t n=sizeof(config);
 if(nvs_get_blob(h,"config",&config,&n)==ESP_OK&&n==sizeof(config)&&config.version==SYNC_RETRY_CONFIG_VERSION&&sync_retry_config_valid(&config.config))recovery_config=config.config;
 n=sizeof(recovery_job);
 if(nvs_get_blob(h,"job",&recovery_job,&n)!=ESP_OK||n!=sizeof(recovery_job)||recovery_job.version!=1||recovery_job.pending>1||recovery_job.repair>1||!memchr(recovery_job.source,0,sizeof(recovery_job.source))||!memchr(recovery_job.policy,0,sizeof(recovery_job.policy))||(*recovery_job.source&&!sync_hex(recovery_job.source))||!recovery_policy_valid(recovery_job.policy))memset(&recovery_job,0,sizeof(recovery_job));
 nvs_close(h);
 if(recovery_job.pending){fast_paused=true;recovery_next_us=esp_timer_get_time()+(int64_t)sync_retry_delay(&recovery_config,1)*1000000;snprintf(recovery_error,sizeof(recovery_error),"NAS-Wiederaufnahme nach Neustart ausstehend");}
}
/* Validate the last completed SD marker independently of an unavailable NAS. Never clears pending. */
static bool recovery_checkpoint_valid(void){
 if(fast_lock||fast_armed_run||storage_fault||io_fault||update_lock||mirror_lock||!app_access_allowed()||fast_record.version!=FAST_VERSION)return false;
 uint32_t cid[7];unsigned char digest[32];update_card_id(cid);
 if(memcmp(cid,fast_record.cid,sizeof(cid))||!mirror_digest(&fast_record,offsetof(fast_record_t,checksum),digest)||memcmp(digest,fast_record.checksum,32))return false;
 FILE *f=fopen(FAST_MARKER,"rb");if(!f)return false;fast_record_t copy;
 bool ok=fread(&copy,1,sizeof(copy),f)==sizeof(copy)&&fgetc(f)==EOF&&!ferror(f)&&!memcmp(&copy,&fast_record,sizeof(copy));
 if(fclose(f)){fast_io_fault(NULL);return false;}return ok;
}
static void recovery_log_failure(void){
 char line[256];snprintf(line,sizeof(line),"NAS-Fehler: %.39s; HTTP %d; ESP %d; Socket %d; TLS %d/%d; Flags %d",sync_diagnostic.stage,sync_diagnostic.http,sync_diagnostic.esp,sync_diagnostic.socket,sync_diagnostic.tls,sync_diagnostic.tls_code,sync_diagnostic.tls_flags);event_log(line);
 if(*sync_diagnostic.server){snprintf(line,sizeof(line),"NAS-Servercode: %.64s",sync_diagnostic.server);event_log(line);}
 snprintf(recovery_error,sizeof(recovery_error),"%.39s; HTTP %d; ESP %d; Socket %d; TLS %d/%d; Flags %d",sync_diagnostic.stage,sync_diagnostic.http,sync_diagnostic.esp,sync_diagnostic.socket,sync_diagnostic.tls,sync_diagnostic.tls_code,sync_diagnostic.tls_flags);
}
static void recovery_wait(void){
 if(recovery_failures<UINT32_MAX)recovery_failures++;
 uint32_t delay=sync_retry_delay(&recovery_config,recovery_failures);recovery_next_us=esp_timer_get_time()+(int64_t)delay*1000000;fast_paused=true;
 char line[160];snprintf(line,sizeof(line),"NAS nicht erreichbar; %s; erneuter Versuch in %u s",recovery_job.repair?"Reparatur wartet, USB gesperrt":"SD unveraendert, Freigabe separat geprueft",(unsigned)delay);fast_note(line);
}
static void recovery_failure(void){
 if(storage_fault||io_fault||update_lock||mirror_lock||usb_test_phase==3)sync_diagnostic.kind=SYNC_FAIL_LOCAL;
 if(sync_diagnostic.kind==SYNC_FAIL_NONE)sync_diag_set(SYNC_FAIL_PERMANENT,fast_result.error?fast_result.error:"Pruefung oder Abschluss ungueltig");
 recovery_log_failure();
 if(sync_diagnostic.kind!=SYNC_FAIL_TRANSIENT){recovery_clear();fast_note("Sync gestoppt: technischer oder dauerhafter Fehler; Diagnose pruefen");return;}
 if(!recovery_job.pending){
  memset(&recovery_job,0,sizeof(recovery_job));recovery_job.version=1;recovery_job.pending=1;recovery_job.repair=fast_lock||remote_repair;
  const char *source=*sync_policy.source?sync_policy.source:fast_record.source;
  const char *policy=*sync_policy.policy?sync_policy.policy:fast_record.policy;
  snprintf(recovery_job.source,sizeof(recovery_job.source),"%s",source);snprintf(recovery_job.policy,sizeof(recovery_job.policy),"%s",policy);
  if(!recovery_save_job()){recovery_job.pending=0;fast_note("Wiederaufnahme nicht gespeichert; manuelle Pruefung erforderlich");return;}
 }else if(fast_lock&&!recovery_job.repair){recovery_job.repair=1;if(!recovery_save_job()){recovery_clear();return;}}
 bool safe=recovery_checkpoint_valid();
 if(storage_fault||io_fault){sync_diagnostic.kind=SYNC_FAIL_LOCAL;snprintf(sync_diagnostic.stage,sizeof(sync_diagnostic.stage),"SD-Abschlusspruefung fehlgeschlagen");recovery_log_failure();recovery_clear();fast_note("Wiederaufnahme gestoppt: SD-Fehler");return;}
 if(!safe&&!recovery_job.repair){recovery_job.repair=1;if(!recovery_save_job()){recovery_clear();fast_note("Reparaturabsicht nicht gespeichert; manuelle Pruefung erforderlich");return;}}
 if(safe){command_t c=CMD_CONNECT;mirror_state=MIRROR_IDLE;display_compare_failed=false;if(xQueueSend(command_queue,&c,0)!=pdTRUE)event_log("NAS-Ausfall: USB-Rueckgabe erneut anfordern");}
 recovery_wait();
 if(!recovery_config.enabled)fast_note("NAS-Ausfall: automatische Wiederaufnahme ausgeschaltet");
}
static bool recovery_policy_allowed(void){
 if(!recovery_executing)return true;
 if((*recovery_job.source&&strcmp(recovery_job.source,sync_policy.source))||(recovery_job.repair&&*recovery_job.policy&&strcmp(recovery_job.policy,sync_policy.policy))){sync_diag_set(SYNC_FAIL_PERMANENT,"Quelle oder Ausschlussregeln gewechselt");return false;}
 return true;
}
static bool recovery_schedule(void){
 if(!recovery_job.pending)return false;
 if(!recovery_active()||app_control_busy||app_archive_busy||fast_busy||!wifi_has_ip||storage_fault||io_fault||usb_test_phase==3||update_lock||mirror_lock||uxQueueMessagesWaiting(command_queue)||esp_timer_get_time()<recovery_next_us)return true;
 command_t c=CMD_AUTO_RETRY;xQueueSend(command_queue,&c,0);return true;
}
static void recovery_worker(void){
 if(!recovery_active())return;
 sync_diag_reset();event_log("Wiederaufnahme: NAS pruefen, SD bleibt zunaechst unveraendert");
 bool ready=wifi_has_ip&&ensure_network_time();
 if(!ready)sync_diag_set(SYNC_FAIL_TRANSIENT,"WLAN oder Netzwerkzeit fehlt");
 if(ready)ready=sync_recovery_probe(recovery_job.source,recovery_job.repair?recovery_job.policy:"");
 if(!ready){recovery_log_failure();if(sync_diagnostic.kind==SYNC_FAIL_TRANSIENT)recovery_wait();else{recovery_clear();fast_note("Wiederaufnahme gestoppt: Quelle, Zertifikat oder Konfiguration pruefen");}return;}
 if(!perform_app_handoff()){sync_diag_set(SYNC_FAIL_LOCAL,"APP-Uebergabe fehlgeschlagen");recovery_log_failure();recovery_clear();return;}
 recovery_executing=true;
 if(recovery_job.repair){fast_repair_worker();if(fast_result.success&&!fast_lock&&!storage_fault&&!io_fault){command_t c=CMD_CONNECT;xQueueSend(command_queue,&c,0);}}
 else{fast_return_usb=true;fast_worker();}
 recovery_executing=false;
}
