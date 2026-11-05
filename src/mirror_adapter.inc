// Manual mirror: same serialized MSC worker and checked APP ownership as Tests18/19.
// Independent sticky NVS journal survives individual Test19 transaction disarm.
#define MIRROR_VERSION 0x42323001U
typedef struct {
 uint32_t version,cid[7],nonce,phase;
 uint64_t size;
 bool directory;
 char path[DAV_PATH_LIMIT];
 unsigned char base[32],hash[32],checksum[32];
} mirror_journal_t;
static mirror_journal_t mirror_journal;
static uint32_t mirror_plan_card[7];
static unsigned char mirror_plan_base[32];
static char mirror_known_temp[40];
static uint64_t mirror_prepare_ms,mirror_capture_ms,mirror_source_ms,mirror_run_ms;
static void mirror_phase(const char *text){xSemaphoreTake(state_lock,portMAX_DELAY);snprintf(mirror_reason,sizeof(mirror_reason),"%s",text);xSemaphoreGive(state_lock);event_log(text);}

static void mirror_phase_callback(void *unused,const char *text){(void)unused;mirror_phase(text);}
static char mirror_failure_path[DAV_PATH_LIMIT],mirror_failure_reason[224];
static int mirror_failure_http;
static void mirror_file_failure(const dav_tree_entry_t *entry,const char *reason,int status)
{
 xSemaphoreTake(state_lock,portMAX_DELAY);
 snprintf(mirror_failure_path,sizeof(mirror_failure_path),"%s",entry->path);
 snprintf(mirror_failure_reason,sizeof(mirror_failure_reason),"%s",reason?reason:"Unbekannter Dateifehler");
 mirror_failure_http=status;
 xSemaphoreGive(state_lock);
}

