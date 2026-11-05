#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H
#include "sync_config.h"
#include "app_settings.h"
#include "cJSON.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "sync_retry.h"
#include "recovery_config.h"
#define APP_VERSION "1.0.3"
static uint32_t app_interval_seconds=SYNC_CHECK_INTERVAL_SECONDS;
static uint32_t app_interval_value=SYNC_CHECK_INTERVAL_SECONDS/60;
static bool app_interval_hours;
static uint64_t app_meta_files,app_meta_directories;
static bool app_meta_known;
static uint32_t app_event_sent,app_event_dropped;
static time_t app_event_time[MAX_LOG];
static char app_device[13],app_boot_id[17];
static bool app_archive_busy;
static volatile bool app_control_busy;
static bool app_auto_paused=!SYNC_AUTOMATIC_ENABLED;
static void app_sync_finished(bool);
static void app_events_flush(void);
static void app_config_boot(void);
static void app_identify(void);
static esp_err_t app_api(httpd_req_t *);
static bool perform_app_handoff(void);
static void app_repair_flow(void);
typedef struct {sync_failure_t kind;int http,esp,socket,tls,tls_code,tls_flags;char stage[40],server[65];} sync_diagnostic_t;
static sync_diagnostic_t sync_diagnostic;
static sync_retry_config_t recovery_config={RECOVERY_ENABLED,{RECOVERY_DELAY_1_SECONDS,RECOVERY_DELAY_2_SECONDS,RECOVERY_DELAY_3_SECONDS,RECOVERY_DELAY_MAX_SECONDS}};
static struct {uint32_t version,pending,repair;char source[65],policy[130];} recovery_job;
static uint32_t recovery_failures;
static int64_t recovery_next_us;
static char recovery_error[256];
static bool recovery_executing;
static bool recovery_active(void);
static void recovery_boot(void);
static bool recovery_set_config(const sync_retry_config_t *);
static void recovery_resume(void);
static void recovery_manual_start(void);
static void recovery_clear(void);
static void recovery_failure(void);
static bool recovery_schedule(void);
static void recovery_worker(void);
static bool recovery_policy_allowed(void);
static void sync_diag_reset(void);
static void sync_diag_set(sync_failure_t,const char *);
static void sync_diag_event(esp_http_client_event_t *);
static esp_err_t sync_http_open(esp_http_client_handle_t,int,const char *);
static int64_t sync_http_headers(esp_http_client_handle_t,const char *);
static int sync_http_read(esp_http_client_handle_t,char *,int,const char *);
#endif
