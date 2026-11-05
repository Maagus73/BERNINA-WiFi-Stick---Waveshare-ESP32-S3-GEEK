// BERNINA WiFi Stick - Test 21: NAS Index + manuelle Spiegelung
// TinyUSB MSC read-only + WLAN HTTP-Diagnose + manuelle USB-Neuanmeldung.
// Manueller HTTPS-Testdownload. SD-Schreiben nur bei bestaetigtem APP-Besitz.
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include "esp_http_client.h"
#include "esp_sntp.h"
#include "mbedtls/sha256.h"
#include "mbedtls/platform_util.h"
#include "synology_cert.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "tusb.h"
#include "esp_http_server.h"
#include "secrets.h"
#include "dav_listing.h"
#include "dav_tree.h"
#include "dav_store.h"
#include "sd_index.h"
#include "dav_memory.h"
#include "dry_run.h"
#include "path_search.h"
#include "single_copy.h"
#include "safe_update.h"
#include "mirror_sync.h"
#include "nas_index.h"
#include "nas_index_config.h"
#include "mbedtls/base64.h"
#include "batch_sync.h"
#include "display_ui.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "esp_system.h"
#include "esp_timer.h"

#ifndef WEB_CONTROL_PASSWORD
#error "Test 17a: WEB_CONTROL_PASSWORD muss in include/secrets.h als Zeichenkette definiert sein."
#else
_Static_assert(sizeof(WEB_CONTROL_PASSWORD) > 1, "WEB_CONTROL_PASSWORD darf nicht leer sein.");
_Static_assert(sizeof(WEB_CONTROL_PASSWORD) <= 129, "WEB_CONTROL_PASSWORD darf maximal 128 UTF-8-Bytes enthalten.");
#endif

#define DEVICE_HOSTNAME "bernina-wifi-stick"
#define WIFI_TIMEOUT_MS 60000
#define WIFI_MAX_RETRIES 10
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define SD_SCK GPIO_NUM_36
#define SD_MOSI GPIO_NUM_35
#define SD_MISO GPIO_NUM_37
#define SD_CS GPIO_NUM_34
#define USB_OFF_MS 20000
#define SD_PATH "/sdcard"
#define MAX_LOG 128

static const char *TAG = "BERNINA";
static EventGroupHandle_t wifi_events;
static esp_netif_t *wifi_netif;
static volatile bool wifi_associated = false;
static volatile bool wifi_has_ip = false;
static int wifi_retries = 0;
static sdmmc_card_t sd_card;
static tinyusb_msc_storage_handle_t storage_handle;
static volatile uint32_t usb_test_phase = 0; // 0 USB, 1 Umschaltung, 2 APP, 3 Fehler
static volatile uint32_t usb_test_count = 0;
static volatile uint32_t usb_disconnect_ms = 0;
static volatile uint32_t usb_reconnect_ms = 0;
static volatile bool usb_seen_mounted = false;
static volatile bool usb_seen_unmounted = false;
static volatile int last_error = 0;
static volatile int last_file_number = 0;
static char last_file[40] = "-";
static char last_action[96] = "Bereit";
static volatile bool app_owned = false;
// USB-Zustandsdiagnose; keine MSC-Callback-Ueberschreibung.
static volatile bool diag_connected = false;
static volatile bool diag_mounted = false;
static volatile bool diag_suspended = false;
static volatile uint32_t diag_changes = 0;
static volatile uint32_t diag_last_change_ms = 0;
static volatile uint32_t diag_mounted_false_ms = 0;
static volatile uint32_t diag_mounted_true_ms = 0;
static volatile uint32_t diag_suspended_true_ms = 0;
static volatile uint32_t diag_connected_false_ms = 0;
static volatile uint32_t diag_app_ms = 0;
static volatile uint32_t diag_usb_ms = 0;
static volatile int diag_storage_point = -1;

static void event_log(const char *message);
// Callback: nur kurze, geschuetzte Zustandsuebertragung, keine I/O/Logs.
typedef struct {
    uint32_t sequence, starts, completes, failures;
    int last_event, last_point, complete_point;
} msc_state_t;
static portMUX_TYPE msc_lock = portMUX_INITIALIZER_UNLOCKED;
static msc_state_t msc_state = { .last_event = -1, .last_point = -1, .complete_point = -1 };
static bool storage_fault = false;
static bool io_fault = false;
static bool usb_detached_by_app = false;
static tinyusb_msc_mount_point_t confirmed_owner = TINYUSB_MSC_STORAGE_MOUNT_USB;
static uint32_t confirmed_sequence = 0;

static msc_state_t msc_snapshot(void)
{
    portENTER_CRITICAL(&msc_lock);
    msc_state_t result = msc_state;
    portEXIT_CRITICAL(&msc_lock);
    return result;
}
static void msc_storage_event_cb(tinyusb_msc_storage_handle_t handle,
                                 tinyusb_msc_event_t *event, void *arg)
{
    (void)arg;
    if (!event) return;
    portENTER_CRITICAL(&msc_lock);
    msc_state.sequence++;
    msc_state.last_event = event->id;
    msc_state.last_point = event->mount_point;
    if (handle != storage_handle) {
        msc_state.failures++;
    } else switch (event->id) {
        case TINYUSB_MSC_EVENT_MOUNT_START: msc_state.starts++; break;
        case TINYUSB_MSC_EVENT_MOUNT_COMPLETE:
            msc_state.completes++;
            msc_state.complete_point = event->mount_point;
            break;
        default: msc_state.failures++; break;
    }
    portEXIT_CRITICAL(&msc_lock);
}
static void msc_log_state(const char *label)
{
    msc_state_t m = msc_snapshot();
    char message[128];
    snprintf(message, sizeof(message),
             "MSC %s: event=%d point=%d starts=%" PRIu32 " complete=%" PRIu32 " errors=%" PRIu32,
             label, m.last_event, m.last_point, m.starts, m.completes, m.failures);
    event_log(message);
}
#include "app_runtime.h"
static char logs[MAX_LOG][192];
static unsigned log_count = 0;
static SemaphoreHandle_t state_lock;
static QueueHandle_t command_queue;
typedef enum { CMD_NONE, CMD_DISCONNECT, CMD_CREATE, CMD_CONNECT, CMD_RECOVER, CMD_NAS_CHECK, CMD_NAS_DOWNLOAD, CMD_COMPARE, CMD_SINGLE, CMD_BATCH, CMD_UPDATE_PREPARE, CMD_UPDATE_RUN, CMD_UPDATE_RECOVER, CMD_MIRROR_PLAN, CMD_MIRROR_RUN, CMD_FAST_RUN, CMD_FAST_RESET, CMD_FAST_REPAIR, CMD_REPAIR_FLOW, CMD_APP_REBOOT, CMD_AUTO_RETRY } command_t;
// Caller already holds state_lock; do not wait on the same non-recursive mutex.
static void event_log_held(const char *message)
{
 unsigned sequence=log_count++,pos=sequence%MAX_LOG;
 time_t now=time(NULL);app_event_time[pos]=now>=1735689600?now:0;
 struct tm local;char stamp[16]="--:--:--";
 if(app_event_time[pos]){localtime_r(&now,&local);strftime(stamp,sizeof(stamp),"%H:%M:%S",&local);}
 snprintf(logs[pos],sizeof(logs[pos]),"%s: %s",stamp,message);app_utf8_tail(logs[pos]);
 if(sequence>=MAX_LOG&&app_event_sent<=sequence-MAX_LOG){app_event_sent=sequence-MAX_LOG+1;app_event_dropped++;}
 snprintf(last_action,sizeof(last_action),"%s",message);
 ESP_LOGI(TAG,"%s",message);
}
static void event_log(const char *message) {
    if (state_lock && xSemaphoreTake(state_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        event_log_held(message);
        xSemaphoreGive(state_lock);
        return;
    }
    ESP_LOGI(TAG, "%s", message);
}
// Schreibschutz wie im erfolgreich getesteten Test 04.
bool tud_msc_is_writable_cb(uint8_t lun) { (void)lun; return false; }

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {

        if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "WLAN gestartet");
            esp_wifi_connect();
        }

        else if (event_id == WIFI_EVENT_STA_CONNECTED) {
            wifi_associated = true;
            wifi_retries = 0;

            ESP_LOGI(TAG, "Access Point verbunden");
        }

        else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *event =
                (wifi_event_sta_disconnected_t *)event_data;

            wifi_associated = false;
            wifi_has_ip = false;

            xEventGroupClearBits(
                wifi_events, WIFI_CONNECTED_BIT);

            ESP_LOGW(TAG, "WLAN getrennt: Grund %u",
                     (unsigned)event->reason);

            if (wifi_retries < WIFI_MAX_RETRIES) {
                wifi_retries++;
                esp_wifi_connect();
            } else {
                xEventGroupSetBits(
                    wifi_events, WIFI_FAILED_BIT);
            }
        }
    }

    else if (event_base == IP_EVENT) {

        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event =
                (ip_event_got_ip_t *)event_data;

            wifi_has_ip = true;
            wifi_retries = 0;

            ESP_LOGI(TAG, "IP-Adresse: " IPSTR,
                     IP2STR(&event->ip_info.ip));

            xEventGroupClearBits(
                wifi_events, WIFI_FAILED_BIT);

            xEventGroupSetBits(
                wifi_events, WIFI_CONNECTED_BIT);
        }

        else if (event_id == IP_EVENT_STA_LOST_IP) {
            wifi_has_ip = false;

            xEventGroupClearBits(
                wifi_events, WIFI_CONNECTED_BIT);
        }
    }
}

// ============================================================
// WLAN initialisieren
// ============================================================

static bool wifi_connect(void)
{
    wifi_events = xEventGroupCreate();

    if (wifi_events == NULL) {
        return false;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_netif = esp_netif_create_default_wifi_sta();

    if (wifi_netif == NULL) {
        return false;
    }

    ESP_ERROR_CHECK(esp_netif_set_hostname(
        wifi_netif, DEVICE_HOSTNAME));

    wifi_init_config_t init_cfg =
        WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        wifi_event_handler,
        NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        wifi_event_handler,
        NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_LOST_IP,
        wifi_event_handler,
        NULL));

    wifi_config_t wifi_cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    if (strlen(WIFI_SSID) >= sizeof(wifi_cfg.sta.ssid) ||
        strlen(WIFI_PASSWORD) >= sizeof(wifi_cfg.sta.password)) {
        ESP_LOGE(TAG, "WLAN-Zugangsdaten zu lang");
        return false;
    }

    strcpy((char *)wifi_cfg.sta.ssid, WIFI_SSID);
    strcpy((char *)wifi_cfg.sta.password, WIFI_PASSWORD);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(
        WIFI_IF_STA, &wifi_cfg));

    uint8_t mac[6];

    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        ESP_LOGI(TAG,
                 "WLAN-MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2],
                 mac[3], mac[4], mac[5]);
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(
        wifi_events,
        WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(WIFI_TIMEOUT_MS));

    return (bits & WIFI_CONNECTED_BIT) != 0;
}