static unsigned char mirror_known_hash[32];
static uint64_t mirror_known_size;
static bool mirror_digest(const void *data,size_t size,unsigned char out[32])
{
 mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);
 bool ok=mbedtls_sha256_starts(&sha,0)==0&&mbedtls_sha256_update(&sha,data,size)==0&&mbedtls_sha256_finish(&sha,out)==0;
 mbedtls_sha256_free(&sha);return ok;
}
static bool mirror_record_binding(void)
{
 uint32_t cid[7];unsigned char base[32];update_card_id(cid);
 return mirror_journal_valid&&mirror_digest(WEBDAV_URL,strlen(WEBDAV_URL),base)&&
  !memcmp(base,mirror_journal.base,32)&&!memcmp(cid,mirror_journal.cid,sizeof(cid));
}
static bool mirror_binding(void){return !mirror_lock||mirror_record_binding();}
static bool mirror_plan_binding(void)
{
 uint32_t cid[7];unsigned char base[32];update_card_id(cid);
 return !memcmp(cid,mirror_plan_card,sizeof(cid))&&mirror_digest(WEBDAV_URL,strlen(WEBDAV_URL),base)&&!memcmp(base,mirror_plan_base,32);
}
static bool mirror_owned(void *unused)
{
 (void)unused;uint32_t cid[7];update_card_id(cid);
 return app_access_allowed()&&!memcmp(cid,mirror_plan_card,sizeof(cid));
}
static void mirror_boot_journal(void)
{
 nvs_handle_t h;esp_err_t e=nvs_open("bernina_mir",NVS_READONLY,&h);
 if(e==ESP_ERR_NVS_NOT_FOUND)return;
 if(e!=ESP_OK){mirror_lock=true;mirror_state=MIRROR_FAILED;snprintf(mirror_reason,sizeof(mirror_reason),"Sync-NVS nicht lesbar; USB gesperrt, externe Pruefung erforderlich");return;}
 uint8_t pending=0;e=nvs_get_u8(h,"pending",&pending);
 if(e!=ESP_ERR_NVS_NOT_FOUND){
  mirror_lock=e!=ESP_OK||pending!=0;size_t size=sizeof(mirror_journal);unsigned char hash[32];
  // An inactive, valid COPY record may retain proof of an unpublished temp file.
  mirror_journal_valid=nvs_get_blob(h,"txn",&mirror_journal,&size)==ESP_OK&&size==sizeof(mirror_journal)&&
   mirror_journal.version==MIRROR_VERSION&&memchr(mirror_journal.path,0,sizeof(mirror_journal.path))&&mirror_journal.phase>=MIRROR_PHASE_COPY&&mirror_journal.phase<=MIRROR_PHASE_VERIFY&&
   mirror_digest(&mirror_journal,offsetof(mirror_journal_t,checksum),hash)&&!memcmp(hash,mirror_journal.checksum,32);
  if(mirror_lock){
   mirror_state=MIRROR_FAILED;
   snprintf(mirror_reason,sizeof(mirror_reason),"%s",mirror_journal_valid?"Sync unterbrochen: USB gesperrt. Erst Test19-Recovery falls offen, dann ausdruecklich Dry Run neu erstellen; Loeschfreigabe verfaellt":"Sync-Journal unlesbar: USB gesperrt; externe Pruefung erforderlich");
  }
 }
 nvs_close(h);
}
static bool mirror_persist(void *unused,mirror_phase_t phase,const char *path,bool directory,const unsigned char *hash,uint64_t size)
{
 (void)unused;if(!app_access_allowed()||update_lock||!mirror_binding()||!mirror_plan_binding()||strlen(path)>=sizeof(mirror_journal.path))return false;
 mirror_journal_t r={.version=MIRROR_VERSION,.nonce=mirror_plan.nonce,.phase=phase,.size=size,.directory=directory};
 update_card_id(r.cid);strcpy(r.path,path);if(hash)memcpy(r.hash,hash,32);
 if(!mirror_digest(WEBDAV_URL,strlen(WEBDAV_URL),r.base)||!mirror_digest(&r,offsetof(mirror_journal_t,checksum),r.checksum))return false;
 // Latch even if commit/readback becomes ambiguous. Never return USB on persistence failure.
 mirror_lock=true;mirror_journal_valid=false;
 nvs_handle_t h;if(nvs_open("bernina_mir",NVS_READWRITE,&h)!=ESP_OK)return false;
 esp_err_t e=nvs_set_blob(h,"txn",&r,sizeof(r));if(e==ESP_OK)e=nvs_set_u8(h,"pending",1);if(e==ESP_OK)e=nvs_commit(h);
 if(e==ESP_OK){mirror_journal_t v;size_t n=sizeof(v);uint8_t pending=0;
  if(nvs_get_blob(h,"txn",&v,&n)!=ESP_OK||n!=sizeof(v)||memcmp(&v,&r,sizeof(v))||nvs_get_u8(h,"pending",&pending)!=ESP_OK||pending!=1)e=ESP_FAIL;
 }
 nvs_close(h);
 if(e==ESP_OK){mirror_journal=r;mirror_journal_valid=true;}return e==ESP_OK;
}
static bool mirror_finish(void *unused,bool exact)
{
 (void)unused;if(!exact&&*mirror_known_temp&&!mirror_persist(NULL,MIRROR_PHASE_COPY,mirror_known_temp,false,mirror_known_hash,mirror_known_size))return false;
 if(!app_access_allowed()||update_lock||!mirror_binding())return false;
 nvs_handle_t h;if(nvs_open("bernina_mir",NVS_READWRITE,&h)!=ESP_OK)return false;
 esp_err_t e=nvs_set_u8(h,"pending",0);if(e==ESP_OK)e=nvs_commit(h);
 if(e==ESP_OK){uint8_t pending=1;if(nvs_get_u8(h,"pending",&pending)!=ESP_OK||pending)e=ESP_FAIL;}
 nvs_close(h);if(e==ESP_OK)mirror_lock=false;return e==ESP_OK;
}
typedef struct {dav_tree_t *tree;const char *parent;} mirror_emit_context_t;
static bool mirror_emit(void *context,const dav_entry_t *entry)
{
 mirror_emit_context_t *c=context;return mirror_source_entry(c->tree,c->parent,entry);
}
static bool mirror_source_directory(dav_tree_t *tree,const char *relative)
{
 if(!app_access_allowed())return false;
 char *url=comparison_url(relative);if(!url)return false;
 mirror_emit_context_t emit={tree,relative};dav_stream_t *parser=dav_stream_new(url,mirror_emit,&emit);
 esp_http_client_handle_t client=NULL;bool ok=false;if(!parser)goto done;
 esp_http_client_config_t config={.url=url,.username=WEBDAV_USER,.password=WEBDAV_PASSWORD,.auth_type=HTTP_AUTH_TYPE_BASIC,
  .cert_pem=synology_cert_start,.transport_type=HTTP_TRANSPORT_OVER_SSL,.disable_auto_redirect=true,.timeout_ms=15000,.buffer_size=2048};
 client=esp_http_client_init(&config);if(!client)goto done;
 static const char body[]="<?xml version=\"1.0\" encoding=\"utf-8\"?><d:propfind xmlns:d=\"DAV:\"><d:prop><d:resourcetype/><d:getcontentlength/><d:getetag/><d:getlastmodified/><d:creationdate/><d:getcontenttype/></d:prop></d:propfind>";
 if(esp_http_client_set_method(client,HTTP_METHOD_PROPFIND)!=ESP_OK||esp_http_client_set_header(client,"Depth","1")!=ESP_OK||
  esp_http_client_set_header(client,"Content-Type","application/xml; charset=utf-8")!=ESP_OK||esp_http_client_set_header(client,"Accept-Encoding","identity")!=ESP_OK||
  esp_http_client_open(client,sizeof(body)-1)!=ESP_OK)goto done;
 size_t sent=0;while(sent<sizeof(body)-1){int n=esp_http_client_write(client,body+sent,sizeof(body)-1-sent);if(n<=0)goto done;sent+=(size_t)n;}
 int64_t length=esp_http_client_fetch_headers(client);nas_last_http=esp_http_client_get_status_code(client);
 if(length<0||nas_last_http!=207)goto done;
 uint64_t received=0;
 for(;;){
  display_ui_cooperate(NULL);if(!app_access_allowed())goto done;
  char buffer[512];int n=esp_http_client_read(client,buffer,sizeof(buffer));if(n<0)goto done;if(!n)break;
  received+=(unsigned)n;if(dav_stream_feed(parser,buffer,(size_t)n)!=DAV_OK)goto done;
 }
 ok=esp_http_client_is_complete_data_received(client)&&(length==0||(uint64_t)length==received)&&dav_stream_finish(parser)==DAV_OK;
done:
 if(client)esp_http_client_cleanup(client);
 dav_stream_free(parser);free(url);return ok;
}
#if BERNINA_NAS_INDEX
static bool nas_index_revalidate(void *context,const char *path,const dav_entry_t *entry)
{
 char parent[DAV_PATH_LIMIT];strcpy(parent,path);char *slash=strrchr(parent,'/');if(slash)*slash=0;else *parent=0;
 return mirror_source_entry(context,parent,entry);
}
#endif
static bool mirror_source(void *unused,dav_tree_t *tree)
{
#ifdef ESP_PLATFORM
 // Full inventory requests use their own TLS client; release the data client first.
 update_transport_t *transport=unused;if(transport&&transport->single.client){esp_http_client_cleanup(transport->single.client);transport->single.client=NULL;}
#else
 (void)unused;
#endif
if(!app_access_allowed()||!mirror_binding()||!mirror_plan_binding()||!wifi_has_ip||!ensure_network_time()||strncmp(WEBDAV_URL,"https://",8))return false;
 mirror_source_reset(tree);
#if BERNINA_NAS_INDEX
 {char token[65];return *nas_index_generation&&nas_index_fetch(nas_index_revalidate,tree,token)&&mirror_source_complete(tree);}
#endif
 for(const dav_tree_entry_t *n DAV_AUTO=dav_tree_first(tree,0);n;n=dav_tree_next(tree,n))if(n->kind==DAV_DIRECTORY&&!mirror_source_directory(tree,n->path))return false;
 return mirror_source_complete(tree);
}
static bool mirror_absent(void *unused,const char *path,bool directory)
{
 (void)unused;if(!app_access_allowed()||update_lock||!mirror_binding()||!mirror_plan_binding()||!wifi_has_ip)return false;
 char *url=comparison_url(path);if(!url)return false;
 if(!directory){size_t n=strlen(url);if(n&&url[n-1]=='/')url[n-1]=0;}
 esp_http_client_config_t c={.url=url,.username=WEBDAV_USER,.password=WEBDAV_PASSWORD,.auth_type=HTTP_AUTH_TYPE_BASIC,
  .cert_pem=synology_cert_start,.transport_type=HTTP_TRANSPORT_OVER_SSL,.disable_auto_redirect=true,.timeout_ms=15000,.buffer_size=2048};
 esp_http_client_handle_t client=esp_http_client_init(&c);free(url);if(!client)return false;
#if BERNINA_NAS_INDEX
 if(!*nas_index_generation||!nas_index_pin(client)){esp_http_client_cleanup(client);return false;}
#endif
 bool ok=esp_http_client_set_method(client,HTTP_METHOD_HEAD)==ESP_OK&&esp_http_client_open(client,0)==ESP_OK&&esp_http_client_fetch_headers(client)>=0;
 nas_last_http=esp_http_client_get_status_code(client);ok=ok&&nas_last_http==404;
 esp_http_client_cleanup(client);return ok&&app_access_allowed();
}
static void mirror_file_progress(void *unused,uint64_t bytes,uint64_t total,bool verifying)
{
 (void)unused;(void)total;
 xSemaphoreTake(state_lock,portMAX_DELAY);if(!verifying)mirror_bytes=mirror_base_bytes+bytes;uint64_t value=mirror_bytes;uint64_t size=mirror_plan.download;xSemaphoreGive(state_lock);
 display_ui_transfer(true,value,size,verifying);
}
static void mirror_progress(void *unused,uint64_t complete,uint64_t total,uint64_t bytes)
{
 (void)unused;(void)total;xSemaphoreTake(state_lock,portMAX_DELAY);mirror_completed=complete;mirror_base_bytes=mirror_bytes=bytes;xSemaphoreGive(state_lock);
 display_ui_transfer(true,bytes,mirror_plan.download,false);
 if(complete==1||complete==total||complete%500==0){char line[128];snprintf(line,sizeof(line),"SYNC: %" PRIu64 "/%" PRIu64 " Dateien, %" PRIu64 " Bytes",complete,total,bytes);event_log(line);}
}
static void mirror_prepare_transport(update_transport_t *t,const dav_tree_entry_t *n)
{
#ifdef ESP_PLATFORM
 t->keep_client=true;
 if(t->single.client&&!esp_http_client_is_complete_data_received(t->single.client)){esp_http_client_cleanup(t->single.client);t->single.client=NULL;}
#else
 if(t->single.client){esp_http_client_cleanup(t->single.client);t->single.client=NULL;}
#endif
 t->single.path=n->path;t->single.etag=n->etag;t->single.strict=true;t->single.status=0;t->content_verified=false;
 // update_current is reused verbatim and reads this confirmed length.
 update_record.new_size=n->size;
}
// A controlled network failure differs from power loss inside rename: the copy
// engine proves this handle was closed and publication was never attempted.
static void mirror_note_partial(update_transport_t *t,const char *temporary,uint64_t size)
{
 const char *name=strrchr(temporary,'/');name=name?name+1:temporary;
 if(strncmp(name,".B16-",5)||update_lock)return;
 update_ops_t u=update_options(t);u.io.owned=mirror_owned;unsigned char hash[32];
 if(mirror_hash_file(SD_PATH,name,size,&u.io,hash))mirror_persist(t,MIRROR_PHASE_COPY,name,false,hash,size);
}
static bool mirror_new_file(void *context,const dav_tree_entry_t *n,unsigned char *hash)
{
 update_transport_t *t=context;mirror_prepare_transport(t,n);
 update_ops_t u=update_options(t);u.io.owned=mirror_owned;u.io.progress=mirror_file_progress;u.io.nonce=esp_random();u.io.verify_content=update_content;u.io.before_publish=update_current;
 single_copy_result_t r;single_copy_run(SD_PATH,n->path,n->size,false,&u.io,&r);
 if(r.storage_error||r.handle_quarantined){io_fault=true;app_owned=false;usb_test_phase=3;}
 if(!r.success&&r.temporary_exists&&!r.publication_attempted&&!r.storage_error&&!r.handle_quarantined)mirror_note_partial(t,r.temporary,r.received);
 if(!r.success)mirror_file_failure(n,r.error,t->single.status);
 if(r.success)memcpy(hash,r.sd_hash,32);
 return r.success;
}
static bool mirror_replace_file(void *context,const dav_tree_entry_t *n,const dav_tree_entry_t *s,uint32_t nonce,const unsigned char *old,unsigned char *hash)
{
 update_transport_t *t=context;mirror_prepare_transport(t,n);
 memset(&update_record,0,sizeof(update_record));update_record.new_size=n->size;update_record.old_size=s->size;
 strcpy(update_record.target,n->path);strcpy(update_record.etag,n->etag);strcpy(update_record.modified,n->modified);memcpy(update_record.old_hash,old,32);
 update_names(&update_record,nonce);update_card_id(update_record.cid);update_record_loaded=true;
 update_ops_t u=update_options(t);u.io.owned=mirror_owned;u.io.progress=mirror_file_progress;u.skip_identical=true;
 update_result_t r;update_run(SD_PATH,&update_record,&u,&r);
 if(r.storage_error||r.quarantined){io_fault=true;app_owned=false;usb_test_phase=3;}
 if(r.partial_safe)mirror_note_partial(t,r.partial,r.partial_size);
 if(!r.success)mirror_file_failure(n,r.error,t->single.status);
 if(r.success)memcpy(hash,update_record.new_hash,32);
 return r.success;
}
static bool mirror_idle(void)
{
 return !mirror_pending&&!update_pending&&active_command==CMD_NONE&&!uxQueueMessagesWaiting(command_queue);
}
static bool mirror_fresh(void)
{
 return mirror_state==MIRROR_PREVIEW&&mirror_plan.valid&&comparison_result&&comparison_result->complete&&!comparison_stale&&
  mirror_revision==comparison_revision&&esp_timer_get_time()-mirror_selected_us<=BATCH_MAX_AGE*1000000LL&&usb_test_phase==2&&app_owned&&!update_lock&&mirror_binding()&&mirror_plan_binding();
}
static esp_err_t mirror_summary(httpd_req_t *req)
{
 esp_err_t e=httpd_resp_sendstr_chunk(req,"<p>Manueller Sync: ");if(e==ESP_OK)e=comparison_text(req,mirror_reason);
 char text[1024];snprintf(text,sizeof(text),"</p><p data-mirror-active='%u'>Neu %" PRIu64 ", geaendert/verdaechtig %" PRIu64 ", ungeklaert %" PRIu64 "; SD-Dateien entfernen %" PRIu64 " (eigene Alt-Artefakte %" PRIu64 "); Ordner +%" PRIu64 " / -%" PRIu64 "; Konflikte %" PRIu64 ".</p><p>Download %" PRIu64 "; benoetigt %" PRIu64 "; frei %" PRIu64 " Bytes. Dateien %" PRIu64 "/%" PRIu64 "; Daten %" PRIu64 "/%" PRIu64 " Bytes. Loeschfreigabe: %s. USB-Journalsperre: %s.</p>",
  mirror_pending?1:0,mirror_plan.new_files,mirror_plan.changed_files,mirror_plan.unknown_files,mirror_plan.delete_files,mirror_plan.internal_files,mirror_plan.create_dirs,mirror_plan.remove_dirs,mirror_plan.conflicts,
  mirror_plan.download,mirror_plan.required,mirror_plan.free_bytes,mirror_completed,mirror_plan.files,mirror_bytes,mirror_plan.download,mirror_plan.delete_allowed?"JA, separat bestaetigt":"GESPERRT",mirror_lock?"AKTIV":"nein");
 if(e==ESP_OK)e=httpd_resp_sendstr_chunk(req,text);
 snprintf(text,sizeof(text),"<p>Lauf: %" PRIu64 " Dateien geprueft; %" PRIu64 " SD-Dateien geloescht; %" PRIu64 " Ordner entfernt; %" PRIu64 " eigene neue Artefakte bereinigt.</p>",mirror_result.completed,mirror_result.deleted_files,mirror_result.deleted_dirs,mirror_result.cleaned);
 if(e==ESP_OK)e=httpd_resp_sendstr_chunk(req,text);
 snprintf(text,sizeof(text),"<p>Laufzeiten: Bestand %" PRIu64 " ms; Zielpruefung %" PRIu64 " ms; NAS-Bestaetigung %" PRIu64 " ms; Ausfuehrung %" PRIu64 " ms.</p>",mirror_prepare_ms,mirror_capture_ms,mirror_source_ms,mirror_run_ms);
 if(e==ESP_OK)e=httpd_resp_sendstr_chunk(req,text);
 if(e==ESP_OK&&*mirror_failure_reason){
  snprintf(text,sizeof(text),"<p><b>Dateioperation fehlgeschlagen</b>; Datei-HTTP %d (0 = keine HTTP-Antwort). Pfad: ",mirror_failure_http);
  e=httpd_resp_sendstr_chunk(req,text);
  if(e==ESP_OK)e=comparison_text(req,mirror_failure_path);
  if(e==ESP_OK)e=httpd_resp_sendstr_chunk(req,"<br>Ursache: ");
  if(e==ESP_OK)e=comparison_text(req,mirror_failure_reason);
  if(e==ESP_OK)e=httpd_resp_sendstr_chunk(req,"</p>");
 }
 if(e==ESP_OK&&mirror_state==MIRROR_PREVIEW&&!mirror_fresh())e=httpd_resp_sendstr_chunk(req,"<p>Plan VERALTET oder APP nicht bestaetigt: Dry Run neu erstellen. Keine gueltige Ausfuehrungsfreigabe.</p>");
 if(e==ESP_OK)e=httpd_resp_sendstr_chunk(req,mirror_plan.empty_source?"<p>NAS-Wurzel nach vollstaendiger Erfassung leer. Loeschfreigabe benoetigt Zusatzbestaetigung.</p>":mirror_plan.dangerous?"<p>Viele Loeschungen: Zusatzbestaetigung erforderlich.</p>":"<p>Eigene neue Sicherungen werden nur mit Loeschfreigabe nach Gesamtpruefung bereinigt.</p>");
 return e;
}
static esp_err_t mirror_report(httpd_req_t *req)
{
 if(mirror_pending||!comparison_result||mirror_revision!=comparison_revision)return httpd_resp_sendstr_chunk(req,"<p>Kein stabiler Plan; Dry Run erstellen.</p>");
 esp_err_t e=httpd_resp_sendstr_chunk(req,"<p>Alle Aktionen. Gleiche Groesse bleibt UNGEKLAERT; bedingter Download und SHA256 entscheiden. Eigene neue Backups/Journal/OLD werden nur mit Loeschfreigabe nach Gesamtpruefung entfernt. Kein NAS-Schreiben.</p><table border='1'><tr><th>Pfad</th><th>Aktion</th><th>NAS / SD Bytes</th><th>NAS ETag</th><th>NAS Last-Modified</th></tr>");
 dav_tree_t *t=&comparison_result->tree;
 for(unsigned side=0;side<2&&e==ESP_OK;side++)for(const dav_tree_entry_t *n DAV_AUTO=dav_tree_first(t,side);n&&e==ESP_OK;n=dav_tree_next(t,n))if(*n->path){
  bool ambiguous=false;const dav_tree_entry_t *s DAV_AUTO=dav_entry_hold(dav_tree_match(t,1-side,n,&ambiguous));if(side&&s&&!ambiguous)continue;
  dry_category_t c=dry_classify(t,side?s:n,side?n:s,ambiguous);
  const char *action=dry_category_name(c);
  if(c!=DRY_CONFLICT&&n->kind==DAV_DIRECTORY)action=side?"SD-Ordner entfernen (unten zuerst)":s?"Ordner erhalten":"Ordner erstellen (auch leer)";
  else if(side&&c!=DRY_CONFLICT)action=!strncmp(n->fat,".B",2)?"Eigenes Alt-Artefakt / SD-only entfernen (separate Freigabe)":"SD-only entfernen (separate Freigabe)";
  const dav_tree_entry_t *nas=side?s:n,*sd=side?n:s;
  if(nas&&sd&&strcmp(nas->path,sd->path))action="KONFLIKT: FAT-/NFC-Schreibweise auf NAS und SD verschieden";
  if(nas&&(!strncmp(nas->fat,".B16-",5)||!strncmp(nas->fat,".B19-",5)||!strncmp(nas->fat,".B20-",5)))action="KONFLIKT: NAS verwendet reservierten Hilfsnamen";
  if(nas&&nas->kind==DAV_FILE&&(!nas->size_known||!batch_strong_etag(nas->etag)))action="UNGEKLAERT / GESPERRT: NAS-Laenge oder starker ETag fehlt";
  if((nas&&nas->kind==DAV_FILE&&nas->size>UPDATE_LIMIT)||(sd&&sd->kind==DAV_FILE&&sd->size>UPDATE_LIMIT))action="GESPERRT: Datei groesser als 2147483647 Bytes (ESP VFS)";
  if(sd&&sd->kind==DAV_FILE&&!sd->size_known)action="GESPERRT: SD-Dateilaenge unbekannt";
  if(side&&!strncmp(n->fat,".B16-",5)&&(!mirror_record_binding()||strcmp(n->path,mirror_journal.path)))action="GESPERRT: unterbrochene eigene Temp-Datei; externe FAT-Pruefung";
  char size[96];snprintf(size,sizeof(size),"%" PRIu64 " / %" PRIu64,side?(s?s->size:0):n->size,side?n->size:s?s->size:0);
  const char *cells[]={n->path,action,size,side?s?s->etag:"":n->etag,side?s?s->modified:"":n->modified};
  e=httpd_resp_sendstr_chunk(req,"<tr>");for(unsigned i=0;i<5&&e==ESP_OK;i++)e=comparison_cell(req,cells[i]);if(e==ESP_OK)e=httpd_resp_sendstr_chunk(req,"</tr>");
 }
 return e==ESP_OK?httpd_resp_sendstr_chunk(req,"</table>"):e;
}
static esp_err_t mirror_handler(httpd_req_t *req,unsigned action)
{
 if(action>=3){
  httpd_resp_set_type(req,"text/html; charset=utf-8");xSemaphoreTake(state_lock,portMAX_DELAY);
  if(action==4&&comparison_result&&comparison_result->tree.disk&&active_command!=CMD_NONE){xSemaphoreGive(state_lock);httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Geraet arbeitet; Bericht nach Abschluss laden.");}
  esp_err_t e=action==4?mirror_report(req):mirror_summary(req);
  if(action==3&&e==ESP_OK&&mirror_fresh()){
   char text[144];snprintf(text,sizeof(text),"<p id='mirror-ticket' data-ticket='%s' data-danger='%u'>Plan gueltig, 5 Minuten. Separate Bestaetigung erforderlich.</p>",mirror_ticket,mirror_plan.dangerous?1:0);e=httpd_resp_sendstr_chunk(req,text);
  }
  xSemaphoreGive(state_lock);return e==ESP_OK?httpd_resp_send_chunk(req,NULL,0):e;
 }
 char ticket[17]={0},danger[32]={0};bool header=httpd_req_get_hdr_value_str(req,"X-Bernina-Selection",ticket,sizeof(ticket))==ESP_OK;
 bool extra=httpd_req_get_hdr_value_str(req,"X-Bernina-Danger",danger,sizeof(danger))==ESP_OK&&!strcmp(danger,"SPIEGEL LOESCHEN");
 xSemaphoreTake(state_lock,portMAX_DELAY);bool ok=mirror_idle()&&!update_lock;
 if(action==0){
  ok=ok&&mirror_binding()&&((usb_test_phase==2&&app_owned)||(!update_usb_ready&&mirror_lock&&!storage_fault&&!io_fault));
  if(ok){mirror_pending=true;mirror_state=MIRROR_PLANNING;mirror_plan.delete_allowed=mirror_plan.danger_confirmed=false;update_valid=false;
   command_t cmd=CMD_MIRROR_PLAN;if(xQueueSend(command_queue,&cmd,0)!=pdTRUE){mirror_pending=false;ok=false;}}
 }else{
  ok=ok&&header&&!strcmp(ticket,mirror_ticket)&&mirror_fresh();
  if(action==2){ok=ok&&(!mirror_plan.dangerous||extra);if(ok){mirror_plan.delete_allowed=true;mirror_plan.danger_confirmed=extra;}}
  else if(ok){mirror_pending=true;mirror_state=MIRROR_RUNNING;command_t cmd=CMD_MIRROR_RUN;if(xQueueSend(command_queue,&cmd,0)!=pdTRUE){mirror_pending=false;mirror_state=MIRROR_PREVIEW;ok=false;}}
 }
 if(ok)event_log_held(action==0?"Sync-Vorbereitung ausdruecklich eingereiht":action==2?"SD-only-Loeschfreigabe fuer aktuellen Plan bestaetigt":"NAS-Uebernahme fuer aktuellen Plan bestaetigt");
 else event_log_held("Sync-Steuerung abgelehnt: Voraussetzungen oder Bestaetigung fehlen");
 xSemaphoreGive(state_lock);httpd_resp_set_status(req,ok?"202 Accepted":"409 Conflict");
 return httpd_resp_sendstr(req,!ok?"GESPERRT: APP/Leerlauf, Journal/Recovery, aktueller Plan oder separate Zusatzbestaetigung fehlt":action==0?"Vollstaendiger Dry Run eingereiht; nur eigener Cache wird geschrieben, keine Nutzdateien veraendert; Loeschfreigabe AUS":action==2?"Loeschungen fuer genau diesen Plan separat freigegeben; Ausfuehrung nochmals bestaetigen":"Manuelle Synchronisation eingereiht; APP bleibt aktiv");
}
static void mirror_worker(command_t cmd)
{
 update_transport_t t={0};mbedtls_sha256_init(&t.single.sha);update_ops_t u=update_options(&t);
 mirror_ops_t ops={.io=u.io,.source=mirror_source,.absent=mirror_absent,.new_file=mirror_new_file,.replace_file=mirror_replace_file,.journal=mirror_persist,.finish=mirror_finish,.progress=mirror_progress,.phase=mirror_phase_callback};
 ops.io.owned=mirror_owned;
 mirror_completed=mirror_bytes=mirror_base_bytes=0;
 xSemaphoreTake(state_lock,portMAX_DELAY);mirror_failure_path[0]=mirror_failure_reason[0]=0;mirror_failure_http=0;xSemaphoreGive(state_lock);
 if(cmd==CMD_MIRROR_PLAN){
  mirror_prepare_ms=mirror_capture_ms=mirror_source_ms=mirror_run_ms=0;
  memset(&mirror_result,0,sizeof(mirror_result));
  if(!update_usb_ready&&mirror_lock&&!app_owned&&!storage_fault&&!io_fault&&mirror_binding()){
   usb_detached_by_app=true;if(mount_app()){app_owned=true;usb_test_phase=2;}
  }
  mirror_phase("Vorbereitung: NAS-/SD-Bestand erfassen");
  int64_t stage=esp_timer_get_time();
  bool reuse=comparison_result&&comparison_result->tree.count[0]&&comparison_result->tree.count[1]&&comparison_result->complete&&comparison_result->plan.complete&&!comparison_stale&&!comparison_running&&app_owned&&usb_test_phase==2&&!mirror_lock&&esp_timer_get_time()-comparison_completed_us<=BATCH_MAX_AGE*1000000LL;
 #if BERNINA_NAS_INDEX
  if(reuse&&!sync_policy_unchanged(nas_index_generation))reuse=false;
 #endif
  if(!reuse)compare_nas_sd();else event_log("Vorbereitung: aktueller APP-Vergleich wird wiederverwendet; Ziel und NAS werden erneut geprueft");
  mirror_prepare_ms=(esp_timer_get_time()-stage)/1000;
  update_card_id(mirror_plan_card);
  bool base_ok=mirror_digest(WEBDAV_URL,strlen(WEBDAV_URL),mirror_plan_base);
  bool complete=comparison_result&&comparison_result->complete&&comparison_result->capacity_known&&comparison_result->plan.complete;
  if(comparison_result)mirror_plan_build(&comparison_result->tree,complete,comparison_result->sd_free,esp_random(),&mirror_plan);else memset(&mirror_plan,0,sizeof(mirror_plan));
  bool uncertain_temp=false;const dav_tree_entry_t *known_temp DAV_AUTO=NULL;mirror_known_temp[0]=0;
  if(comparison_result)for(const dav_tree_entry_t *e DAV_AUTO=dav_tree_first(&comparison_result->tree,1);e;e=dav_tree_next(&comparison_result->tree,e))if(!strncmp(e->fat,".B16-",5)){
   bool known=mirror_record_binding()&&mirror_journal.phase==MIRROR_PHASE_COPY&&!strcmp(mirror_journal.path,e->path)&&e->kind==DAV_FILE&&e->size_known&&e->size==mirror_journal.size;
   if(known)dav_entry_assign(&known_temp,e);else {uncertain_temp=true;mirror_plan.conflicts++;mirror_plan.valid=false;}
  }
  mirror_phase("Vorbereitung: SD-Dateiinhalte und Zielzustand pruefen");
  stage=esp_timer_get_time();
  bool ok=base_ok&&mirror_plan.valid&&mirror_binding()&&mirror_capture(SD_PATH,&comparison_result->tree,&mirror_plan,&ops.io);
  mirror_capture_ms=(esp_timer_get_time()-stage)/1000;
  if(ok){mirror_phase("Vorbereitung: NAS-Generation vollstaendig bestaetigen");stage=esp_timer_get_time();ok=mirror_source(NULL,&comparison_result->tree);mirror_source_ms=(esp_timer_get_time()-stage)/1000;}
  if(ok&&known_temp&&memcmp(mirror_meta(known_temp)->hash,mirror_journal.hash,32)){ok=false;uncertain_temp=true;mirror_plan.conflicts++;}
  if(ok&&known_temp){
   if(strlen(known_temp->path)>=sizeof(mirror_known_temp))ok=false;
   else {strcpy(mirror_known_temp,known_temp->path);memcpy(mirror_known_hash,mirror_meta(known_temp)->hash,32);mirror_known_size=known_temp->size;}
  }
  xSemaphoreTake(state_lock,portMAX_DELAY);if(!ok)comparison_stale=true;mirror_plan.valid=ok;mirror_revision=comparison_revision;mirror_selected_us=esp_timer_get_time();
  snprintf(mirror_ticket,sizeof(mirror_ticket),"%08" PRIx32 "%08" PRIx32,esp_random(),esp_random());mirror_state=ok?MIRROR_PREVIEW:MIRROR_FAILED;
  snprintf(mirror_reason,sizeof(mirror_reason),"%s",uncertain_temp?"Unterbrochene neue Datei: B16-Temp-Datei koennte FAT-Rename-Doppelname sein. Keine Bereinigung; SD extern pruefen, Wiederaufnahme bleibt gesperrt":ok?"Dry Run vollstaendig: SD-SHA256 und NAS erneut geprueft. Loeschungen GESPERRT. Plan vor Ausfuehrung separat bestaetigen":"Dry Run GESPERRT: unvollstaendig, Konflikte/Metadaten, Speicher, APP oder Ziel-/NAS-Aenderung. Keine Schreiboperationen");xSemaphoreGive(state_lock);
 }else{
  mirror_phase("SYNC laeuft: Quell-/Zielpruefung, Ordner vorbereiten, Dateien uebertragen");
  int64_t stage=esp_timer_get_time();
  display_ui_transfer(true,0,mirror_plan.download,false);
  mirror_run(SD_PATH,&comparison_result->tree,&mirror_plan,&ops,&mirror_result);
  mirror_run_ms=(esp_timer_get_time()-stage)/1000;
  xSemaphoreTake(state_lock,portMAX_DELAY);mirror_state=mirror_result.state;snprintf(mirror_reason,sizeof(mirror_reason),"%s",mirror_result.error);
  comparison_stale=true;comparison_revision++;mirror_plan.valid=false;mirror_plan.delete_allowed=mirror_plan.danger_confirmed=false;update_valid=false;xSemaphoreGive(state_lock);
 }
 if(t.single.client)esp_http_client_cleanup(t.single.client);
 mbedtls_sha256_free(&t.single.sha);xSemaphoreTake(state_lock,portMAX_DELAY);mirror_pending=false;xSemaphoreGive(state_lock);
 event_log(mirror_reason);
 if(*mirror_failure_reason){event_log(mirror_failure_reason);event_log(mirror_failure_path);}
 display_ui_transfer(false,0,0,false);
}
