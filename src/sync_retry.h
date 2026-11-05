#ifndef SYNC_RETRY_H
#define SYNC_RETRY_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { SYNC_FAIL_NONE, SYNC_FAIL_TRANSIENT, SYNC_FAIL_PERMANENT, SYNC_FAIL_LOCAL } sync_failure_t;
typedef struct {uint32_t enabled,seconds[4];} sync_retry_config_t;
#define SYNC_RETRY_CONFIG_VERSION 1U
bool sync_retry_config_valid(const sync_retry_config_t *);
bool sync_retry_delays_parse(const char *,uint32_t seconds[4]);
uint32_t sync_retry_delay(const sync_retry_config_t *,uint32_t failures);
sync_failure_t sync_retry_http_class(int status,const char *server_error);
#endif