// Nur Statusabfragen: kein Eingriff in den USB-/MSC-Datenpfad.
static void usb_diag_sample(void)
{
    bool connected = tud_connected();
    bool mounted = tud_mounted();
    bool suspended = tud_suspended();
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (connected != diag_connected || mounted != diag_mounted || suspended != diag_suspended) {
        if (diag_connected && !connected) diag_connected_false_ms = now;
        if (diag_mounted && !mounted) diag_mounted_false_ms = now;
        if (!diag_mounted && mounted) diag_mounted_true_ms = now;
        if (!diag_suspended && suspended) diag_suspended_true_ms = now;
        diag_connected = connected;
        diag_mounted = mounted;
        diag_suspended = suspended;
        diag_last_change_ms = now;
        diag_changes++;
    }
    if (mounted) usb_seen_mounted = true;
    if (usb_seen_mounted && !mounted) usb_seen_unmounted = true;
}
static void usb_diag_storage(void)
{
    tinyusb_msc_mount_point_t point = TINYUSB_MSC_STORAGE_MOUNT_USB;
    if (tinyusb_msc_get_storage_mount_point(storage_handle, &point) == ESP_OK)
        diag_storage_point = point == TINYUSB_MSC_STORAGE_MOUNT_APP ? 1 : 0;
    else diag_storage_point = -1;
}
static void usb_diag_log_snapshot(const char *label)
{
    char message[128];
    usb_diag_sample();
    usb_diag_storage();
    snprintf(message, sizeof(message), "DIAG %s: C=%u M=%u S=%u SD=%d A=%u",
             label, (unsigned)diag_connected, (unsigned)diag_mounted,
             (unsigned)diag_suspended, diag_storage_point, (unsigned)diag_changes);
    event_log(message);
}

// Browser-Diagnose: Nur GET / ist lesend. Steuerbefehle ausschliesslich per POST.
static volatile uint32_t comparison_revision;
static esp_err_t comparison_render(httpd_req_t *req);
static esp_err_t home_get_handler(httpd_req_t *req);
static esp_err_t protected_diagnostic_handler(httpd_req_t *req);
static esp_err_t home_summary(httpd_req_t *req);
static volatile command_t active_command;
static bool fast_lock,fast_busy,fast_paused,fast_started,fast_copying;
static char fast_reason[224]="Automatik wartet auf WLAN und bestaetigte Speicheruebergabe";
static void fast_boot(void);
static void fast_schedule(void);
static void fast_worker(void);
static void fast_reset_worker(void);
static void fast_repair_worker(void);
static esp_err_t fast_handler(httpd_req_t *,unsigned);
static esp_err_t fast_summary(httpd_req_t *);
static void fast_scan(void *,const char *,uint64_t,uint64_t);

static volatile int nas_last_http=-1;
static volatile bool comparison_stale;
static time_t comparison_completed_at;
static int64_t comparison_completed_us;
static volatile bool display_boot_usb,display_system_initialized,display_compare_failed;
static void display_snapshot(display_input_t *out);
static esp_err_t matrix_setting_handler(httpd_req_t *req);
static bool batch_pending,batch_cancelled;
static batch_plan_t batch_plan;
static batch_state_t batch_state;
static uint32_t batch_revision;
static int64_t batch_preview_us;
static char batch_ticket[17];
static unsigned batch_current,batch_succeeded,batch_failed;
static uint64_t batch_received,batch_finished_bytes;
static char batch_reason[160]="Noch kein Testlauf";
static unsigned char batch_guards[BATCH_DIRS][32];
static bool comparison_running;
static bool update_pending,update_lock,update_record_loaded,update_valid,update_usb_ready,update_finished;
static unsigned update_succeeded,update_failed;
static uint64_t update_received;
static bool update_verifying;
static void update_display_reset(void);
static update_record_t update_record;
static uint32_t update_revision;
static int64_t update_selected_us;
static char update_ticket[17],update_reason[224]="Noch kein Test-19-Auftrag";
static char update_http_modified[96],update_sd_modified[96];
static bool update_http_digest;
static esp_err_t update_select_handler(httpd_req_t *req);
static esp_err_t update_start_handler(httpd_req_t *req);
static esp_err_t update_status_handler(httpd_req_t *req);
static esp_err_t update_recovery_handler(httpd_req_t *req);
static void update_worker(command_t cmd);
static bool mirror_pending,mirror_lock,mirror_journal_valid;
static mirror_state_t mirror_state;
static void update_display_reset(void){update_finished=false;if(!mirror_lock&&!mirror_pending)mirror_state=MIRROR_IDLE;}
static mirror_plan_t mirror_plan;
static mirror_result_t mirror_result;
static uint64_t mirror_completed,mirror_bytes,mirror_base_bytes;
static uint32_t mirror_revision;
static int64_t mirror_selected_us;
static char mirror_ticket[17],mirror_reason[224]="Noch kein vollstaendiger Sync-Plan";
static void mirror_worker(command_t cmd);
static void mirror_boot_journal(void);
static esp_err_t mirror_handler(httpd_req_t *req,unsigned action);
static esp_err_t mirror_summary(httpd_req_t *req);
static bool mirror_fresh(void);
static bool update_controls_blocked(void){return update_pending||update_lock||mirror_pending||mirror_lock||fast_lock||fast_busy;}
static bool update_usb_connect(void);
static void update_boot_journal(void);






static bool sync_sd_ignored(const char *,bool);
static esp_err_t sync_adopt_source(httpd_req_t *);
#include "sync_page.inc"
static esp_err_t diagnostic_get_handler(httpd_req_t *req)
{
 if(req->user_ctx){esp_err_t result=home_summary(req);return result==ESP_OK?httpd_resp_send_chunk(req,NULL,0):result;}
 return sync_page(req,true);
}
// Passwort nur im POST-Body, niemals in URLs, HTML oder Diagnosemeldungen.
// Eigener Header verhindert einfache Cross-Origin-Formularbefehle; kein CORS.
static bool control_authenticated(httpd_req_t *req)
{
    char marker[2] = {0};
    unsigned char supplied[128] = {0};
    const unsigned char *expected = (const unsigned char *)WEB_CONTROL_PASSWORD;
    const size_t expected_len = sizeof(WEB_CONTROL_PASSWORD) - 1;
    if (httpd_req_get_hdr_value_len(req, "X-Bernina-Control") != 1 ||
        httpd_req_get_hdr_value_str(req, "X-Bernina-Control", marker, sizeof(marker)) != ESP_OK ||
        marker[0] != '1' || req->content_len == 0 || req->content_len > sizeof(supplied)) return false;
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, (char *)supplied + received, req->content_len - received);
        if (n <= 0) {
            mbedtls_platform_zeroize(supplied, sizeof(supplied));
            return false;
        }
        received += (size_t)n;
    }
    unsigned difference = received != expected_len;
    for (size_t i = 0; i < expected_len; i++) difference |= supplied[i] ^ expected[i];
    mbedtls_platform_zeroize(supplied, sizeof(supplied));
    return difference == 0;
}
static esp_err_t command_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (!control_authenticated(req)) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_sendstr(req, "Steuerpasswort fehlt oder ist ungueltig");
        return ESP_FAIL;
    }
    command_t cmd = (command_t)(uintptr_t)req->user_ctx;
    if (cmd == CMD_CREATE || cmd == CMD_NAS_DOWNLOAD || cmd == CMD_NAS_CHECK) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "Historischer Schreibtest deaktiviert; aktuellen Testfall auf der Testseite waehlen");
    }
    xSemaphoreTake(state_lock,portMAX_DELAY);
    bool sync_busy=update_controls_blocked()||active_command==CMD_BATCH;
    // Persistent journals block USB and payload writes, not an idle handoff to APP.
    // checked_mount and app_access_allowed still verify the real MSC ownership.
    if(cmd==CMD_DISCONNECT){
        sync_busy=storage_fault||io_fault||active_command!=CMD_NONE||
            update_pending||mirror_pending||comparison_running||fast_busy||
            usb_test_phase!=0||!command_queue||uxQueueMessagesWaiting(command_queue);
    }
    bool queued=!sync_busy&&cmd!=CMD_NONE&&command_queue&&xQueueSend(command_queue,&cmd,0)==pdTRUE;
    xSemaphoreGive(state_lock);
    if (!queued) {
        if(cmd==CMD_DISCONNECT)event_log("APP-Uebernahme verweigert: Zustand, laufender Vorgang oder technischer Fehler; Status pruefen");
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "Steuerung beschaeftigt");
        return ESP_FAIL;
    }
    event_log(cmd == CMD_CONNECT ? "HTTP: USB-Rueckgabe angenommen" : "HTTP: Steuerbefehl angenommen");
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "Aktion angefordert; wartet ggf. auf laufenden Vergleich");
}
static esp_err_t start_diagnostic_server(void)
{
 httpd_config_t config=HTTPD_DEFAULT_CONFIG();config.server_port=80;config.stack_size=12288;config.max_uri_handlers=16;config.max_resp_headers=8;
 httpd_handle_t server=NULL;esp_err_t e=httpd_start(&server,&config);if(e!=ESP_OK)return e;
 httpd_uri_t home={.uri="/",.method=HTTP_GET,.handler=home_get_handler};if((e=httpd_register_uri_handler(server,&home))!=ESP_OK)return e;
 httpd_uri_t alias={.uri="/diagnostics",.method=HTTP_GET,.handler=diagnostic_get_handler};if((e=httpd_register_uri_handler(server,&alias))!=ESP_OK)return e;
 const char *get[]={"/api/status","/api/events"};for(unsigned i=0;i<2;i++){httpd_uri_t r={.uri=get[i],.method=HTTP_GET,.handler=app_api};if((e=httpd_register_uri_handler(server,&r))!=ESP_OK)return e;}
 const char *post[]={"/api/action","/api/config"};for(unsigned i=0;i<2;i++){httpd_uri_t r={.uri=post[i],.method=HTTP_POST,.handler=app_api};if((e=httpd_register_uri_handler(server,&r))!=ESP_OK)return e;}
 event_log("HTTP-Steuerung bereit");return ESP_OK;
}

