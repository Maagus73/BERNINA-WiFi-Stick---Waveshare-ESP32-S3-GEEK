#include "fast_sync.h"
#include "sync_config.h"
#define FAST_BASELINE SD_PATH "/BNCACHE/APPLIED24.IDX"
#define FAST_MARKER SD_PATH "/BNCACHE/APPLIED25.OK"
#define FAST_TEMP SD_PATH "/BNCACHE/TRANSFER24.TMP"
#define FAST_VERSION 0x42325001U
typedef struct {
    uint32_t version, cid [ 7 ];
    char generation [ 65 ], source [ 65 ], policy [ 130 ];
    unsigned char baseline [ 32 ], checksum [ 32 ];
} fast_record_t;
static fast_record_t fast_record;
static int64_t fast_next_us;
static bool fast_handoff, fast_return_usb, fast_force, fast_armed_run;
static fast_sync_result_t fast_result;
static uint64_t fast_file_base, fast_total_bytes, fast_elapsed_ms;
static int64_t fast_scan_logged;
static update_transport_t fast_transport;
static void fast_note(const char * text) {
    xSemaphoreTake(state_lock, portMAX_DELAY);
    snprintf(fast_reason, sizeof(fast_reason), "%s", text);
    xSemaphoreGive(state_lock);
    event_log(text);
}
static bool fast_store(bool pending, const fast_record_t * record) {
    nvs_handle_t h;
    if (nvs_open("bernina_auto", NVS_READWRITE, & h) != ESP_OK) return false;
    esp_err_t e = ESP_OK;
    if (record) e = nvs_set_blob(h, "applied", record, sizeof(* record));
    if (e == ESP_OK) e = nvs_set_u8(h, "pending", pending ? 1 : 0);
    if (e == ESP_OK) e = nvs_commit(h);
    if (e == ESP_OK) {
        uint8_t check = 2;
        if (nvs_get_u8(h, "pending", & check) != ESP_OK || check !=(pending ? 1 : 0)) e = ESP_FAIL;
        if (record) {
            fast_record_t copy;
            size_t size = sizeof(copy);
            if (nvs_get_blob(h, "applied", & copy, & size) != ESP_OK || size != sizeof(copy) || memcmp(record, & copy, size)) e = ESP_FAIL;
        }
    }
    nvs_close(h);
    return e == ESP_OK;
}
static void fast_boot(void) {
    fast_paused = app_auto_paused;
    fast_next_us = esp_timer_get_time() + SYNC_START_DELAY_SECONDS * 1000000LL;
    nvs_handle_t h;
    esp_err_t e = nvs_open("bernina_auto", NVS_READONLY, & h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return;
    if (e != ESP_OK) {
        fast_lock = true;
        fast_paused = true;
        return;
    }
    uint8_t pending = 0;
    e = nvs_get_u8(h, "pending", & pending);
    fast_lock =(e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND) || pending != 0;
    size_t size = sizeof(fast_record);
    if (nvs_get_blob(h, "applied", & fast_record, & size) != ESP_OK || size != sizeof(fast_record)) memset(& fast_record, 0, sizeof(fast_record));
    nvs_close(h);
    if (fast_lock) {
        fast_paused = true;
        snprintf(fast_reason, sizeof(fast_reason), "Sync unterbrochen; Reparatur erforderlich");
    }
}
static bool fast_owned(void * unused) {
    (void) unused;
    return app_access_allowed();
}
static void * fast_allocate(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static void fast_release(void * p) {
    heap_caps_free(p);
}
static bool fast_ignored(void * unused, const char * path, bool directory) {
    (void) unused;
    return sync_sd_ignored(path, directory);
}
static void fast_scan(void *unused,const char *text,uint64_t done,uint64_t total) {
    (void)unused;
    unsigned phase=strstr(text,"bereinigen")||strstr(text,"entfernen")?4:strstr(text,"Ordner")?5:strstr(text,"speichern")?7:strstr(text,"Manifest")?1:6;
    display_ui_compare(phase,(unsigned)done,(unsigned)total,(unsigned)done);
    char line[224];snprintf(line,sizeof(line),"%s: %" PRIu64 "/%" PRIu64 " Eintraege",text,done,total);
    xSemaphoreTake(state_lock,portMAX_DELAY);snprintf(fast_reason,sizeof(fast_reason),"%s",line);xSemaphoreGive(state_lock);
    int64_t now=esp_timer_get_time();
    if(!done||(total&&done==total)||now-fast_scan_logged>=5000000LL){event_log(line);fast_scan_logged=now;}
}
static void fast_phase(void * unused, const char * text) {
    (void) unused;
    xSemaphoreTake(state_lock,portMAX_DELAY);mirror_plan.files=fast_result.files;mirror_plan.download=fast_result.bytes;xSemaphoreGive(state_lock);
    fast_note(text);
    fast_scan(NULL,text,0,0);
}
static bool fast_arm(void * unused) {
    (void) unused;
    if (! app_access_allowed()) return false;
    fast_lock = true;
    fast_armed_run = true;
    return fast_store(true, NULL);
}
static bool fast_space(void * unused, uint64_t needed) {
    return single_space(unused, needed);
}
static void fast_progress(void * unused, uint64_t completed, uint64_t count, uint64_t received, uint64_t bytes) {
    (void) unused;
    fast_copying=true;
    fast_file_base = received;
    fast_total_bytes = bytes;
    xSemaphoreTake(state_lock, portMAX_DELAY);
    mirror_completed = completed;
    mirror_plan . files = count;
    mirror_bytes = received;
    mirror_plan . download = bytes;
    xSemaphoreGive(state_lock);
    display_ui_transfer(true, received, bytes, false);
    if (completed == 1 ||(completed && completed % 500 == 0) || completed == count) {
        char log [ 120 ];
        snprintf(log, sizeof(log), "Auto-Sync: %" PRIu64 "/%" PRIu64 " Dateien, %" PRIu64 "/%" PRIu64 " Bytes", completed, count, received, bytes);
        event_log(log);
    }
}
static void fast_http_close(void) {
    if (fast_transport . single . client) {
        esp_http_client_cleanup(fast_transport . single . client);
        fast_transport . single . client = NULL;
    }
}
static bool fast_copy(void * unused, const dav_tree_entry_t * entry) {
    (void) unused;
    fast_transport . keep_client = true;
    fast_transport.single.strict=true;
    fast_transport.content_verified=false;
    fast_transport . single . path = entry -> path;
    fast_transport . single . etag = entry -> etag;
    if(!fast_result.completed){char line[224];snprintf(line,sizeof(line),"Erster Download: %.180s",entry->path);event_log(line);}
    char destination [ DAV_PATH_LIMIT + 64 ];
    if (snprintf(destination, sizeof(destination), "%s/%s", SD_PATH, entry -> path) >=(int) sizeof(destination)) return false;
    unsigned char * buffer = heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (! buffer) return false;
    bool success = false;
    const char *failure="Auto-Sync: Download/SHA256/SD-Abschluss fehlgeschlagen";
    int saved_status = 0;
    for (unsigned attempt = 0; attempt < SYNC_NETWORK_ATTEMPTS && ! success; attempt ++) {
        int fd = - 1;
        bool disk_error = false;
        uint64_t used = 0;
        unsigned char digest [ 32 ];
        if (! app_access_allowed()) break;
        fast_transport . single . status = 0;
        if (! update_open(& fast_transport, entry -> size, HTTP_METHOD_GET)) {
            saved_status = fast_transport . single . status;
            failure="HTTPS-Kopfpruefung fehlgeschlagen: Status/Laenge/ETag/Kodierung/Digest";
            fast_http_close();
            if (saved_status > 0 && saved_status != 408 && saved_status != 429 && saved_status < 500) break;
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        saved_status = fast_transport . single . status;
        failure="Auto-Sync: Download/SHA256/SD-Abschluss fehlgeschlagen";
        if(!fast_result.completed)event_log("Erster Download: HTTP-Header bestaetigt, Daten lesen");
        if (! single_hash_start(& fast_transport . single)) break;
        if (! app_access_allowed()) break;
        fd = open(FAST_TEMP, O_CREAT | O_TRUNC | O_WRONLY, 0600);
        if (fd < 0) {
            disk_error = true;
            goto attempt_done;
        }
        while (used < entry -> size) {
            display_ui_cooperate(NULL);
            if (! app_access_allowed()) goto attempt_done;
            int got = esp_http_client_read(fast_transport . single . client, (char *) buffer, (entry -> size - used) > 16384 ? 16384 :(size_t)(entry -> size - used));
            if (got <= 0) goto attempt_done;
            if (! single_hash_update(& fast_transport . single, buffer, (size_t) got)) goto attempt_done;
            if (write(fd, buffer, (size_t) got) != got) {
                disk_error = true;
                goto attempt_done;
            }
            used +=(unsigned) got;
            xSemaphoreTake(state_lock, portMAX_DELAY);mirror_bytes=fast_file_base+used;xSemaphoreGive(state_lock);
            display_ui_transfer(true, fast_file_base + used, fast_total_bytes, false);
        }
        if (! esp_http_client_is_complete_data_received(fast_transport . single . client) || ! single_hash_finish(& fast_transport . single, digest) || ! update_content(& fast_transport, digest)) goto attempt_done;
        /* Manifest ETag is explicitly SHA256 in this NAS protocol, not arbitrary WebDAV. */
        char expected [ 65 ];
        for (unsigned i = 0; i < 32; i ++) snprintf(expected + i * 2, 3, "%02x", digest [ i ]);
        if (strlen(entry -> etag) != 66 || memcmp(expected, entry -> etag + 1, 64)) goto attempt_done;
        if (! app_access_allowed() || fsync(fd)) {
            disk_error = true;
            goto attempt_done;
        }
        {
            int closing = fd;
            fd = - 1;
            if (close(closing)) {
                disk_error = true;
                goto attempt_done;
            }
        }
        if (! app_access_allowed()) goto attempt_done;
        /* Disposable mirror: no independent backup. Dirty journal blocks USB on interruption. */
        if (unlink(destination) && errno != ENOENT) {
            disk_error = true;
            goto attempt_done;
        }
        if (! app_access_allowed() || rename(FAST_TEMP, destination)) {
            disk_error = true;
            goto attempt_done;
        }
        success = true;
        attempt_done : if (fd >= 0 && close(fd)) {
            disk_error = true;
        }
        if (! success) {
            fast_http_close();
            if (disk_error) {
                io_fault = true;
                app_owned = false;
                usb_test_phase = 3;
                break;
            }
        }
    }
    heap_caps_free(buffer);
    if (! success) {
        mirror_file_failure(entry, failure, saved_status);
        fast_note("Datei fehlgeschlagen; Automatik pausiert und USB bleibt gesperrt");
    }
    return success;
}
static bool fast_old_add(void * context, const char * path, const dav_entry_t * entry) {
    comparison_result_t * r = context;
    char parent [ DAV_PATH_LIMIT ];
    strcpy(parent, path);
    char * slash = strrchr(parent, '/');
    if (slash) * slash = 0;
    else * parent = 0;
    r -> error = comparison_add(r, 1, parent, entry);
    return r -> error == DAV_OK;
}
static void fast_io_fault(void *unused) {
    (void)unused;io_fault=true;app_owned=false;usb_test_phase=3;
}
static bool fast_file_hash(const char * path, unsigned char out [ 32 ]) {
    if (! app_access_allowed()) return false;
    FILE * f = fopen(path, "rb");
    if (! f) return false;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(& sha);
    bool ok = mbedtls_sha256_starts(& sha, 0) == 0;
    unsigned char * buffer = fast_allocate(16384);
    if (! buffer) ok = false;
    while (ok) {
        display_ui_cooperate(NULL);
        if (! app_access_allowed()) {
            ok = false;
            break;
        }
        size_t n = fread(buffer, 1, 16384, f);
        if (n && mbedtls_sha256_update(& sha, buffer, n)) ok = false;
        if (n < 16384) {
            if (ferror(f)) ok = false;
            break;
        }
    }
    if (ok) ok = mbedtls_sha256_finish(& sha, out) == 0;
    if (fclose(f)) {fast_io_fault(NULL);ok=false;}
    fast_release(buffer);
    mbedtls_sha256_free(& sha);
    return ok;
}
static bool fast_baseline_valid(void) {
    if(!app_access_allowed())return false;
    if(!memchr(fast_record.generation,0,sizeof(fast_record.generation)) ||
       !memchr(fast_record.source,0,sizeof(fast_record.source)) ||
       !memchr(fast_record.policy,0,sizeof(fast_record.policy)))return false;
    uint32_t cid [ 7 ];
    update_card_id(cid);
    unsigned char hash [ 32 ];
    if (fast_lock || fast_force || fast_record . version != FAST_VERSION || memcmp(cid, fast_record . cid, sizeof(cid)) || strcmp(sync_policy . source, fast_record . source) || strcmp(sync_policy . policy, fast_record . policy) || ! mirror_digest(& fast_record, offsetof(fast_record_t, checksum), hash) || memcmp(hash, fast_record . checksum, 32)) return false;
    FILE * f = fopen(FAST_MARKER, "rb");
    if (! f) return false;
    fast_record_t check;
    bool ok = fread(& check, 1, sizeof(check), f) == sizeof(check) && fgetc(f) == EOF && ! ferror(f);
    if (fclose(f)) {fast_io_fault(NULL);ok=false;}
    return ok && ! memcmp(& check, & fast_record, sizeof(check));
}
static bool fast_load_old(comparison_result_t * r) {
    unsigned char hash [ 32 ];
    if (! fast_file_hash(FAST_BASELINE, hash) || memcmp(hash, fast_record . baseline, 32)) return false;
    FILE * f = fopen(FAST_BASELINE, "rb");
    if (! f) return false;
    nas_index_parser_t * parser = calloc(1, sizeof(* parser));
    if (! parser) {
        if(fclose(f))fast_io_fault(NULL);
        return false;
    }
    nas_index_init(parser, fast_old_add, r);
    bool ok = true;
    char bytes [ 2048 ];
    for (;;) {
        if (! app_access_allowed()) {
            ok = false;
            break;
        }
        size_t n = fread(bytes, 1, sizeof(bytes), f);
        if (n && ! nas_index_feed(parser, bytes, n)) {
            ok = false;
            break;
        }
        if (n < sizeof(bytes)) {
            if (ferror(f)) ok = false;
            break;
        }
        display_ui_cooperate(NULL);
    }
    if (ok) ok = nas_index_finish(parser) && ! strcmp(parser -> token, fast_record . generation);
    if (fclose(f)) {fast_io_fault(NULL);ok=false;}
    free(parser);
    return ok;
}
static bool fast_save_baseline(dav_tree_t * tree) {
    if (! app_access_allowed()) return false;
    FILE * f = fopen(FAST_BASELINE, "wb");
    if (! f) return false;
    char * buffer = fast_allocate(32768);
    if (buffer) setvbuf(f, buffer, _IOFBF, 32768);
    bool ok = fprintf(f, "BINDEX1\t%s\n", nas_index_generation) > 0;
    size_t count = 0;
    for (dav_tree_entry_t * e DAV_AUTO = dav_tree_first(tree, 0); e && ok; e = dav_tree_next(tree, e)) if (* e -> path) {
        if (! app_access_allowed()) {
            ok = false;
            break;
        }
        char hash [ 65 ] = "-";
        if (e -> kind == DAV_FILE) {
            if (strlen(e -> etag) != 66) {
                ok = false;
                break;
            }
            memcpy(hash, e -> etag + 1, 64);
            hash [ 64 ] = 0;
        }
        ok = fprintf(f, "%c\t%" PRIu64 "\t%s\t", e -> kind == DAV_FILE ? 'F' : 'D', e -> kind == DAV_FILE ? e -> size : 0, hash) > 0;
        for (const unsigned char * p =(const unsigned char *) e -> path; * p && ok; p ++) {
            if ((* p >= 'a' && * p <= 'z') ||(* p >= 'A' && * p <= 'Z') ||(* p >= '0' && * p <= '9') || strchr("/-_.", * p)) ok = fputc(* p, f) != EOF;
            else ok = fprintf(f, "%%%02X", * p) > 0;
        }
        if (ok) ok = fputc('\n', f) != EOF;
        count ++;
        display_ui_cooperate(NULL);
    }
    if (ok) ok = fprintf(f, "END\t%zu\t%s\n", count, nas_index_generation) > 0;
    if (! dav_tree_healthy(tree)) ok = false;
    if (fflush(f) || fsync(fileno(f))) {fast_io_fault(NULL);ok=false;}
    if (fclose(f)) {fast_io_fault(NULL);ok=false;}
    fast_release(buffer);
    if (! ok) return false;
    fast_record_t record = {
        . version = FAST_VERSION
    };
    update_card_id(record . cid);
    strcpy(record . generation, nas_index_generation);
    strcpy(record . source, sync_policy . source);
    strcpy(record . policy, sync_policy . policy);
    if (! fast_file_hash(FAST_BASELINE, record . baseline) || ! mirror_digest(& record, offsetof(fast_record_t, checksum), record . checksum)) return false;
    if (! app_access_allowed()) return false;
    f = fopen(FAST_MARKER, "wb");
    if (! f) return false;
    ok = fwrite(& record, 1, sizeof(record), f) == sizeof(record);
    if (fflush(f) || fsync(fileno(f))) {fast_io_fault(NULL);ok=false;}
    if (fclose(f)) {fast_io_fault(NULL);ok=false;}
    if (! ok) return false;
    fast_record = record;
    return true;
}
static bool fast_inventory(bool * baseline) {
    fast_phase(NULL,"SD-Index fuer NAS-Liste oeffnen");
    if (! comparison_sd_discard()) return false;
    if (comparison_result) {
        dry_plan_release(& comparison_result -> plan);
        dav_tree_clear(& comparison_result -> tree);
        free(comparison_result);
        comparison_result = NULL;
    }
    comparison_result_t * r = calloc(1, sizeof(* r));
    if (! r) return false;
    comparison_result = r;
    comparison_stale = true;
    r -> tree . budget = dav_tree_platform_budget(& r -> psram_total, & r -> psram_free);
    dav_entry_t root = {
        . kind = DAV_DIRECTORY
    };
    bool ok = r -> tree . budget && comparison_sd_open(& r -> tree);
    r -> tree . ignore_path = sync_sd_ignored;
    if (ok) ok = dav_tree_add(& r -> tree, 0, "", & root, comparison_upper) == DAV_OK;
    /* Fetch a full, authenticated manifest before any destructive operation. */
    if(ok)fast_phase(NULL,"NAS-Manifest empfangen");
    if (ok) ok = nas_index_compare(r) == DAV_OK;
    if (ok) ok = dav_tree_add(& r -> tree, 1, "", & root, comparison_upper) == DAV_OK;
    if(ok&&*baseline)fast_phase(NULL,"Bestaetigten Altstand lesen");
    if (ok && * baseline && ! fast_load_old(r)) {
        /* Invalid cache never becomes an empty baseline. Rebuild from NAS. */
        * baseline = false;
        fast_force = true;
        fast_note("Baseline unlesbar: neuer NAS-Vollaufbau statt leerer Vergleichsbasis");
        return fast_inventory(baseline);
    }
    return ok && dav_tree_healthy(& r -> tree);
}
static bool fast_adopt_initial(void) {
    /* Bind only after a complete manifest, same rule as Test24. */
    return nas_index_checkpoint();
}
#include "remote_plan_adapter.inc"

static void fast_worker(void) {
    int64_t started_us=esp_timer_get_time();
    sync_diag_reset();
    fast_result=(fast_sync_result_t){0};
    fast_started = true;
    fast_busy = true;
    fast_copying=false;
    display_ui_compare(0,0,0,0);
    fast_armed_run = false;
    fast_paused = true;
    mirror_pending = true;
    mirror_state = MIRROR_RUNNING;
    display_compare_failed = false;
    xSemaphoreTake(state_lock, portMAX_DELAY);
    mirror_failure_path [ 0 ] = mirror_failure_reason [ 0 ] = 0;
    mirror_failure_http = 0;
    mirror_completed = mirror_bytes = 0;
    mirror_plan . files = mirror_plan . download = 0;
    xSemaphoreGive(state_lock);
    fast_note("Auto-Sync: NAS-Generation pruefen");
    bool ok = app_access_allowed() && ! update_lock && ! mirror_lock && wifi_has_ip && ensure_network_time();
    if(!ok)sync_diag_set(!app_access_allowed()||update_lock||mirror_lock?SYNC_FAIL_LOCAL:SYNC_FAIL_TRANSIENT,"APP-Freigabe oder Netzwerkzeit fehlt");
    bool baseline = false;
    if(fast_lock&&!fast_force&&!remote_repair){ok=false;fast_result.error="Unvollstaendiger Sync: Reparatur ohne Neu-Loeschung starten";}
    if (ok) ok = sync_load_policy();
    if (ok) ok = recovery_policy_allowed();
    if (ok) {
        baseline = !remote_repair&&fast_baseline_valid();
        if (baseline && ! strcmp(sync_policy . generation, fast_record . generation)) {
            fast_note("Auto-Sync: NAS unveraendert; kein Dateivergleich erforderlich");
            goto complete;
        }
    }
    if(ok)ok=remote_run(baseline);
    complete : if (! comparison_sd_discard()){ok=false;sync_diag_set(SYNC_FAIL_LOCAL,"SD-Cache schliessen fehlgeschlagen");}
    if (ok && fast_armed_run) {
        ok = fast_store(false, & fast_record);
        if (ok) fast_lock = false;
    }
    fast_elapsed_ms=(esp_timer_get_time()-started_us)/1000;
    if(fast_result.success&&!ok)fast_result.error="NAS-Abschluss, Baseline oder Cache-Close fehlgeschlagen";
    fast_result.success=ok;
    app_sync_finished(ok);
    fast_force = false;
    fast_busy = false;
    fast_copying=false;
    mirror_pending = false;
    comparison_stale = true;
    mirror_state = ok ? MIRROR_COMPLETE : MIRROR_FAILED;
    if (ok) {
        recovery_clear();recovery_error[0]=0;
        fast_paused = app_auto_paused;
        fast_next_us = esp_timer_get_time() + app_interval_seconds * 1000000LL;
        fast_note("Auto-Sync abgeschlossen; naechste NAS-Pruefung nach Intervall");
    } else {
        if (! fast_lock && fast_armed_run) fast_lock = true;
        recovery_failure();
    }
    display_ui_transfer(false, 0, 0, false);
    if (ok && fast_return_usb) {
        command_t c = CMD_CONNECT;
        xQueueSend(command_queue, & c, 0);
    }
    fast_return_usb = false;
}
static void fast_reset_worker(void) {
    fast_started = true;
    fast_paused = true;
    fast_handoff = false;
    fast_return_usb = false;
    if (storage_fault || io_fault) {
        fast_note("Reset verweigert: echter MSC-/SD-Fehler; kein Alles-okay erzwingen");
        return;
    }
    if (! app_owned && ! update_usb_ready) {
        usb_detached_by_app = true;
        if (mount_app()) {
            app_owned = true;
            usb_test_phase = 2;
        }
    }
    if (! app_owned || usb_test_phase != 2 || ! app_access_allowed()) {
        fast_note("Reset: erst APP uebernehmen");
        return;
    }
    if (update_lock) {
        fast_note("Reset: offene Dateioperation gezielt wiederherstellen");
        update_worker(CMD_UPDATE_RECOVER);
        if (update_lock || storage_fault || io_fault) {
            fast_note("Reset gesperrt: Datei-Recovery nicht abgeschlossen");
            return;
        }
    }
    if (! comparison_sd_discard()) {
        fast_note("Reset gesperrt: Cache nicht geschlossen");
        return;
    }
    /* Explicitly authorised abandonment of old disposable mirror job. No full comparison. */
    nvs_handle_t h;
    esp_err_t e = nvs_open("bernina_mir", NVS_READWRITE, & h);
    if (e == ESP_OK) {
        e = nvs_set_u8(h, "pending", 0);
        if (e == ESP_OK) e = nvs_commit(h);
        uint8_t pending = 1;
        if (e == ESP_OK &&(nvs_get_u8(h, "pending", & pending) != ESP_OK || pending)) e = ESP_FAIL;
        nvs_close(h);
    }
    fast_record_t empty = {
        0
    };
    if (e != ESP_OK || ! fast_store(false, & empty)) {
        fast_lock = true;
        fast_note("Reset nicht dauerhaft bestaetigt; USB gesperrt");
        return;
    }
    mirror_lock = false;
    mirror_journal_valid = false;
    mirror_known_temp [ 0 ] = 0;
    fast_lock = false;
    fast_record = empty;
    fast_force = true;
    fast_result =(fast_sync_result_t) {
        0
    };
    mirror_state = MIRROR_IDLE;
    mirror_plan . valid = false;
    mirror_plan . delete_allowed = false;
    display_compare_failed = false;
    comparison_stale = true;
    fast_return_usb = true;
    fast_note("Alter Sync verworfen, keine Nutzdaten geprueft. Neuer Aufbau freigegeben");
    fast_paused = false;
    fast_next_us = 0;
    /* Scheduler starts only after confirmed APP ownership. */
}
static void fast_schedule(void) {
    if(recovery_schedule())return;
    if (app_control_busy || app_archive_busy || fast_paused || fast_busy || ! wifi_has_ip || storage_fault || io_fault || update_lock || mirror_lock || fast_lock || uxQueueMessagesWaiting(command_queue) || esp_timer_get_time() < fast_next_us) return;
    command_t command;
    if (usb_test_phase == 0) {
        command = CMD_DISCONNECT;
        fast_return_usb = true;
        fast_handoff = true;
    } else if (usb_test_phase == 2 && app_owned) {
        command = CMD_FAST_RUN;
        fast_handoff = false;
    } else return;
    xQueueSend(command_queue, & command, 0);
}
static esp_err_t fast_handler(httpd_req_t * req, unsigned action) {
    char phrase [ 40 ] = {
        0
    };
    httpd_req_get_hdr_value_str(req, "X-Bernina-Confirm", phrase, sizeof(phrase));
    xSemaphoreTake(state_lock, portMAX_DELAY);
    bool idle = active_command == CMD_NONE && ! uxQueueMessagesWaiting(command_queue) && ! fast_busy && ! mirror_pending && ! update_pending && ! comparison_running;
    bool ok = idle && ! storage_fault && ! io_fault;
    command_t command = CMD_NONE;
    if (action == 0) {
        ok = ok && ! strcmp(phrase, "RESET SYNC24.2");
        command = CMD_FAST_RESET;
    } else if (action == 1) {
        ok = ok && ! strcmp(phrase, "NAS AUF SD") && ! update_lock && ! mirror_lock && usb_test_phase == 2 && app_owned;
        command = CMD_FAST_RUN;
        if (ok) {
            fast_return_usb = false;
            fast_paused = true;
        }
    } else if(action == 4) {
        ok=ok&&!strcmp(phrase,"SPIEGEL REPARIEREN")&&usb_test_phase==2&&app_owned&&!update_lock&&!mirror_lock;
        command=CMD_FAST_REPAIR;
    } else {
        ok = ok && ! strcmp(phrase, "AUTOMATIK PAUSE");
        if (ok) {
            fast_paused = true;
            fast_handoff = false;
        }
    }
    if (ok && command != CMD_NONE) ok = xQueueSend(command_queue, & command, 0) == pdTRUE;
    xSemaphoreGive(state_lock);
    httpd_resp_set_status(req, ok ? "202 Accepted" : "409 Conflict");
    return httpd_resp_sendstr(req, ok ? "Auftrag angenommen; Status beobachten" : "Gesperrt: Leerlauf/APP, technische Fehler oder ausdrueckliche Bestaetigung pruefen");
}
static esp_err_t fast_summary(httpd_req_t * req) {
    char text [ 750 ];
    snprintf(text, sizeof(text), "<section><h3>Automatische Spiegelung Test35</h3><p>%s; Intervall %u s; Vollaufbau bei mehr als %u abweichenden Dateien. Unvollstaendiger SD-Stand: %s.</p><p>Letzter Lauf: %s; Unterschiede %" PRIu64 "; Dateien %" PRIu64 "/%" PRIu64 "; Daten %" PRIu64 "/%" PRIu64 " Bytes.</p><p>", fast_busy ? "Vorgang aktiv; weitere Timerauftraege warten" : fast_paused ? "pausiert" : "freigegeben", (unsigned)app_interval_seconds, SYNC_REBUILD_THRESHOLD, fast_lock ? "USB gesperrt" : "nein", fast_result . rebuild ? "Vollaufbau" : "Delta/Generationspruefung", fast_result . differences, mirror_completed, mirror_plan . files, mirror_bytes, mirror_plan . download);
    esp_err_t e = httpd_resp_sendstr_chunk(req, text);
    if (e == ESP_OK) e = comparison_text(req, fast_reason);
    if(e==ESP_OK){snprintf(text,sizeof(text),"; letzte Laufzeit %" PRIu64 " ms",fast_elapsed_ms);e=httpd_resp_sendstr_chunk(req,text);}
    if(e==ESP_OK&&!fast_busy&&fast_result.error&&!fast_result.success){e=httpd_resp_sendstr_chunk(req,"; Fehler: ");if(e==ESP_OK)e=comparison_text(req,fast_result.error);}
    if (e == ESP_OK && * mirror_failure_path) {
        e = httpd_resp_sendstr_chunk(req, "</p><p>Dateioperation fehlgeschlagen: ");
        if (e == ESP_OK) e = comparison_text(req, mirror_failure_path);
        if (e == ESP_OK) e = httpd_resp_sendstr_chunk(req, "; Ursache: ");
        if (e == ESP_OK) e = comparison_text(req, mirror_failure_reason);
        if (e == ESP_OK) {
            snprintf(text, sizeof(text), "; Datei-HTTP %d", mirror_failure_http);
            e = httpd_resp_sendstr_chunk(req, text);
        }
    }
    return e == ESP_OK ? httpd_resp_sendstr_chunk(req, "</p></section>") : e;
}


static void fast_repair_worker(void)
{
 remote_repair=true;repair_verified=0;fast_return_usb=false;
 event_log("Reparatur: vollstaendiger NAS-Plan, SD-Inhalte per SHA256 pruefen; keine Vorab-Loeschung");
 fast_worker();
 char note[160];snprintf(note,sizeof(note),"Reparatur: %" PRIu64 " vorhandene Dateien per SHA256 bestaetigt",repair_verified);event_log(note);
 fast_release(repair_inventory.keys);memset(&repair_inventory,0,sizeof(repair_inventory));remote_repair=false;
}
