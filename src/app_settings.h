#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
bool app_day_range(time_t now,time_t *start,time_t *end);
void app_utf8_tail(char *text);
bool app_interval_convert(uint32_t value,bool hours,uint32_t *seconds);
#endif