static void wifi_task(void *arg)
{
    (void)arg;
    if (wifi_connect()) {
        esp_netif_ip_info_t ip = {0};
        esp_netif_get_ip_info(wifi_netif, &ip);
        ESP_LOGI(TAG, "Diagnose im Browser: http://" IPSTR "/", IP2STR(&ip.ip));
        start_diagnostic_server();
    } else {
        ESP_LOGE(TAG, "WLAN nicht verbunden; USB-Test laeuft trotzdem");
    }
    vTaskDelete(NULL);
}

// 2.4.0 verschluckt interne Fehler: nur ein neuer, passender COMPLETE
// nach genau einem START bestaetigt einen echten Wechsel. Kein No-op-Recovery.
static bool checked_mount(tinyusb_msc_mount_point_t target, const char *name)
{
    app_owned = false;
    msc_state_t before = msc_snapshot();
    tinyusb_msc_mount_point_t actual;
    if (storage_fault || io_fault || !usb_detached_by_app ||
        before.sequence != confirmed_sequence ||
        tinyusb_msc_get_storage_mount_point(storage_handle, &actual) != ESP_OK ||
        actual != confirmed_owner || actual == target) goto fail;

    esp_err_t err = tinyusb_msc_set_storage_mount_point(storage_handle, target);
    msc_state_t after = msc_snapshot();
    if (err != ESP_OK ||
        tinyusb_msc_get_storage_mount_point(storage_handle, &actual) != ESP_OK || actual != target ||
        after.sequence - before.sequence != 2 || after.starts - before.starts != 1 ||
        after.completes - before.completes != 1 || after.failures != before.failures ||
        after.last_event != TINYUSB_MSC_EVENT_MOUNT_COMPLETE || after.complete_point != (int)target)
        goto fail;
    confirmed_sequence = after.sequence;
    confirmed_owner = target;
    msc_log_state(name);
    return true;
fail:
    storage_fault = true;
    last_error = ESP_FAIL;
    event_log("MSC unklar: Schreiben und USB-Anmeldung gesperrt; Neustart erforderlich");
    msc_log_state(name);
    return false;
}
static bool mount_app(void)
{
    if (!checked_mount(TINYUSB_MSC_STORAGE_MOUNT_APP, "APP")) return false;
    DIR *dir = opendir(SD_PATH);
    if (!dir) {
        storage_fault = true;
        last_error = errno;
        event_log("APP-Dateisystem nicht lesbar; Zugriff gesperrt");
        return false;
    }
    if (closedir(dir) != 0) {
        storage_fault = true;
        last_error = errno;
        return false;
    }
    return true;
}
static bool mount_usb(void)
{
    return checked_mount(TINYUSB_MSC_STORAGE_MOUNT_USB, "USB");
}
static bool app_access_allowed(void)
{
    tinyusb_msc_mount_point_t actual;
    msc_state_t m = msc_snapshot();
    bool ok = !storage_fault && !io_fault && usb_detached_by_app && app_owned &&
        confirmed_owner == TINYUSB_MSC_STORAGE_MOUNT_APP && m.sequence == confirmed_sequence &&
        tinyusb_msc_get_storage_mount_point(storage_handle, &actual) == ESP_OK &&
        actual == TINYUSB_MSC_STORAGE_MOUNT_APP;
    if (!ok) {
        app_owned = false;
        storage_fault = true;
        usb_test_phase = 3;
        event_log("APP-Zugriff verweigert: Besitznachweis ungueltig");
    }
    return ok;
}


// Test 11: NAS-Diagnose ausschliesslich lesend. USB und SD bleiben unberuehrt.
typedef struct {
    size_t received;
    bool overflow;
    mbedtls_sha256_context sha;
} nas_context_t;

static esp_err_t nas_data_handler(esp_http_client_event_t *evt)
{
    nas_context_t *ctx = (nas_context_t *)evt->user_data;
    if (ctx && evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        if (ctx->received + (size_t)evt->data_len > 1024 * 1024) {
            ctx->overflow = true;
            return ESP_FAIL;
        }
        if (mbedtls_sha256_update(&ctx->sha,
            (const unsigned char *)evt->data, (size_t)evt->data_len) != 0) return ESP_FAIL;
        ctx->received += (size_t)evt->data_len;
    }
    return ESP_OK;
}

