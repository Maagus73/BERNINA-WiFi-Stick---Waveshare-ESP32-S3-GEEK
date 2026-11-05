// Test19: bounded one-file job on the existing serialized MSC worker.
typedef struct {
 single_transport_t single; // first: existing streaming/hash callbacks may use this prefix
 char modified[96];
 bool digest_present,digest_invalid,content_verified;
 bool keep_client; // opt-in only for serialized mirror transfers; recovery defaults to fresh clients
 unsigned char digest[32];
} update_transport_t;
static esp_err_t update_http_event(esp_http_client_event_t *event)
{
 single_http_event(event);update_transport_t *t=event->user_data;
 if(!t||event->event_id!=HTTP_EVENT_ON_HEADER||!event->header_key||!event->header_value)return ESP_OK;
 const char *v=event->header_value;
 if(!strcasecmp(event->header_key,"Last-Modified"))snprintf(t->modified,sizeof(t->modified),"%s",v);
 bool modern=!strcasecmp(event->header_key,"Content-Digest"),legacy=!strcasecmp(event->header_key,"Digest");
 if((modern&&!strncmp(v,"sha-256",7)&&strncmp(v,"sha-256=:",9))||(legacy&&!strncmp(v,"sha-256",7)&&strncmp(v,"sha-256=",8))){t->digest_invalid=true;return ESP_OK;}
 if((modern&&!strncmp(v,"sha-256=:",9))||(legacy&&!strncmp(v,"sha-256=",8))){
  const char *encoded=v+(modern?9:8);size_t n=strlen(encoded),length=0;unsigned char digest[32];
  if(modern){if(!n||encoded[n-1]!=':'){t->digest_invalid=true;return ESP_OK;}n--;}
  bool valid=mbedtls_base64_decode(digest,sizeof(digest),&length,(const unsigned char *)encoded,n)==0&&length==32;
  if(!valid||(t->digest_present&&memcmp(t->digest,digest,32)))t->digest_invalid=true;
  else{memcpy(t->digest,digest,32);t->digest_present=true;}
 }
 return ESP_OK;
}
static bool update_open(void *context,uint64_t size,esp_http_client_method_t method)
{
 update_transport_t *t=context;single_transport_t *c=&t->single;
#ifdef ESP_PLATFORM
 if(c->client&&(!t->keep_client||!esp_http_client_is_complete_data_received(c->client))){esp_http_client_cleanup(c->client);c->client=NULL;}
#else
 if(c->client){esp_http_client_cleanup(c->client);c->client=NULL;}
#endif
 c->encoded=c->etag_seen=c->etag_mismatch=false;t->digest_present=t->digest_invalid=false;t->modified[0]=0;
 if(!wifi_has_ip||!ensure_network_time()||!batch_strong_etag(c->etag)||strncmp(WEBDAV_URL,"https://",8))return false;
 char *url=comparison_url(c->path);if(!url)return false;size_t n=strlen(url);if(n&&url[n-1]=='/')url[n-1]=0;
 esp_http_client_config_t config={.url=url,.username=WEBDAV_USER,.password=WEBDAV_PASSWORD,.auth_type=HTTP_AUTH_TYPE_BASIC,
  .cert_pem=synology_cert_start,.transport_type=HTTP_TRANSPORT_OVER_SSL,.disable_auto_redirect=true,.timeout_ms=15000,.buffer_size=2048,
  .buffer_size_tx=4096,.event_handler=update_http_event,.user_data=t};
#ifdef ESP_PLATFORM
 config.keep_alive_enable=t->keep_client;
 if(c->client){if(esp_http_client_set_url(c->client,url)!=ESP_OK){free(url);return false;}}
 else c->client=esp_http_client_init(&config);
#else
 c->client=esp_http_client_init(&config);
#endif
 free(url);if(!c->client)return false;
 #if BERNINA_NAS_INDEX
 if(!*nas_index_generation||!nas_index_pin(c->client))return false;
#endif
 if(esp_http_client_set_header(c->client,"Accept-Encoding","identity")!=ESP_OK||esp_http_client_set_header(c->client,"If-Match",c->etag)!=ESP_OK||
  esp_http_client_set_method(c->client,method)!=ESP_OK||esp_http_client_open(c->client,0)!=ESP_OK)return false;
 int64_t length=esp_http_client_fetch_headers(c->client);c->status=esp_http_client_get_status_code(c->client);
 return c->status==200&&length>=0&&(uint64_t)length==size&&c->etag_seen&&!c->etag_mismatch&&!c->encoded&&!t->digest_invalid;
}
static bool update_begin(void *c,uint64_t size){return update_open(c,size,HTTP_METHOD_GET);}
static bool update_current(void *c){return app_access_allowed()&&update_open(c,update_record.new_size,HTTP_METHOD_HEAD);}
static bool update_content(void *c,const unsigned char *hash)
{
 update_transport_t *t=c;bool ok=!t->digest_invalid&&(!t->digest_present||!memcmp(hash,t->digest,32));t->content_verified=ok&&t->digest_present;return ok;
}
static bool update_arm(void *unused,const update_record_t *r)
{
 (void)unused;if(!app_access_allowed())return false;
 nvs_handle_t h;if(nvs_open("bernina_upd",NVS_READWRITE,&h)!=ESP_OK)return false;
 esp_err_t e=nvs_set_blob(h,"txn",r,sizeof(*r));if(e==ESP_OK)e=nvs_set_u8(h,"pending",1);if(e==ESP_OK)e=nvs_commit(h);
 if(e==ESP_OK){
  update_record_t verify;size_t size=sizeof(verify);uint8_t pending=0;
  if(nvs_get_blob(h,"txn",&verify,&size)!=ESP_OK||size!=sizeof(verify)||memcmp(&verify,r,sizeof(verify))||nvs_get_u8(h,"pending",&pending)!=ESP_OK||pending!=1)e=ESP_FAIL;
 }
 nvs_close(h);
 // Even an ambiguous persistence error keeps USB locked in the running session.
 xSemaphoreTake(state_lock,portMAX_DELAY);update_lock=true;update_record_loaded=true;xSemaphoreGive(state_lock);
 return e==ESP_OK;
}
static bool update_disarm(void *unused)
{
 (void)unused;if(!app_access_allowed())return false;
 nvs_handle_t h;if(nvs_open("bernina_upd",NVS_READWRITE,&h)!=ESP_OK)return false;
 esp_err_t e=nvs_set_u8(h,"pending",0);if(e==ESP_OK)e=nvs_commit(h);
 if(e==ESP_OK){uint8_t pending=1;if(nvs_get_u8(h,"pending",&pending)!=ESP_OK||pending!=0)e=ESP_FAIL;}
 nvs_close(h);
 if(e==ESP_OK){xSemaphoreTake(state_lock,portMAX_DELAY);update_lock=false;xSemaphoreGive(state_lock);}
 return e==ESP_OK;
}
static void update_boot_journal(void)
{
 nvs_handle_t h;esp_err_t e=nvs_open("bernina_upd",NVS_READONLY,&h);
 if(e==ESP_ERR_NVS_NOT_FOUND)return;
 if(e!=ESP_OK){update_lock=true;snprintf(update_reason,sizeof(update_reason),"NVS-Journal nicht lesbar; USB gesperrt");return;}
 uint8_t pending=0;e=nvs_get_u8(h,"pending",&pending);
 if(e==ESP_ERR_NVS_NOT_FOUND){nvs_close(h);return;}
 update_lock=e!=ESP_OK||pending!=0;
 if(update_lock){
  size_t size=sizeof(update_record);update_record_loaded=nvs_get_blob(h,"txn",&update_record,&size)==ESP_OK&&size==sizeof(update_record);
  if(update_record_loaded){
   update_transport_t t={0};mbedtls_sha256_init(&t.single.sha);
   single_copy_ops_t o={.context=&t,.hash_start=single_hash_start,.hash_update=single_hash_update,.hash_finish=single_hash_finish};
   update_record_loaded=update_record_valid(&update_record,&o);mbedtls_sha256_free(&t.single.sha);
  }
  if(!update_record_loaded)memset(&update_record,0,sizeof(update_record));
  snprintf(update_reason,sizeof(update_reason),"%s",update_record_loaded?"Offener Test19-Vorgang: USB gesperrt; Recovery ausdruecklich bestaetigen":"NVS-Journal beschaedigt oder Format unbekannt; USB gesperrt; externe Pruefung erforderlich");
 }
 nvs_close(h);
}
static bool update_usb_connect(void)
{
 if(update_controls_blocked())return false;
 mirror_state=MIRROR_IDLE;
 if(!update_usb_ready){
  tinyusb_config_t config=TINYUSB_DEFAULT_CONFIG();
  if(tinyusb_driver_install(&config)!=ESP_OK){io_fault=true;return false;}update_usb_ready=true;
 }
 return tud_connect();
}
static void update_progress(void *unused,uint64_t bytes,uint64_t total,bool verifying)
{
 (void)unused;xSemaphoreTake(state_lock,portMAX_DELAY);
 if(!verifying)update_received=bytes;
 update_verifying=verifying;uint64_t received=update_received;
 xSemaphoreGive(state_lock);display_ui_transfer(true,received,total,verifying);
}
static update_ops_t update_options(update_transport_t *transport)
{
 return (update_ops_t){.io={.context=transport,.owned=single_owned,.space=single_space,.begin=update_begin,.read=single_read,.complete=single_complete,
  .hash_start=single_hash_start,.hash_update=single_hash_update,.hash_finish=single_hash_finish,.publish=single_publish,.uppercase=comparison_upper,
  .nonce=esp_random(),.progress=update_progress,.size_limit=UPDATE_LIMIT,.cooperate=display_ui_cooperate},.content=update_content,.current=update_current,.arm=update_arm,.disarm=update_disarm};
}
static void update_card_id(uint32_t id[7])
{
 id[0]=(uint32_t)sd_card.cid.mfg_id;id[1]=(uint32_t)sd_card.cid.oem_id;
 id[2]=(uint32_t)sd_card.cid.revision;id[3]=(uint32_t)sd_card.cid.serial;id[4]=(uint32_t)sd_card.cid.date;
 memcpy(&id[5],sd_card.cid.name,8);
}
static bool update_idle(void)
{
 return !update_pending&&!batch_pending&&!single_selection.pending&&active_command==CMD_NONE&&!uxQueueMessagesWaiting(command_queue);
}
static esp_err_t update_select_handler(httpd_req_t *req)
{
 size_t n=httpd_req_get_url_query_len(req);char *encoded=NULL,*decoded=NULL;path_query_t *q=NULL;
 if(n>=3&&n<=3*(DAV_PATH_LIMIT-1)+2){encoded=malloc(n+1);decoded=malloc(DAV_PATH_LIMIT);q=malloc(sizeof(*q));}
 bool valid=encoded&&decoded&&q&&httpd_req_get_url_query_str(req,encoded,n+1)==ESP_OK&&!strncmp(encoded,"q=",2)&&!strchr(encoded,'&')&&
  path_query_decode(encoded+2,decoded,DAV_PATH_LIMIT)&&path_query_prepare(q,decoded,comparison_upper);
 free(encoded);free(decoded);if(!valid){free(q);httpd_resp_set_status(req,"400 Bad Request");return httpd_resp_sendstr(req,"Ungueltiger relativer Pfad / Suchspeicher fehlt");}
 xSemaphoreTake(state_lock,portMAX_DELAY);
 bool ok=update_idle()&&!update_lock&&!mirror_pending&&!mirror_lock&&comparison_result&&comparison_result->complete&&comparison_result->plan.complete&&!comparison_stale&&!comparison_running;
 const dav_tree_entry_t *nas DAV_AUTO=NULL,*sd DAV_AUTO=NULL;unsigned matches=0;
 if(ok)for(const dav_tree_entry_t *e DAV_AUTO=dav_tree_first(&comparison_result->tree,0);e;e=dav_tree_next(&comparison_result->tree,e))if(path_query_matches(q,e)){dav_entry_assign(&nas,e);matches++;}
 if(ok&&matches==1){bool ambiguous=false;dav_entry_assign(&sd,dav_tree_match(&comparison_result->tree,1,nas,&ambiguous));
  ok=sd&&!ambiguous&&!strcmp(sd->path,nas->path)&&nas->kind==DAV_FILE&&sd->kind==DAV_FILE&&nas->size_known&&sd->size_known&&
   nas->size<=UPDATE_TEST_LIMIT&&sd->size<=UPDATE_TEST_LIMIT&&batch_strong_etag(nas->etag)&&strncmp(nas->fat,".B19-",5)&&strncmp(nas->fat,".B16-",5)&&
   (dry_classify(&comparison_result->tree,nas,sd,false)==DRY_CHANGED||dry_classify(&comparison_result->tree,nas,sd,false)==DRY_UNKNOWN);
 }else ok=false;
 if(ok){
  update_display_reset();
  memset(&update_record,0,sizeof(update_record));strcpy(update_record.target,nas->path);strcpy(update_record.etag,nas->etag);strcpy(update_record.modified,nas->modified);snprintf(update_sd_modified,sizeof(update_sd_modified),"%s",sd->modified);
  update_record.new_size=nas->size;update_record.old_size=sd->size;update_names(&update_record,esp_random());update_card_id(update_record.cid);
  update_received=0;update_verifying=false;update_finished=false;update_succeeded=update_failed=0;update_revision=comparison_revision;update_pending=true;update_valid=false;update_record_loaded=true;
  snprintf(update_reason,sizeof(update_reason),"Manuelle Pruefung eingereiht; noch keine Schreibfreigabe");
  command_t cmd=CMD_UPDATE_PREPARE;if(xQueueSend(command_queue,&cmd,0)!=pdTRUE){update_pending=false;ok=false;}
 }
 xSemaphoreGive(state_lock);free(q);httpd_resp_set_status(req,ok?"202 Accepted":"409 Conflict");
 return httpd_resp_sendstr(req,ok?"Genau eine vorhandene Datei wird lesend geprueft; Status laden":"Auswahl gesperrt: vollstaendiger aktueller Vergleich, eindeutige Datei auf beiden Seiten, starker ETag, maximal 10 MiB je Version und Leerlauf erforderlich");
}
static esp_err_t update_start_handler(httpd_req_t *req)
{
 char ticket[17]={0};bool header=httpd_req_get_hdr_value_str(req,"X-Bernina-Selection",ticket,sizeof(ticket))==ESP_OK;
 xSemaphoreTake(state_lock,portMAX_DELAY);
 bool ok=header&&update_idle()&&!update_lock&&!mirror_pending&&!mirror_lock&&update_valid&&!strcmp(ticket,update_ticket)&&update_revision==comparison_revision&&!comparison_stale&&
  esp_timer_get_time()-update_selected_us<=BATCH_MAX_AGE*1000000LL&&usb_test_phase==2&&app_owned;
 if(ok){update_received=0;update_verifying=false;snprintf(update_reason,sizeof(update_reason),"SYNC aktiv: Download, SD-Pruefung und gesicherte Einzeldateiersetzung");update_finished=false;update_succeeded=update_failed=0;update_valid=false;update_pending=true;command_t cmd=CMD_UPDATE_RUN;if(xQueueSend(command_queue,&cmd,0)!=pdTRUE){update_pending=false;ok=false;}}
 xSemaphoreGive(state_lock);httpd_resp_set_status(req,ok?"202 Accepted":"409 Conflict");
 return httpd_resp_sendstr(req,ok?"Ersetzung genau dieser Datei separat bestaetigt; Backup bleibt erhalten; APP bleibt aktiv":"Bestaetigung fehlt/veraltet, bereits verwendet oder APP/Leerlauf fehlt");
}
static esp_err_t update_recovery_handler(httpd_req_t *req)
{
 char confirm[2]={0};bool header=httpd_req_get_hdr_value_str(req,"X-Bernina-Recovery",confirm,sizeof(confirm))==ESP_OK&&confirm[0]=='1';
 xSemaphoreTake(state_lock,portMAX_DELAY);bool ok=header&&update_idle()&&update_lock&&update_record_loaded;
 if(ok){update_pending=true;command_t cmd=CMD_UPDATE_RECOVER;if(xQueueSend(command_queue,&cmd,0)!=pdTRUE){update_pending=false;ok=false;}}
 xSemaphoreGive(state_lock);httpd_resp_set_status(req,ok?"202 Accepted":"409 Conflict");
 return httpd_resp_sendstr(req,ok?"Recovery ausdruecklich eingereiht; keine unbekannten Dateien ueberschreiben":"Keine bestaetigte lesbare Recovery / beschaeftigt");
}
static esp_err_t update_status_handler(httpd_req_t *req)
{
 httpd_resp_set_type(req,"text/html; charset=utf-8");xSemaphoreTake(state_lock,portMAX_DELAY);
 esp_err_t err=httpd_resp_sendstr_chunk(req,"<p>");if(err==ESP_OK)err=comparison_text(req,update_reason);
 if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,"</p><p>Pfad: ");
 if(err==ESP_OK)err=comparison_text(req,update_record_loaded?update_record.target:"noch keiner");
 char line[512];
 snprintf(line,sizeof(line),"</p><p data-update-active='%u'>%s; %" PRIu64 "/%" PRIu64 " Bytes heruntergeladen.</p><progress aria-label='Test19 Download' max='%" PRIu64 "' value='%" PRIu64 "' style='width:100%%'></progress><p>NAS:",
  update_pending?1:0,update_pending?(update_verifying?"SYNC: SD-PRUEFUNG":"Test19 aktiv"):(update_lock?"RECOVERY ERFORDERLICH":"APP / Leerlauf"),update_received,update_record.new_size,
  update_record.new_size?update_record.new_size:1,update_record.new_size?update_received:(update_succeeded?1:0));
 if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,line);
 snprintf(line,sizeof(line)," %" PRIu64 " Bytes; SD: %" PRIu64 " Bytes. Einstufung: %s.</p><p>NAS PROPFIND ETag: ",update_record.new_size,update_record.old_size,
  update_record.new_size!=update_record.old_size?"GEAENDERT / VERDAECHTIG: unterschiedliche Groessen":"UNGEKLAERT: gleiche Groesse beweist keine Inhaltsgleichheit");
 if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,line);
 if(err==ESP_OK)err=comparison_text(req,update_record.etag);
 if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,"; Last-Modified: ");
 if(err==ESP_OK)err=comparison_text(req,update_record.modified);
 if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,"; SD-Zeit im Vergleich (FAT): ");
 if(err==ESP_OK)err=comparison_text(req,update_sd_modified);
 if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,"</p><p>Letzter bedingter HTTP-Status: ");
 snprintf(line,sizeof(line),"%d; Last-Modified aus HTTP: ",nas_last_http);if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,line);
 if(err==ESP_OK)err=comparison_text(req,update_http_modified);
 snprintf(line,sizeof(line),"; SHA256-Serverheader: %s. ETag ist ein Versionsmerkmal, kein Inhalts-Hash. SD-Zeit nicht direkt mit NAS-Zeit vergleichbar.</p><p>USB-Journalsperre: %s; APP bleibt nach dem Vorgang aktiv.</p>",update_http_digest?"vorhanden":"nicht geliefert / nicht unterstuetzt",update_lock?"AKTIV":"nein");
 if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,line);
 if(update_valid){snprintf(line,sizeof(line),"<p id='update-ticket' data-ticket='%s'>SD-SHA256 erfasst; genau diese Datei kann separat bestaetigt werden.</p>",update_ticket);if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,line);}
 xSemaphoreGive(state_lock);return err==ESP_OK?httpd_resp_send_chunk(req,NULL,0):err;
}
static void update_worker(command_t cmd)
{
 update_transport_t transport={.single={.path=update_record.target,.etag=update_record.etag,.status=-1,.strict=true}};
 mbedtls_sha256_init(&transport.single.sha);update_ops_t ops=update_options(&transport);update_result_t result={0};bool ok=false;
 uint32_t cid[7];update_card_id(cid);
 if(memcmp(cid,update_record.cid,sizeof(cid))){result.error="Andere SD-Karte als im bestaetigten Journal; kein Zugriff";}
 else if(cmd==CMD_UPDATE_RECOVER){
  if(!update_usb_ready&&!app_owned&&!storage_fault&&!io_fault){
   // No USB driver was installed on this boot: there can be no host access.
   usb_detached_by_app=true;if(mount_app()){app_owned=true;usb_test_phase=2;}
  }
  update_recover(SD_PATH,&update_record,&ops,&result);ok=result.success;
 }else if(cmd==CMD_UPDATE_PREPARE){
  ok=update_revision==comparison_revision&&!comparison_stale&&app_access_allowed()&&update_current(&transport)&&
   update_hash_file(SD_PATH,update_record.target,update_record.old_size,&ops.io,update_record.old_hash);
  xSemaphoreTake(state_lock,portMAX_DELAY);
  update_valid=ok;update_selected_us=esp_timer_get_time();snprintf(update_ticket,sizeof(update_ticket),"%08" PRIx32 "%08" PRIx32,esp_random(),esp_random());
  xSemaphoreGive(state_lock);
  result.error=ok?"Vorschau bereit: SD-SHA256 und bedingte NAS-Metadaten geprueft; separate Ersetzung bestaetigen":"Lesende Pruefung fehlgeschlagen: APP, NAS-ETag/HEAD-Laenge oder SD-Pfad pruefen; nichts ersetzt";
 }else{
  if(update_revision==comparison_revision&&!comparison_stale&&app_access_allowed()){
   display_ui_transfer(true,0,update_record.new_size,false);update_run(SD_PATH,&update_record,&ops,&result);ok=result.success;
  }else result.error="Auswahl seit Bestaetigung veraltet; nichts ersetzt";
 }
 if(transport.single.client)esp_http_client_cleanup(transport.single.client);
 mbedtls_sha256_free(&transport.single.sha);
 xSemaphoreTake(state_lock,portMAX_DELAY);
 nas_last_http=transport.single.status;snprintf(update_http_modified,sizeof(update_http_modified),"%s",transport.modified);update_http_digest=transport.digest_present||transport.content_verified;
 update_pending=false;snprintf(update_reason,sizeof(update_reason),"%s",result.error?result.error:"Unbekannter Fehler");
 if(cmd!=CMD_UPDATE_PREPARE){update_finished=true;update_succeeded=ok?1:0;update_failed=ok?0:1;comparison_stale=true;comparison_revision++;single_selection.valid=false;batch_plan.valid=false;update_valid=false;}
 xSemaphoreGive(state_lock);
 if(result.storage_error||result.quarantined){io_fault=true;app_owned=false;usb_test_phase=3;}
 event_log(update_reason);display_ui_transfer(false,0,0,false);
}
