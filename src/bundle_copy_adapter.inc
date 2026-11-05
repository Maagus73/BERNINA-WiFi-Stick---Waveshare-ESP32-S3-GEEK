#include "bulk_copy.h"
/* A queue contains only F records from the already validated immutable plan. */
static struct {
    bulk_entry_t files[SYNC_BULK_FILES];
    unsigned count;
    uint64_t bytes;
    char plan[65];
} bundle;

static void *bundle_rx_allocate(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void *bundle_dma_allocate(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
}

typedef struct {
    esp_http_client_handle_t client;
    mbedtls_sha256_context sha;
    uint64_t base_files, base_bytes, wire_bytes, read_calls, expected_wire, last_log_us;
    int last_read, socket_error;
    const char *transfer_id;
} bundle_transport_t;

static bool bundle_hash_start(void *v) { return mbedtls_sha256_starts(&((bundle_transport_t *)v)->sha, 0) == 0; }
static bool bundle_hash_update(void *v, const unsigned char *p, size_t n) { return mbedtls_sha256_update(&((bundle_transport_t *)v)->sha, p, n) == 0; }
static bool bundle_hash_finish(void *v, unsigned char *out) { return mbedtls_sha256_finish(&((bundle_transport_t *)v)->sha, out) == 0; }
static int bundle_read(void *v, unsigned char *p, size_t n)
{
    bundle_transport_t *t=v;uint64_t start=(uint64_t)esp_timer_get_time();
    int got=sync_http_read(t->client,(char *)p,n,"NAS-Paket lesen");
    uint64_t now=(uint64_t)esp_timer_get_time();
    t->last_read=got;t->read_calls++;
    if(got>0)t->wire_bytes+=(unsigned)got;
    else t->socket_error=esp_http_client_get_errno(t->client);
    if(got<=0||now-start>=5000000||now-t->last_log_us>=10000000){
        char line[224];snprintf(line,sizeof(line),"Empfang %s: %" PRIu64 "/%" PRIu64 " Bytes; read %d; Socket %d; Aufruf %" PRIu64 " ms",*t->transfer_id?t->transfer_id:"ohne-ID",t->wire_bytes,t->expected_wire,got,t->socket_error,(now-start)/1000);
        event_log(line);t->last_log_us=now;
    }
    return got;
}
static bool bundle_complete(void *v) { return esp_http_client_is_complete_data_received(((bundle_transport_t *)v)->client); }
static uint64_t bundle_clock(void *v) { (void)v; return (uint64_t)esp_timer_get_time(); }

static void bundle_progress(void *v, uint64_t bytes, unsigned files, bool committed)
{
    bundle_transport_t *t = v;
    if (committed) {
        fast_result.completed = t->base_files + files;
        fast_result.received = t->base_bytes + bytes;
        fast_progress(NULL, fast_result.completed, fast_result.files, fast_result.received, fast_result.bytes);
    } else {
        xSemaphoreTake(state_lock, portMAX_DELAY);
        mirror_bytes = t->base_bytes + bytes;
        xSemaphoreGive(state_lock);
        display_ui_transfer(true, t->base_bytes + bytes, fast_result.bytes, false);
    }
}

static void bundle_reset(void)
{
    for (unsigned i = 0; i < bundle.count; i++) fast_release((void *)bundle.files[i].path);
    bundle.count = 0;
    bundle.bytes = 0;
}

static bool bundle_flush(void)
{
    if (!bundle.count) return true;
    const uint64_t base_files = fast_result.completed, base_bytes = fast_result.received;
    bool ok = false;
    int last_status = 0;
    unsigned failed_index = 0;
    fast_http_close();
    for (unsigned attempt = 0; attempt < SYNC_NETWORK_ATTEMPTS && !ok; attempt++) {
        if (attempt) {
            event_log("Sammeltransfer: Wartezeit vor erneutem Verbindungsaufbau");
            for (unsigned wait = 0; wait < attempt * 20 && app_access_allowed(); wait++)
                vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (!app_access_allowed()) { fast_result.error = "Sammeltransfer: APP-Besitz verloren"; break; }
        fast_result.completed = base_files;
        fast_result.received = base_bytes;
        fast_progress(NULL, base_files, fast_result.files, base_bytes, fast_result.bytes);
        char resource[160], note[200];
        snprintf(resource, sizeof(resource), ".bernina-bundle-v1/%s/%" PRIu64 "/%u", bundle.plan, base_files, bundle.count);
        snprintf(note, sizeof(note), "Sammeltransfer: Datei %" PRIu64 ", %u Dateien, %" PRIu64 " Bytes; Versuch %u", base_files + 1, bundle.count, bundle.bytes, attempt + 1);
        event_log(note);
        remote_headers_t headers = {0};
        bundle_transport_t transport = {.base_files = base_files, .base_bytes = base_bytes};
        uint64_t started = (uint64_t)esp_timer_get_time();
        last_status = 0;
        transport.client = remote_open(resource, &headers, false, false);
        if (!transport.client) {
            fast_result.error = "Sammeltransfer: HTTPS-Verbindungsaufbau fehlgeschlagen";
            event_log(fast_result.error);
            continue;
        }
        int64_t length = sync_http_headers(transport.client,"NAS-Paket Antwort");
        int status = esp_http_client_get_status_code(transport.client);
        nas_last_http = status;
        last_status = status;
        if (status != 200 || length <= 0 || !remote_meta(&headers) || strcmp(headers.id, bundle.plan)) {
            snprintf(note, sizeof(note), "Sammeltransfer-Kopf: HTTP %d; Laenge %" PRId64 "; Metadaten %s; Plan %s; Server %s", status, length, remote_meta(&headers) ? "OK" : "ungueltig", !strcmp(headers.id, bundle.plan) ? "OK" : "ungueltig", *headers.server_error ? headers.server_error : "kein Fehlercode");
            event_log(note);
            esp_http_client_cleanup(transport.client);
            fast_result.error = "Sammeltransfer: HTTPS-Kopf, Plan oder NAS-Vorbedingung ungueltig";
            if (status > 0 && status < 500 && status != 408 && status != 429) break;
            continue;
        }
        transport.expected_wire=(uint64_t)length;transport.transfer_id=headers.transfer_id;transport.last_log_us=started;
        snprintf(note,sizeof(note),"Paket-ID %s: erwartet %" PRId64 " Bytes inkl. Rahmen",*headers.transfer_id?headers.transfer_id:"Server ohne Transfer-ID",length);event_log(note);
        mbedtls_sha256_init(&transport.sha);
        bulk_ops_t ops = {
            .context = &transport, .owned = fast_owned, .read = bundle_read, .complete = bundle_complete,
            .hash_start = bundle_hash_start, .hash_update = bundle_hash_update, .hash_finish = bundle_hash_finish,
            .progress = bundle_progress, .cooperate = display_ui_cooperate, .fault = fast_io_fault,
            .clock_us = bundle_clock, .allocate = fast_allocate, .allocate_read = bundle_rx_allocate, .allocate_write = bundle_dma_allocate, .release = fast_release
        };
        bulk_result_t result;
        bulk_receive(SD_PATH, FAST_TEMP, bundle.plan, base_files, bundle.files, bundle.count, &ops, &result);
        mbedtls_sha256_free(&transport.sha);
        bool complete=esp_http_client_is_complete_data_received(transport.client);
        int socket_error=transport.socket_error?transport.socket_error:esp_http_client_get_errno(transport.client);
        esp_http_client_cleanup(transport.client);
        snprintf(note,sizeof(note),"Empfang-Ende %s: %" PRIu64 "/%" PRIu64 " Bytes; read %d; Socket %d; HTTP komplett %u",*headers.transfer_id?headers.transfer_id:"ohne-ID",transport.wire_bytes,transport.expected_wire,transport.last_read,socket_error,(unsigned)complete);event_log(note);
        snprintf(note,sizeof(note),"TLS-Diagnose: ESP %d; Bibliothek %d; Flags %d; read-Aufrufe %" PRIu64,headers.tls_error,headers.tls_code,headers.tls_flags,transport.read_calls);event_log(note);
        snprintf(note, sizeof(note), "Sammeltransfer %u/%u: gesamt %" PRIu64 " ms; Netz %" PRIu64 "; SD auf/schreib/ab %" PRIu64 "/%" PRIu64 "/%" PRIu64 " ms", result.completed, bundle.count, ((uint64_t)esp_timer_get_time() - started) / 1000, result.read_us / 1000, result.open_us / 1000, result.write_us / 1000, result.finish_us / 1000);
        event_log(note);
        snprintf(note, sizeof(note), "Bloecke: Netz %" PRIu64 " Aufrufe, Mittel %" PRIu64 " Bytes; SD %" PRIu64 " Aufrufe, Mittel %" PRIu64 " Bytes", result.read_calls, result.read_calls ? result.read_bytes / result.read_calls : 0, result.write_calls, result.write_calls ? result.write_bytes / result.write_calls : 0);
        event_log(note);
        app_events_flush();
        ok = result.success;
        if (!ok) {
            fast_result.error = result.error;
            if(result.error&&strstr(result.error,"SHA256"))sync_diag_set(SYNC_FAIL_PERMANENT,"SHA256-Pruefung fehlgeschlagen");
            failed_index = result.completed < bundle.count ? result.completed : bundle.count - 1;
            if (io_fault || storage_fault) break;
        }
    }
    if (ok) fast_result.error = NULL;
    else {
        dav_tree_entry_t file = {.path = (char *)bundle.files[failed_index].path};
        if (!fast_result.error) fast_result.error = "Sammeltransfer fehlgeschlagen";
        mirror_file_failure(&file, fast_result.error, last_status);
        event_log(fast_result.error);
    }
    bundle_reset();
    return ok;
}

static bool bundle_queue(const char *path, const dav_entry_t *entry)
{
    if (bundle.count && (bundle.count == SYNC_BULK_FILES || entry->size > SYNC_BULK_BYTES - (bundle.bytes < SYNC_BULK_BYTES ? bundle.bytes : SYNC_BULK_BYTES))) {
        if (!bundle_flush()) return false;
    }
    char *copy = fast_allocate(strlen(path) + 1);
    if (!copy) return false;
    strcpy(copy, path);
    bulk_entry_t *file = &bundle.files[bundle.count];
    file->path = copy;
    file->size = entry->size;
    if (strlen(entry->etag) != 66) { fast_release(copy); return false; }
    for (unsigned i = 0; i < 32; i++) {
        char digits[3] = {entry->etag[1 + i*2], entry->etag[2 + i*2], 0};
        char *end;
        unsigned long n = strtoul(digits, &end, 16);
        if (*end) { fast_release(copy); return false; }
        file->hash[i] = (unsigned char)n;
    }
    bundle.count++;
    bundle.bytes += entry->size;
    return true;
}