static bool ensure_network_time(void)
{
    time_t now = 0;
    time(&now);
    if (now >= 1735689600) return true;
    event_log("NAS: NTP-Zeitsynchronisierung gestartet");
    if (!esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
    }
    for (int i = 0; i < 25; ++i) {
        time(&now);
        if (now >= 1735689600) return true;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    event_log("NAS FEHLER: Uhrzeit nicht synchronisiert");
    return false;
}



// Test 17a: rekursive, rein lesende Momentaufnahme. Keine Dateiinhalte.
typedef struct {
 dav_tree_t tree;
 dav_error_t error;
 bool complete,capacity_known;
 dry_plan_t plan;
 uint64_t sd_total,sd_free,elapsed_ms;
 int status;
 size_t xml_offset,psram_total,psram_free,psram_free_after;
 uint32_t seconds;
 char error_path[DAV_PATH_LIMIT];
} comparison_result_t;
static volatile uint32_t comparison_revision;
static comparison_result_t *comparison_result;
static char comparison_note[160]="Noch kein Vergleich ausgefuehrt.";
// Alle fremden Namen/Metadaten werden vor HTML-Ausgabe escaped.
static esp_err_t comparison_text(httpd_req_t *req, const char *s)
{
    char chunk[192]; size_t n = 0;
    while (*s) {
        const char *escaped = NULL;
        switch (*s) {
            case '&': escaped = "&amp;"; break;
            case '<': escaped = "&lt;"; break;
            case '>': escaped = "&gt;"; break;
            case '\"': escaped = "&quot;"; break;
            case '\'': escaped = "&#39;"; break;
        }
        size_t len = escaped ? strlen(escaped) : 1;
        if (n + len > sizeof(chunk)) {
            esp_err_t err = httpd_resp_send_chunk(req, chunk, n); if (err != ESP_OK) return err;
            n = 0;
        }
        if (escaped) memcpy(chunk + n, escaped, len); else chunk[n] = *s;
        n += len; s++;
    }
    return n ? httpd_resp_send_chunk(req, chunk, n) : ESP_OK;
}
static esp_err_t comparison_cell(httpd_req_t *req, const char *text)
{
    esp_err_t err = httpd_resp_sendstr_chunk(req, "<td style='white-space:pre-wrap'>");
    if (err == ESP_OK) err = comparison_text(req, text);
    return err == ESP_OK ? httpd_resp_sendstr_chunk(req, "</td>") : err;
}
static esp_err_t dry_render(httpd_req_t *req,const comparison_result_t *r)
{
 const dry_plan_t *p=&r->plan;char text[640];
 esp_err_t err=httpd_resp_sendstr_chunk(req,"<h4>Synchronisationsvorschau - NAS ist Quelle</h4>"
  "<p><b>VERGLEICHSBERICHT: NUR LESEN.</b> Dieser Vergleich schreibt keine Dateien. Einzeldateitest nur getrennt bestaetigt.</p>");
 if(err!=ESP_OK)return err;
 if(!p->complete) {
  err=httpd_resp_sendstr_chunk(req,"<p><b>UNVOLLSTAENDIG: Zaehler und Volumina sind nur Teilwerte. Keine spaetere Synchronisation freigeben.</b></p>");
  if(err!=ESP_OK)return err;
 }
 if(p->overflow)return httpd_resp_sendstr_chunk(req,"<p>Rechenueberlauf: Summen ungueltig, Vorschau UNVOLLSTAENDIG.</p>");
 err=httpd_resp_sendstr_chunk(req,"<table border='1' cellpadding='5'><tr><th>Kategorie</th><th>Dateien</th><th>Ordner</th><th>Sonstige</th><th>Eintraege</th></tr>");
 for(unsigned c=0;c<DRY_CATEGORY_COUNT&&err==ESP_OK;c++) {
  const dry_counts_t *n=&p->categories[c];
  snprintf(text,sizeof(text),"<tr><td>%s</td><td>%" PRIu64 "</td><td>%" PRIu64 "</td><td>%" PRIu64 "</td><td>%" PRIu64 "</td></tr>",
    dry_category_name((dry_category_t)c),n->files,n->directories,n->other,n->entries);
  err=httpd_resp_sendstr_chunk(req,text);
 }
 if(err!=ESP_OK)return err;
 snprintf(text,sizeof(text),"</table><p>Vorhandene passende Ordner ohne Aktion: %" PRIu64 ". "
  "Mehrdeutige Namenseintraege: %" PRIu64 "; nicht darstellbare Pfade: %" PRIu64 ". "
  "Datei-/Ordner-Konflikte zaehlen in beiden Typspalten, als ein zugeordnetes Konfliktpaar. "
  "Mehrdeutige Namen werden je Seite separat gezaehlt.</p>",p->existing_directories,p->ambiguous_entries,p->invalid_paths);
 err=httpd_resp_sendstr_chunk(req,text);if(err!=ESP_OK)return err;
 snprintf(text,sizeof(text),"<p>Download fuer eindeutig neue Dateien: %" PRIu64 " Bytes. "
  "Zusaetzlich geaendert/verdaechtig: %" PRIu64 ", ungeklaert: %" PRIu64 ", zusammen moeglich: %" PRIu64 " Bytes. "
  "Dateien mit unbekannter Groesse/Belegung: %" PRIu64 ". %s</p>",p->new_bytes,p->changed_bytes,p->unknown_bytes,p->possible_bytes,p->unknown_sizes,
  p->volumes_known?"Volumensummen des vollstaendigen Baums.":"Bekannte Teilvolumina; Gesamtsumme NICHT verlaesslich berechenbar.");
 err=httpd_resp_sendstr_chunk(req,text);if(err!=ESP_OK)return err;
 snprintf(text,sizeof(text),"<p>Zusatzbedarf an Nutzdaten ohne Gutschrift fuer kleinere/SD-only Dateien: %" PRIu64 " Bytes. "
  "Konservative Nutzdatenreserve, falls alle neuen/geaenderten/ungeklaerten Dateien vollstaendig temporaer gehalten wuerden: %" PRIu64 " Bytes. "
  "Cluster-Rundung, Verzeichniseintraege und Metadaten sind NICHT enthalten. "
  "Schreibstrategie und exakter Spitzenbedarf stehen noch nicht fest; dies ist keine Platzfreigabe.</p>",p->growth_bytes,p->temporary_payload_bytes);
 err=httpd_resp_sendstr_chunk(req,text);if(err!=ESP_OK)return err;
 if(r->capacity_known) {
  snprintf(text,sizeof(text),"<p>microSD frei: %" PRIu64 " Bytes, Dateisystem gesamt: %" PRIu64 " Bytes. %s</p>",r->sd_free,r->sd_total,
   p->volumes_known&&p->temporary_payload_bytes>r->sd_free?"Die beschriebene temporaere Nutzdatenreserve uebersteigt den freien Platz.":
   "Aus dem Nutzdatenvergleich allein wird keine ausreichende Kapazitaet zugesichert.");
  err=httpd_resp_sendstr_chunk(req,text);
 } else err=httpd_resp_sendstr_chunk(req,"<p>microSD-Speicherplatz NICHT verlaesslich ermittelt; Vorschau unvollstaendig.</p>");
 if(err!=ESP_OK)return err;
 snprintf(text,sizeof(text),"<p>Laufzeit dieses Durchlaufs: %" PRIu64 " ms. PSRAM-Baum inklusive Index/Stringablage: %u / %u Bytes.</p>",
  r->elapsed_ms,(unsigned)r->tree.used,(unsigned)r->tree.budget);
 err=httpd_resp_sendstr_chunk(req,text);if(err!=ESP_OK)return err;
 err=httpd_resp_sendstr_chunk(req,"<h4>Pfadbeispiele (maximal vier je Kategorie, Summen umfassen alle Eintraege)</h4>");
 for(unsigned c=0;c<DRY_CATEGORY_COUNT&&err==ESP_OK;c++) {
  snprintf(text,sizeof(text),"<h5>%s</h5><ul>",dry_category_name((dry_category_t)c));err=httpd_resp_sendstr_chunk(req,text);
  for(size_t i=0;i<p->categories[c].example_count&&err==ESP_OK;i++) {
   const dry_example_t *e=&p->categories[c].examples[i];
   err=httpd_resp_sendstr_chunk(req,"<li style='white-space:pre-wrap'>");
   if(err==ESP_OK)err=comparison_text(req,e->nas?e->nas->path:e->sd->path);
   if(err==ESP_OK&&e->nas&&e->sd&&strcmp(e->nas->path,e->sd->path)) {
    err=httpd_resp_sendstr_chunk(req," (SD: ");
    if(err==ESP_OK)err=comparison_text(req,e->sd->path);
    if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,")");
   }
   if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,"</li>");
  }
  if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,"</ul>");
 }
 return err;
}
static const char *comparison_kind(dav_kind_t kind)
{
 return kind==DAV_FILE?"Datei":kind==DAV_DIRECTORY?"Ordner":"Sonstige Ressource";
}
static esp_err_t comparison_row(httpd_req_t *req,const dav_tree_t *tree,const dav_tree_entry_t *n,const dav_tree_entry_t *s,bool ambiguous)
{
 char sizes[80];snprintf(sizes,sizeof(sizes),"%s / %s",n&&n->size_known?"bekannt":"-",s&&s->size_known?"bekannt":"-");
 if(n&&s&&n->size_known&&s->size_known)snprintf(sizes,sizeof(sizes),"%" PRIu64 " / %" PRIu64,n->size,s->size);
 else if(n&&n->size_known)snprintf(sizes,sizeof(sizes),"%" PRIu64 " / -",n->size);
 else if(s&&s->size_known)snprintf(sizes,sizeof(sizes),"- / %" PRIu64,s->size);
 const char *result=dry_category_name(dry_classify(tree,n,s,ambiguous));
 const char *names=n&&s&&n->kind!=s->kind?"Datei-/Ordner-Kollision":
  (n&&n->invalid_fat)||(s&&s->invalid_fat)?"Nicht als FAT-LFN darstellbar":
  n&&s&&strcmp(n->path,s->path)?"Gross-/Kleinschreibung oder Unicode-Schreibweise verschieden":"NFC-/FAT-Schluessel geprueft";
 const char *cells[]={n?n->path:"-",s?s->path:"-",n?comparison_kind(n->kind):comparison_kind(s->kind),result,names,sizes,
  n&&*n->etag?n->etag:"-",n&&*n->modified?n->modified:"-",s&&*s->modified?s->modified:"-",
  n&&*n->created?n->created:"-",n&&*n->content_type?n->content_type:"-"};
 esp_err_t err=httpd_resp_sendstr_chunk(req,"<tr>");
 for(size_t i=0;err==ESP_OK&&i<sizeof(cells)/sizeof(cells[0]);i++)err=comparison_cell(req,cells[i]);
 return err==ESP_OK?httpd_resp_sendstr_chunk(req,"</tr>"):err;
}
static esp_err_t comparison_render(httpd_req_t *req)
{
 char query[32]={0};
 bool summary_only=httpd_req_get_url_query_str(req,query,sizeof(query))==ESP_OK&&!strcmp(query,"summary=1");
 esp_err_t err=httpd_resp_sendstr_chunk(req,"<section id='comparison-report'><h3>Test24: Vergleichsbericht</h3>");
 if(err!=ESP_OK)return err;
 if(xSemaphoreTake(state_lock,pdMS_TO_TICKS(500))!=pdTRUE)return httpd_resp_sendstr_chunk(req,"<p>Bericht beschaeftigt.</p></section>");

#ifdef ESP_PLATFORM
 if(comparison_result&&comparison_result->tree.disk&&active_command!=CMD_NONE){xSemaphoreGive(state_lock);httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Geraet arbeitet; Index nach Abschluss erneut laden.");}
#endif
 comparison_result_t *r=comparison_result;
 if(comparison_running||!r)err=comparison_text(req,comparison_running?"Vergleich laeuft; SD-Index wird aufgebaut ...":comparison_note);
 else {
  char summary[512];
  snprintf(summary,sizeof(summary),"<p><b>%s</b> NAS: %u Dateien, %u Ordner; SD: %u Dateien, %u Ordner. "
   "Vollstaendig gelesene Ordner NAS %u/%u, SD %u/%u (jeweils inkl. Wurzel). Baumdaten %u/%u Bytes. Vergleichsdauer %" PRIu64 " ms; Gerätelaufzeit %" PRIu32 " s.</p>",
   r->complete?"VOLLSTAENDIGE MOMENTAUFNAHME":"UNVOLLSTAENDIGE TEILLISTE - kein vollstaendiger Vergleich",
   (unsigned)r->tree.files[0],(unsigned)r->tree.directories[0],(unsigned)r->tree.files[1],(unsigned)r->tree.directories[1],
   (unsigned)r->tree.scanned[0],(unsigned)r->tree.directories[0],(unsigned)r->tree.scanned[1],(unsigned)r->tree.directories[1],
   (unsigned)r->tree.used,(unsigned)r->tree.budget,r->elapsed_ms,r->seconds);
  err=httpd_resp_sendstr_chunk(req,summary);
  if(err==ESP_OK) {
   snprintf(summary,sizeof(summary),"<p>Erkannter PSRAM %u Bytes, frei vor Vergleich %u Bytes, frei nach Auswertung %u Bytes. "
    "SD-Index mit begrenztem RAM-Datensatzcache; der vollstaendige Bestand liegt im reservierten Ordner BNCACHE. Index-Flash wird nicht verwendet.</p>",
    (unsigned)r->psram_total,(unsigned)r->psram_free,(unsigned)r->psram_free_after);
   err=httpd_resp_sendstr_chunk(req,summary);
  }
  if(r->tree.disk&&err==ESP_OK){
   snprintf(summary,sizeof(summary),"<p>SD-Index: %u Bytes, fester Cache mit %u Datensatzplaetzen. Cacheordner BNCACHE ist von Vergleich, Suche und Loeschplanung ausgeschlossen. Abschlusspruefung: %s. Nach USB oder Neustart neu vergleichen.</p>",(unsigned)sd_index_bytes(&r->tree),SD_INDEX_SLOTS,r->complete?"bestaetigt":"nicht bestaetigt");
   err=httpd_resp_sendstr_chunk(req,summary);
  }
  if(r->tree.store&&err==ESP_OK){
   snprintf(summary,sizeof(summary),"<p>Hybridvergleich: %u Bytes Index-Flash, %u persistierte Bloecke/Datensaetze. Abschlusspruefung: %s. Nach Neustart ist stets ein neuer Vergleich erforderlich.</p>",(unsigned)r->tree.store->used,(unsigned)r->tree.store->records,r->tree.store->sealed?"bestaetigt":"nicht bestaetigt");
   err=httpd_resp_sendstr_chunk(req,summary);
   if(err==ESP_OK&&r->tree.store->failure){
    snprintf(summary,sizeof(summary),"<p>Index-Fehlerstufe: %s; Flash-Offset %u.</p>",r->tree.store->failure,(unsigned)r->tree.store->failure_offset);
    err=httpd_resp_sendstr_chunk(req,summary);
   }
  }
  if(!r->complete&&err==ESP_OK) {
   snprintf(summary,sizeof(summary),"<p>Fehler: %s; HTTP %d; Verzeichnis: ",dav_error_name(r->error),r->status);
   err=httpd_resp_sendstr_chunk(req,summary);
   if(err==ESP_OK)err=comparison_text(req,*r->error_path?r->error_path:"(Wurzel)");
   if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,"</p>");
  }
  if(summary_only){
   if(err==ESP_OK&&!r->complete)err=httpd_resp_sendstr_chunk(req,"<p><b>Keine Schreibfreigabe.</b> Nicht gelesene SD-Eintraege sind kein Nachweis einer leeren SD. Bei Cachefehlern APP-Besitz, freien SD-Speicher und den reservierten Cacheordner pruefen. Kein unvollstaendiger Cache erlaubt Schreiben.</p>");
   xSemaphoreGive(state_lock);
   return err==ESP_OK?httpd_resp_sendstr_chunk(req,"</section>"):err;
  }
  if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,
   "<p>Alle Dateien und Unterordner, keine Endungsfilter. Relative Pfade werden fuer den Vergleich in Unicode NFC normalisiert "
   "und mit der FatFs-Unicode-Grossschreibung geprueft; Originalpfade bleiben sichtbar. Mehrdeutige Namen werden NICHT eindeutig zugeordnet. "
   "Gleiche Groesse beweist keine Inhaltsgleichheit. ETags sind nur auf NAS verfuegbar; NAS-Zeit und lokale FAT-Zeit sind nicht direkt vergleichbar. "
   "Dieser Vergleich verwendet Metadaten; der Spiegelungs-Dry-Run liest SD-Inhalte zusaetzlich fuer SHA256. Der Vergleich schreibt nur den eigenen SD-Cache; Nutzdateien bleiben unveraendert. Momentaufnahme nach Aenderungen manuell erneuern.</p>");
  if(err==ESP_OK)err=dry_render(req,r);
  for(unsigned group=0;group<2&&err==ESP_OK;group++) {
   err=httpd_resp_sendstr_chunk(req,group?"<h4>Vollstaendige Ordnerpfade und andere Typen</h4>":"<h4>Dateien aller Endungen</h4>");
   if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,"<div style='overflow-x:auto'><table border='1' cellpadding='5'><thead><tr>"
    "<th>NAS-Relativpfad</th><th>SD-Relativpfad</th><th>Typ</th><th>Vergleich</th><th>Namenspruefung</th><th>Bytes NAS / SD</th>"
    "<th>NAS ETag</th><th>NAS Last-Modified</th><th>SD FAT-Zeit (lokal)</th><th>NAS Creation-Date</th><th>NAS Content-Type</th></tr></thead><tbody>");
   for(const dav_tree_entry_t *n DAV_AUTO=dav_tree_first(&r->tree,0);n&&err==ESP_OK;n=dav_tree_next(&r->tree,n))if(*n->path&&(n->kind!=DAV_FILE)==(group!=0)) {
    bool ambiguous=false;const dav_tree_entry_t *s DAV_AUTO=dav_entry_hold(dav_tree_match(&r->tree,1,n,&ambiguous));
    err=comparison_row(req,&r->tree,n,ambiguous?NULL:s,ambiguous);
   }
   for(const dav_tree_entry_t *s DAV_AUTO=dav_tree_first(&r->tree,1);s&&err==ESP_OK;s=dav_tree_next(&r->tree,s))if(*s->path&&(s->kind!=DAV_FILE)==(group!=0)) {
    bool ambiguous=false;const dav_tree_entry_t *n DAV_AUTO=dav_entry_hold(dav_tree_match(&r->tree,0,s,&ambiguous));
    if(!n||ambiguous)err=comparison_row(req,&r->tree,NULL,s,ambiguous);
   }
   if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,"</tbody></table></div>");
  }
 }
 xSemaphoreGive(state_lock);
 return err==ESP_OK?httpd_resp_sendstr_chunk(req,"</section>"):err;
}
static uint32_t comparison_upper(uint32_t c){return (uint32_t)ff_wtoupper(c);}
static dav_error_t comparison_add(comparison_result_t *r,unsigned side,const char *parent,const dav_entry_t *entry)
{
 char relative[DAV_PATH_LIMIT];
 int n=snprintf(relative,sizeof(relative),"%s%s%s",parent,*parent?"/":"",entry->name);
 if(n<0||(size_t)n>=sizeof(relative))return DAV_ERR_RESOURCE;
 // Keep working space available to TLS/parser/stdio and fail explicitly, not reset.
#ifdef ESP_PLATFORM
 if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<24*1024)return DAV_ERR_MEMORY;
#else
 if(esp_get_free_heap_size()<24*1024)return DAV_ERR_MEMORY;
#endif
 dav_error_t error=dav_tree_add(&r->tree,side,relative,entry,comparison_upper);
 if(error==DAV_OK&&(r->tree.count[side]%64)==0)
  display_ui_compare(side+1,r->tree.scanned[side],r->tree.directories[side],r->tree.count[0]+r->tree.count[1]-2);
 return error;
}
typedef struct { comparison_result_t *result; const char *parent; dav_error_t error; } comparison_emit_context_t;
static bool comparison_emit(void *context,const dav_entry_t *entry)
{
 comparison_emit_context_t *c=context;
 c->error=comparison_add(c->result,0,c->parent,entry);return c->error==DAV_OK;
}
static char *comparison_url(const char *relative)
{
 size_t base=strlen(WEBDAV_URL),len=strlen(relative);
 char *url=malloc(base+3*len+3);if(!url)return NULL;
 memcpy(url,WEBDAV_URL,base);size_t n=base;if(!n||url[n-1]!='/')url[n++]='/';
 static const char hex[]="0123456789ABCDEF";
 for(const unsigned char *p=(const unsigned char *)relative;*p;p++) {
  unsigned char c=*p;
  if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~'||c=='/')url[n++]=(char)c;
  else {url[n++]='%';url[n++]=hex[c>>4];url[n++]=hex[c&15];}
 }
 if(len)url[n++]='/';
 url[n]=0;return url;
}
#if BERNINA_NAS_INDEX
#include "sync_policy_adapter.inc"
#include "nas_index_adapter.inc"
#ifdef ESP_PLATFORM
#include "sd_compare_adapter.inc"
#endif
#endif

static dav_error_t comparison_nas_directory(comparison_result_t *r,const char *relative)
{
 r->status=-1;r->xml_offset=0;
 if(!app_access_allowed())return DAV_ERR_OWNERSHIP;
 char *url=comparison_url(relative);if(!url)return DAV_ERR_MEMORY;
 comparison_emit_context_t emit={.result=r,.parent=relative};
 dav_stream_t *parser=dav_stream_new(url,comparison_emit,&emit);
 esp_http_client_handle_t client=NULL;dav_error_t error=DAV_ERR_MEMORY;
 if(!parser)goto done;
 esp_http_client_config_t config={.url=url,.username=WEBDAV_USER,.password=WEBDAV_PASSWORD,
  .auth_type=HTTP_AUTH_TYPE_BASIC,.cert_pem=synology_cert_start,.transport_type=HTTP_TRANSPORT_OVER_SSL,
  .disable_auto_redirect=true,.timeout_ms=15000,.buffer_size=2048};
 client=esp_http_client_init(&config);if(!client)goto done;
 static const char body[]="<?xml version=\"1.0\" encoding=\"utf-8\"?>"
  "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:resourcetype/><d:getcontentlength/><d:getetag/>"
  "<d:getlastmodified/><d:creationdate/><d:getcontenttype/></d:prop></d:propfind>";
 error=DAV_ERR_HTTP;r->status=-1;r->xml_offset=0;
 if(esp_http_client_set_method(client,HTTP_METHOD_PROPFIND)!=ESP_OK||
  esp_http_client_set_header(client,"Depth","1")!=ESP_OK||
  esp_http_client_set_header(client,"Content-Type","application/xml; charset=utf-8")!=ESP_OK||
  esp_http_client_set_header(client,"Accept-Encoding","identity")!=ESP_OK||
  esp_http_client_open(client,sizeof(body)-1)!=ESP_OK)goto done;
 size_t sent=0;
 while(sent<sizeof(body)-1){int n=esp_http_client_write(client,body+sent,sizeof(body)-1-sent);if(n<=0)goto done;sent+=(size_t)n;}
 int64_t length=esp_http_client_fetch_headers(client);r->status=esp_http_client_get_status_code(client);
 if(length<0||r->status!=207)goto done;
 size_t received=0;
 while(true) {
  char buffer[512];int n=esp_http_client_read(client,buffer,sizeof(buffer));
  if(n<0){error=DAV_ERR_HTTP_INCOMPLETE;goto done;}
  if(!n)break;
  received+=(size_t)n;
  error=dav_stream_feed(parser,buffer,(size_t)n);r->xml_offset=dav_stream_offset(parser);
  if(error!=DAV_OK){if(emit.error!=DAV_OK)error=emit.error;goto done;}
 }
 error=DAV_ERR_HTTP_INCOMPLETE;
 if(!esp_http_client_is_complete_data_received(client)||(length>0&&(uint64_t)length!=received))goto done;
 error=dav_stream_finish(parser);
done:
 if(client)esp_http_client_cleanup(client);
 dav_stream_free(parser);free(url);return error;
}
static dav_error_t comparison_sd_directory(comparison_result_t *r,const char *relative)
{
 if(!app_access_allowed())return DAV_ERR_OWNERSHIP;
 char path[sizeof(SD_PATH)+DAV_PATH_LIMIT+256];
 int prefix=snprintf(path,sizeof(path),"%s%s%s",SD_PATH,*relative?"/":"",relative);
 if(prefix<0||(size_t)prefix+257>=sizeof(path))return DAV_ERR_RESOURCE;
 DIR *dir=opendir(path);if(!dir)return DAV_ERR_SD;
 dav_error_t error=DAV_OK;
 while(error==DAV_OK) {
  if(!app_access_allowed()){error=DAV_ERR_OWNERSHIP;break;}
  errno=0;struct dirent *v=readdir(dir);
  if(!v){if(errno)error=DAV_ERR_SD;break;}
  if(!strcmp(v->d_name,".")||!strcmp(v->d_name,".."))continue;
  if(r->tree.disk&&!*relative&&sd_index_reserved(v->d_name))continue;
  if(strlen(v->d_name)>=256||!dav_utf8_valid(v->d_name)){error=DAV_ERR_PATH;break;}
  int n=snprintf(path+prefix,sizeof(path)-(size_t)prefix,"/%s",v->d_name);struct stat st;
  if(n<0||(size_t)n>=sizeof(path)-(size_t)prefix){error=DAV_ERR_RESOURCE;break;}
  if(stat(path,&st)!=0||st.st_size<0){error=DAV_ERR_SD;break;}
  dav_entry_t entry={.kind=S_ISREG(st.st_mode)?DAV_FILE:S_ISDIR(st.st_mode)?DAV_DIRECTORY:DAV_OTHER,
   .size=(uint64_t)st.st_size,.size_known=S_ISREG(st.st_mode)};
  if(sync_sd_ignored(relative,entry.kind==DAV_DIRECTORY))continue;
  strcpy(entry.name,v->d_name);struct tm tm;
  if(localtime_r(&st.st_mtime,&tm))strftime(entry.modified,sizeof(entry.modified),"%Y-%m-%d %H:%M:%S",&tm);
  error=comparison_add(r,1,relative,&entry);
 }
 if(closedir(dir)!=0&&error==DAV_OK)error=DAV_ERR_SD;
 return error;
}
static void compare_nas_sd(void)
{
 TickType_t started=xTaskGetTickCount();
 if(usb_test_phase!=2||!app_owned){event_log("Vergleich verweigert: APP-Uebergabe erforderlich");return;}
 if(!app_access_allowed())return;
 xSemaphoreTake(state_lock,portMAX_DELAY);
#if defined(ESP_PLATFORM) && BERNINA_NAS_INDEX
 if(comparison_result&&comparison_result->tree.disk){
  xSemaphoreGive(state_lock);if(!comparison_sd_discard())return;xSemaphoreTake(state_lock,portMAX_DELAY);
 }
#endif
 if(comparison_result){dry_plan_release(&comparison_result->plan);dav_tree_clear(&comparison_result->tree);free(comparison_result);comparison_result=NULL;}
 display_ui_compare(0,0,0,0);
 comparison_running=true;display_compare_failed=false;comparison_revision++;xSemaphoreGive(state_lock);
 comparison_result_t *r=calloc(1,sizeof(*r));
 if(r) {
  r->status=-1;
  r->tree.budget=dav_tree_platform_budget(&r->psram_total,&r->psram_free);
  if(!r->tree.budget)r->error=DAV_ERR_MEMORY;
  else if(!wifi_has_ip||!ensure_network_time())r->error=DAV_ERR_HTTP;
  else if(strncmp(WEBDAV_URL,"https://",8))r->error=DAV_ERR_PATH;
  else {
   dav_entry_t root={.kind=DAV_DIRECTORY};
#if defined(ESP_PLATFORM) && BERNINA_NAS_INDEX
   if(!comparison_sd_open(&r->tree))r->error=DAV_ERR_STORE;
   r->tree.ignore_path=sync_sd_ignored;
#endif
   if(r->error==DAV_OK)r->error=dav_tree_add(&r->tree,0,"",&root,comparison_upper);

   #if BERNINA_NAS_INDEX
   if(r->error==DAV_OK)r->error=nas_index_compare(r);
#endif
   if(r->error==DAV_OK)r->error=dav_tree_add(&r->tree,1,"",&root,comparison_upper);
   for(unsigned side=0;side<2&&r->error==DAV_OK;side++) {
    #if BERNINA_NAS_INDEX
    if(side==0)continue;
#endif
    // Linked directory work queue; no recursive C stack and one open directory.
    for(dav_tree_entry_t *e DAV_AUTO=dav_tree_first(&r->tree,side);e&&r->error==DAV_OK;e=dav_tree_next(&r->tree,e))if(e->kind==DAV_DIRECTORY&&!e->scanned) {
     display_ui_compare(side+1,r->tree.scanned[side],r->tree.directories[side],r->tree.count[0]+r->tree.count[1]-2);
     snprintf(r->error_path,sizeof(r->error_path),"%s",e->path);
     r->error=side?comparison_sd_directory(r,e->path):comparison_nas_directory(r,e->path);
     if(r->error==DAV_OK){e->scanned=true;r->tree.scanned[side]++;}
     display_ui_compare(side+1,r->tree.scanned[side],r->tree.directories[side],r->tree.count[0]+r->tree.count[1]-2);
    }
   }
  }
  if(!dav_tree_healthy(&r->tree))r->error=DAV_ERR_STORE;
  r->complete=r->error==DAV_OK&&r->tree.scanned[0]==r->tree.directories[0]&&r->tree.scanned[1]==r->tree.directories[1];
  if(r->complete) {
   if(!app_access_allowed()){r->error=DAV_ERR_OWNERSHIP;r->complete=false;}
   else if(esp_vfs_fat_info(SD_PATH,&r->sd_total,&r->sd_free)!=ESP_OK || r->sd_free>r->sd_total) {
    r->error=DAV_ERR_SD;r->complete=false;
   } else r->capacity_known=true;
  }
  display_ui_compare(3,0,0,r->tree.count[0]+r->tree.count[1]);
  dry_plan_build(&r->tree,r->complete,&r->plan);
  if(!dav_tree_healthy(&r->tree)){r->error=DAV_ERR_STORE;r->complete=false;r->plan.complete=false;}
  if(r->plan.overflow){r->error=DAV_ERR_RESOURCE;r->complete=false;}
#if BERNINA_NAS_INDEX
  if(r->complete&&r->tree.disk&&!sd_index_seal(&r->tree,nas_index_generation)){r->error=DAV_ERR_STORE;r->complete=false;r->plan.complete=false;}
  if(r->complete&&r->tree.store&&!dav_store_seal(r->tree.store,nas_index_generation,r->tree.count[0],r->tree.count[1])){r->error=DAV_ERR_STORE;r->complete=false;r->plan.complete=false;}
  if(r->complete&&!nas_index_checkpoint()){r->error=DAV_ERR_SD;r->complete=false;r->plan.complete=false;}
#endif
  size_t ignored_total;
  (void)dav_tree_platform_budget(&ignored_total,&r->psram_free_after);
  r->elapsed_ms=(uint64_t)(TickType_t)(xTaskGetTickCount()-started)*portTICK_PERIOD_MS;
  r->seconds=(uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS/1000);
 }
 xSemaphoreTake(state_lock,portMAX_DELAY);
 if(!r)snprintf(comparison_note,sizeof(comparison_note),"%s",dav_error_name(DAV_ERR_MEMORY));
 nas_last_http=r?r->status:-1;comparison_stale=false;comparison_completed_at=time(NULL);comparison_completed_us=esp_timer_get_time();
 display_compare_failed=!(r&&r->complete);
 comparison_result=r;comparison_running=false;comparison_revision++;xSemaphoreGive(state_lock);
 event_log(r&&r->complete?"Rekursiver Vergleich vollstaendig; Inhalte ungeprueft":"Vergleich UNVOLLSTAENDIG; Fehler und Zaehler im Bericht");
}

// Test 17a: genau die bereits bekannte NAS-Testdatei, keine Synchronisation.
// Erwartungswerte entsprechen der unveraenderten lesenden NAS-Diagnose.
#define NAS_TEST_BYTES 1888
static const unsigned char nas_test_sha256[32] __attribute__((unused)) = {
    0xe8,0xd4,0xe3,0x12,0x85,0x7e,0x20,0xe5,
    0x91,0x71,0xfb,0x4b,0xf7,0x84,0xc2,0x70,
    0x95,0x64,0xea,0x62,0xa7,0xa5,0xe9,0x01,
    0xa8,0x4b,0x9e,0xc1,0x26,0x2c,0xec,0xfd
};



static bool perform_app_handoff(void)
{
            if(usb_test_phase==2&&app_access_allowed())return true;
            if (usb_test_phase != 0) { event_log("Trennen ignoriert: nicht im USB-Modus"); return false; }
            app_owned = false;
            usb_test_phase = 1;
            usb_disconnect_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            usb_diag_log_snapshot("vor Trennen");
            event_log("USB: Trennung angefordert");
            if (update_usb_ready && !tud_disconnect()) {
                usb_test_phase = 3;
                event_log("USB-Trennung fehlgeschlagen; kein SD-Zugriff");
                return false;
            }
            usb_detached_by_app = true;
            // tud_mounted() ist kein sicherer Besitznachweis. Das Storage-API
            // entscheidet, ob die SD auf APP umgeschaltet werden kann.
            if(!update_usb_ready)event_log("APP-Uebernahme: USB wegen Journal beim Start nicht angemeldet");
            for (int i = 0; update_usb_ready && i < 30; i++) {
                vTaskDelay(pdMS_TO_TICKS(100));
                usb_diag_sample();
            }
            usb_diag_log_snapshot("3s nach Trennen");
            event_log(tud_mounted() ? "USB Status nach 3s: konfiguriert (nur Diagnose)" :
                                      "USB Status nach 3s: unkonfiguriert");
            if (mount_app()) {
                app_owned = true;
                diag_app_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
                usb_diag_storage();
                usb_diag_log_snapshot("APP uebernommen");
                msc_log_state("APP bestaetigt");
                usb_test_phase = 2;
                last_error = 0;
                event_log("SD fuer ESP32 freigegeben");
            } else {
                usb_test_phase = 3;
                event_log("Schreibzugriff gesperrt; Neustart erforderlich");
                return false;
            }
 return app_access_allowed();
}

static void usb_test_task(void *arg)
{
    (void)arg;
    command_t cmd;
    event_log("BERNINA WiFi Stick 1.0.3 bereit");
    usb_diag_log_snapshot("Start");
    msc_log_state("Start");
    while (true) {
        usb_diag_sample();
        xSemaphoreTake(state_lock,portMAX_DELAY);
        bool got_command=xQueueReceive(command_queue,&cmd,0)==pdTRUE;
        active_command=got_command?cmd:CMD_NONE;
        xSemaphoreGive(state_lock);
        if(!got_command){app_events_flush();fast_schedule();vTaskDelay(pdMS_TO_TICKS(100));continue;}
        // Defence in depth, including commands already queued by another caller.
        if (cmd == CMD_CREATE || cmd == CMD_NAS_DOWNLOAD || cmd == CMD_NAS_CHECK) {
            event_log("Historischer Schreibtest verweigert; separaten aktuellen Testfall verwenden");
            continue;
        }
        if(cmd==CMD_FAST_RUN){recovery_manual_start();fast_worker();
        } else if(cmd==CMD_AUTO_RETRY){recovery_worker();
        } else if(cmd==CMD_REPAIR_FLOW){recovery_manual_start();app_repair_flow();
        } else if(cmd==CMD_APP_REBOOT){event_log("Neustart angefordert");app_events_flush();vTaskDelay(pdMS_TO_TICKS(200));esp_restart();
        } else if(cmd==CMD_FAST_REPAIR){fast_repair_worker();
        } else if(cmd==CMD_FAST_RESET){fast_reset_worker();
        } else if(cmd==CMD_MIRROR_PLAN||cmd==CMD_MIRROR_RUN){
            mirror_worker(cmd);
        } else if(cmd==CMD_UPDATE_RECOVER){
            update_worker(cmd);
        } else if (cmd == CMD_COMPARE) {
            compare_nas_sd();
        } else if (cmd == CMD_DISCONNECT) {
            (void)perform_app_handoff();
        } else if (cmd == CMD_CONNECT || cmd == CMD_RECOVER) {
            if (cmd == CMD_CONNECT && (usb_test_phase != 2 || !app_owned || fast_lock)) {
                event_log("Verbinden verweigert: SD nicht im APP-Modus"); continue;
            }
            if (cmd == CMD_RECOVER && usb_test_phase != 3) {
                event_log("Wiederherstellung nur im Fehlerzustand"); continue;
            }
            if (storage_fault || io_fault) {
                event_log("Wiederherstellung gesperrt: MSC-/Dateifehler; Neustart erforderlich");
                continue;
            }
#if defined(ESP_PLATFORM) && BERNINA_NAS_INDEX
            if(cmd==CMD_CONNECT&&!comparison_sd_discard()){event_log("USB gesperrt: SD-Index nicht sicher geschlossen");continue;}
#endif
            usb_test_phase = 1;
            if (cmd == CMD_CONNECT) {
                if (!app_access_allowed()) continue;
                event_log("SD an USB zurueckgeben");
                if (!mount_usb()) { usb_test_phase = 3; continue; }
            }
            // Recovery nur nach Transportfehler mit weiterhin bestaetigtem USB-Besitz.
            tinyusb_msc_mount_point_t actual;
            if (confirmed_owner != TINYUSB_MSC_STORAGE_MOUNT_USB ||
                msc_snapshot().sequence != confirmed_sequence ||
                tinyusb_msc_get_storage_mount_point(storage_handle, &actual) != ESP_OK ||
                actual != TINYUSB_MSC_STORAGE_MOUNT_USB) {
                storage_fault = true; usb_test_phase = 3;
                event_log("USB-Anmeldung gesperrt: Speicherzustand unklar");
                continue;
            }
            app_owned = false;
            diag_usb_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            usb_diag_log_snapshot("USB zurueckgegeben");
            msc_log_state("USB bestaetigt");
            if (!update_usb_connect()) {
                usb_test_phase = 3;
                event_log("USB-Anmeldung fehlgeschlagen; erneuter Versuch moeglich");
                continue;
            }
            usb_detached_by_app = false;
            usb_reconnect_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            usb_test_count++;
            usb_test_phase = 0;
            last_error = 0;
            event_log("USB: Wiederanmeldung angefordert");
            usb_diag_log_snapshot("nach Connect");
        }
    }
}

// Searches only the immutable published RAM snapshot. No filesystem or NAS access.


static esp_err_t sync_events(httpd_req_t *req)
{
 if(xSemaphoreTake(state_lock,pdMS_TO_TICKS(300))!=pdTRUE){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Protokoll beschaeftigt");}
 esp_err_t result=httpd_resp_sendstr_chunk(req,"<h2>Ereignisprotokoll</h2><p>Letzte 128 Ereignisse dieser Laufzeit; kein Dateiverzeichnis wird gelesen.</p><pre>");
 unsigned first=log_count>MAX_LOG?log_count-MAX_LOG:0;
 for(unsigned i=first;i<log_count&&result==ESP_OK;i++){result=comparison_text(req,logs[i%MAX_LOG]);if(result==ESP_OK)result=httpd_resp_sendstr_chunk(req,"\n");}
 if(result==ESP_OK)result=httpd_resp_sendstr_chunk(req,"</pre>");
 xSemaphoreGive(state_lock);return result==ESP_OK?httpd_resp_send_chunk(req,NULL,0):result;
}
static esp_err_t home_summary(httpd_req_t *req)
{
 if(xSemaphoreTake(state_lock,pdMS_TO_TICKS(300))!=pdTRUE){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr_chunk(req,"Status beschaeftigt; erneut pruefen.");}
 bool working=active_command!=CMD_NONE||mirror_pending||update_pending||comparison_running||(command_queue&&uxQueueMessagesWaiting(command_queue));
 char text[640];snprintf(text,sizeof(text),"<section id='runtime' data-device-busy='%u'><h2>%s</h2><p>Speicher: %s; WLAN: %s; Laufzeit %" PRIu32 " s; letzter NAS-HTTP %d.</p><p>Vergleich: %s. Matrix: %s.</p>",working?1:0,working?"VORGANG LÄUFT – bitte warten":"KEIN VORGANG AKTIV",usb_test_phase==2&&app_owned?"APP":usb_test_phase==0?"USB":"GESPERRT",wifi_has_ip?"verbunden":"getrennt",(uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS/1000),nas_last_http,comparison_stale?"veraltet":comparison_result&&comparison_result->complete?"vollstaendig":"nicht vollstaendig",display_ui_matrix_enabled()?"ein":"aus");
 esp_err_t result=httpd_resp_sendstr_chunk(req,text);
 if(result==ESP_OK)result=fast_summary(req);
 if(result==ESP_OK&&!fast_started)result=mirror_summary(req);
 if(result==ESP_OK&&mirror_state==MIRROR_PREVIEW&&mirror_fresh()){snprintf(text,sizeof(text),"<p id='mirror-ticket' data-ticket='%s' data-danger='%u'>Plan bereit; separate Bestaetigungen erforderlich.</p>",mirror_ticket,mirror_plan.dangerous?1:0);result=httpd_resp_sendstr_chunk(req,text);}
 if(result==ESP_OK&&update_lock){result=httpd_resp_sendstr_chunk(req,"<p>Datei-Recovery offen: ");if(result==ESP_OK)result=comparison_text(req,update_reason);if(result==ESP_OK)result=httpd_resp_sendstr_chunk(req,"</p>");}
 if(result==ESP_OK&&*sync_policy.candidate){snprintf(text,sizeof(text),"<p id='source-candidate' data-source='%s'>Neue NAS-Quelle erkannt. Erst alte Vorgänge abschliessen, dann in der Diagnose ausdrücklich bestätigen.</p>",sync_policy.candidate);result=httpd_resp_sendstr_chunk(req,text);}
 if(result==ESP_OK)result=httpd_resp_sendstr_chunk(req,"</section>");
 xSemaphoreGive(state_lock);return result;
}
static esp_err_t home_get_handler(httpd_req_t *req)
{
 if(req->user_ctx){httpd_resp_set_type(req,"text/html; charset=utf-8");httpd_resp_set_hdr(req,"Cache-Control","no-store");esp_err_t result=home_summary(req);return result==ESP_OK?httpd_resp_send_chunk(req,NULL,0):result;}
 return sync_page(req,false);
}
static esp_err_t protected_diagnostic_handler(httpd_req_t *req)
{
 httpd_resp_set_hdr(req,"Cache-Control","no-store");
 if(!control_authenticated(req)){httpd_resp_set_status(req,"403 Forbidden");httpd_resp_set_hdr(req,"Connection","close");return httpd_resp_sendstr(req,"Steuerpasswort fehlt oder ist ungueltig");}
 if(comparison_result&&comparison_result->tree.disk&&active_command!=CMD_NONE&&((uintptr_t)req->user_ctx==3||(uintptr_t)req->user_ctx==4||(uintptr_t)req->user_ctx==5||(uintptr_t)req->user_ctx==7||(uintptr_t)req->user_ctx==12||(uintptr_t)req->user_ctx==20)){
  httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Geraet arbeitet; Indexbericht nach Abschluss laden.");
 }
 switch((uintptr_t)req->user_ctx){
 case 1:case 2:req->user_ctx=(uintptr_t)req->user_ctx==1?(void *)(uintptr_t)1:NULL;return diagnostic_get_handler(req);
 case 3:{httpd_resp_set_type(req,"text/html; charset=utf-8");esp_err_t result=comparison_render(req);return result==ESP_OK?httpd_resp_send_chunk(req,NULL,0):result;}
 case 8:return matrix_setting_handler(req);
 case 14:return update_status_handler(req);
 case 15:return update_recovery_handler(req);
 case 16:case 17:case 18:case 19:case 20:return mirror_handler(req,(unsigned)(uintptr_t)req->user_ctx-16);
 case 21:return sync_adopt_source(req);
 case 22:return sync_events(req);
 case 23:case 24:case 25:case 26:case 27:return fast_handler(req,(unsigned)(uintptr_t)req->user_ctx-23);
 default:return ESP_FAIL;
 }
}





// Test 17a: bounded selection/job state, never a copy of either directory tree.
static struct {
 char path[DAV_PATH_LIMIT],etag[128],ticket[17];
 uint64_t size;
 uint32_t revision;
 bool valid,pending,allow_directories;
} single_selection;
typedef struct {esp_http_client_handle_t client;mbedtls_sha256_context sha;const char *path,*etag;int status;bool encoded,strict,etag_seen,etag_mismatch;} single_transport_t;
static bool single_owned(void *c){(void)c;return app_access_allowed();}
static bool single_space(void *c,uint64_t needed){(void)c;uint64_t total,available;return app_access_allowed()&&esp_vfs_fat_info(SD_PATH,&total,&available)==ESP_OK&&available<=total&&available>=needed;}
static esp_err_t single_http_event(esp_http_client_event_t *event)
{
 single_transport_t *c=event->user_data;
 if(c&&c->strict&&event->event_id==HTTP_EVENT_ON_HEADER&&event->header_key&&event->header_value&&!strcasecmp(event->header_key,"ETag")){
  c->etag_seen=true;if(strcmp(event->header_value,c->etag))c->etag_mismatch=true;
 }
 if(c&&event->event_id==HTTP_EVENT_ON_HEADER&&event->header_key&&event->header_value&&
    !strcasecmp(event->header_key,"Content-Encoding")&&strcasecmp(event->header_value,"identity"))c->encoded=true;
 return ESP_OK;
}

static int single_read(void *c,unsigned char *p,size_t n){return esp_http_client_read(((single_transport_t *)c)->client,(char *)p,n);}
static bool single_complete(void *c){return esp_http_client_is_complete_data_received(((single_transport_t *)c)->client);}
static bool single_hash_start(void *c){return mbedtls_sha256_starts(&((single_transport_t *)c)->sha,0)==0;}
static bool single_hash_update(void *c,const unsigned char *p,size_t n){return mbedtls_sha256_update(&((single_transport_t *)c)->sha,p,n)==0;}
static bool single_hash_finish(void *c,unsigned char *p){return mbedtls_sha256_finish(&((single_transport_t *)c)->sha,p)==0;}
static int single_publish(void *c,const char *from,const char *to){(void)c;return app_access_allowed()?rename(from,to):-1;}



#include "update_adapter.inc"
#include "mirror_adapter.inc"
#if defined(ESP_PLATFORM) && BERNINA_NAS_INDEX
static bool comparison_sd_discard(void)
{
 xSemaphoreTake(state_lock,portMAX_DELAY);
 if(comparison_result&&comparison_result->tree.disk){
  comparison_stale=true;comparison_revision++;comparison_result->complete=false;comparison_result->plan.complete=false;
  if(!sd_index_close_file(&comparison_result->tree)){xSemaphoreGive(state_lock);return false;}
  update_valid=false;mirror_plan.valid=false;
  dry_plan_release(&comparison_result->plan);dav_tree_clear(&comparison_result->tree);free(comparison_result);comparison_result=NULL;
  snprintf(comparison_note,sizeof(comparison_note),"SD-Index geschlossen und verworfen; nach USB erneut vollstaendig vergleichen");
 }
 xSemaphoreGive(state_lock);return true;
}
#endif


#include "fast_sync_adapter.inc"
#include "recovery_adapter.inc"
#include "app_runtime.inc"

// Source migration is a separate password-protected, explicit metadata action.
static esp_err_t sync_adopt_source(httpd_req_t *req)
{
 if(!app_access_allowed()){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Bestaetigter APP-Besitz fehlt");}
 char source[65]={0},phrase[32]={0};
 bool header=httpd_req_get_hdr_value_str(req,"X-Bernina-Source",source,sizeof(source))==ESP_OK&&httpd_req_get_hdr_value_str(req,"X-Bernina-Confirm",phrase,sizeof(phrase))==ESP_OK;
 xSemaphoreTake(state_lock,portMAX_DELAY);
 bool ok=header&&!strcmp(phrase,"QUELLE WECHSELN")&&sync_hex(source)&&!strcmp(source,sync_policy.candidate)&&!mirror_lock&&!update_lock&&!fast_lock&&!fast_busy&&mirror_idle()&&app_owned&&usb_test_phase==2;
 xSemaphoreGive(state_lock);
 if(!ok){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Quellenwechsel gesperrt: aktueller Kandidat, APP, Leerlauf und abgeschlossene Journale erforderlich");}
 if(!comparison_sd_discard()){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Index konnte nicht sicher geschlossen werden");}
 nvs_handle_t handle;esp_err_t result=nvs_open("bernina_idx",NVS_READWRITE,&handle);
 if(result==ESP_OK){result=nvs_set_str(handle,"source",source);if(result==ESP_OK)result=nvs_commit(handle);char check[65];size_t size=sizeof(check);if(result==ESP_OK&&(nvs_get_str(handle,"source",check,&size)!=ESP_OK||strcmp(check,source)))result=ESP_FAIL;nvs_close(handle);}
 if(result!=ESP_OK){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Quellbindung nicht dauerhaft bestaetigt; keine Freigabe");}
 xSemaphoreTake(state_lock,portMAX_DELAY);sync_policy.loaded=false;sync_policy.candidate[0]=0;comparison_stale=true;comparison_revision++;xSemaphoreGive(state_lock);
 event_log("NAS-Quellwechsel ausdruecklich bestaetigt; Vollvergleich und neue Freigaben erforderlich");
 return httpd_resp_sendstr(req,"Neue NAS-Quelle gebunden. Vollstaendig neu vorbereiten; keine Kopier-/Loeschfreigabe uebernommen.");
}

// Observational adapter only. Never calls app_access_allowed() (it has side effects).
static void display_snapshot(display_input_t *out)
{
 if(state_lock&&xSemaphoreTake(state_lock,0)==pdTRUE){
  if(mirror_pending||mirror_state>=MIRROR_COMPLETE){out->file_index=mirror_completed;out->file_count=mirror_plan.files;out->succeeded=mirror_completed;out->failed=mirror_state==MIRROR_FAILED?1:0;}
  else if(update_pending||update_finished){out->file_index=1;out->file_count=1;out->succeeded=update_succeeded;out->failed=update_failed;}
  xSemaphoreGive(state_lock);
 }
 out->initialized=display_system_initialized;
 out->comparing=(fast_busy&&!fast_copying)||active_command==CMD_COMPARE||(mirror_pending&&mirror_state==MIRROR_PLANNING);
 out->busy=active_command!=CMD_NONE;
 out->fault=(fast_lock&&!fast_busy)||storage_fault||io_fault||usb_test_phase==3||display_compare_failed||(update_lock&&!update_pending)||(mirror_lock&&!mirror_pending)||mirror_state==MIRROR_FAILED;
 out->error=fast_lock&&!fast_busy?"SYNC UNVOLLSTAENDIG":(mirror_lock&&!mirror_pending)||mirror_state==MIRROR_FAILED?"SYNC UNVOLLSTAENDIG":update_lock&&!update_pending?"UPDATE RECOVERY":io_fault?"DATEI SD FEHLER":storage_fault||usb_test_phase==3?"MSC SD FEHLER":display_compare_failed?"VERGLEICH FEHLER":NULL;
 out->retry_waiting=recovery_active()&&active_command==CMD_NONE;
 out->retry_seconds=recovery_next_us>esp_timer_get_time()?(recovery_next_us-esp_timer_get_time())/1000000:0;
 if(out->retry_waiting)out->fault=storage_fault||io_fault||usb_test_phase==3||update_lock||mirror_lock;
 if(!out->initialized||!storage_handle)return;
 msc_state_t before=msc_snapshot();tinyusb_msc_mount_point_t actual;
 bool queried=tinyusb_msc_get_storage_mount_point(storage_handle,&actual)==ESP_OK;
 msc_state_t after=msc_snapshot();
 if(!queried){out->fault=true;out->error="MSC ABFRAGE FEHLER";return;}
 if(before.sequence!=after.sequence)return;
 bool proof=before.sequence==confirmed_sequence;
 out->app_confirmed=!out->fault&&usb_test_phase==2&&app_owned&&usb_detached_by_app&&proof&&
  confirmed_owner==TINYUSB_MSC_STORAGE_MOUNT_APP&&actual==TINYUSB_MSC_STORAGE_MOUNT_APP;
 // Library 2.4.0 initially assigns USB without MOUNT_COMPLETE; successful creation
 // plus actual USB mapping and first enumeration is the initial evidence.
 bool boot=display_boot_usb&&before.sequence==0;
 bool returned=proof&&confirmed_owner==TINYUSB_MSC_STORAGE_MOUNT_USB&&
  before.last_event==TINYUSB_MSC_EVENT_MOUNT_COMPLETE&&before.complete_point==TINYUSB_MSC_STORAGE_MOUNT_USB;
 out->usb_confirmed=!out->fault&&usb_test_phase==0&&!app_owned&&!usb_detached_by_app&&actual==TINYUSB_MSC_STORAGE_MOUNT_USB&&(boot||returned);
 out->host_ready=tud_connected()&&tud_mounted()&&!tud_suspended();
 if(before.failures||(usb_test_phase==2&&!out->app_confirmed)||(actual==TINYUSB_MSC_STORAGE_MOUNT_APP&&usb_test_phase==0)){
  out->fault=true;out->error="MSC ZUSTAND UNKLAR";
 }
}
static esp_err_t matrix_setting_handler(httpd_req_t *req)
{
 char setting[2]={0};
 if(httpd_req_get_hdr_value_str(req,"X-Bernina-Matrix",setting,sizeof(setting))!=ESP_OK||
    (setting[0]!='0'&&setting[0]!='1')){httpd_resp_set_status(req,"400 Bad Request");return httpd_resp_sendstr(req,"Matrix: Ein/Aus fehlt");}
 if(update_controls_blocked()||active_command!=CMD_NONE||(command_queue&&uxQueueMessagesWaiting(command_queue))){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Matrix-Einstellung bitte im Leerlauf speichern");}
 esp_err_t error=display_ui_set_matrix(setting[0]=='1');
 if(error!=ESP_OK){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Matrix-Einstellung nicht gespeichert; bisheriger Wert bleibt aktiv");}
 return httpd_resp_sendstr(req,setting[0]=='1'?"Matrix Ein gespeichert; nur bei bestaetigtem BEREIT":"Matrix Aus gespeichert; Statusanzeige bleibt aktiv");
}

void app_main(void)
{
    setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1);tzset();app_identify();
    ESP_LOGI(TAG, "BERNINA WiFi Stick 1.0.3");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "NVS-Initialisierung fehlgeschlagen; vorhandene Daten bleiben erhalten");
        return;
    }
    ESP_ERROR_CHECK(ret);
    update_boot_journal();
    mirror_boot_journal();
    fast_boot();
    app_config_boot();
    recovery_boot();
    display_ui_start(display_snapshot);
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI, .miso_io_num = SD_MISO,
        .sclk_io_num = SD_SCK, .quadwp_io_num = -1,
        .quadhd_io_num = -1, .max_transfer_sz = 8192
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA));
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    ESP_ERROR_CHECK(sdspi_host_init());
    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = SD_CS;
    slot_cfg.host_id = SPI2_HOST;
    sdspi_dev_handle_t sd_device;
    ESP_ERROR_CHECK(sdspi_host_init_device(&slot_cfg, &sd_device));
    host.slot = sd_device;
    ESP_ERROR_CHECK(sdmmc_card_init(&host, &sd_card));
    uint64_t capacity = (uint64_t)sd_card.csd.capacity * sd_card.csd.sector_size;
    ESP_LOGI(TAG, "microSD: %" PRIu64 " MiB", capacity / (1024ULL * 1024ULL));
    tinyusb_msc_driver_config_t msc_cfg = {
        .user_flags.auto_mount_off = 1,
        .callback = msc_storage_event_cb,
    };
    ESP_ERROR_CHECK(tinyusb_msc_install_driver(&msc_cfg));
    tinyusb_msc_storage_config_t storage_cfg = {
        .medium.card = &sd_card,
        .fat_fs = { .do_not_format = true, .base_path = SD_PATH,
            .config = { .max_files = 8 } }, // SD index + original + independent backup, with reserve
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB
    };
    ESP_ERROR_CHECK(tinyusb_msc_new_storage_sdmmc(&storage_cfg, &storage_handle));
    display_boot_usb=!update_lock&&!mirror_lock&&!fast_lock;

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    if(!update_lock&&!mirror_lock&&!fast_lock){ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));update_usb_ready=true;}
    else event_log("Test19-Journal offen: USB bleibt bis bestaetigter Recovery gesperrt");
    display_system_initialized=true;
    ESP_LOGI(TAG, "USB MSC bereit (nur Lesen)");
    state_lock = xSemaphoreCreateMutex();
    command_queue = xQueueCreate(8, sizeof(command_t));
    ESP_ERROR_CHECK(state_lock && command_queue ? ESP_OK : ESP_ERR_NO_MEM);
    xTaskCreate(usb_test_task, "usb_test", 24576, NULL, 5, NULL);
    xTaskCreate(wifi_task, "wifi_start", 6144, NULL, 4, NULL);
}
