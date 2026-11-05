#define _POSIX_C_SOURCE 200809L
#include "app_settings.h"
bool app_interval_convert(uint32_t value,bool hours,uint32_t *seconds){uint32_t unit=hours?3600:60;if(!value||value>604800/unit||!seconds)return false;*seconds=value*unit;return true;}

#include <string.h>
void app_utf8_tail(char *text){size_t n=strlen(text);if(!n)return;size_t i=n-1;while(i&&(text[i]&0xc0)==0x80)i--;unsigned char c=(unsigned char)text[i];size_t expected=c<128?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:(c&0xf8)==0xf0?4:1;if(n-i<expected)text[i]=0;}

bool app_day_range(time_t now,time_t *start,time_t *end){if(now<1735689600||!start||!end)return false;struct tm local;if(!localtime_r(&now,&local))return false;local.tm_hour=local.tm_min=local.tm_sec=0;local.tm_isdst=-1;*start=mktime(&local);local.tm_mday++;local.tm_isdst=-1;*end=mktime(&local);return *start!=(time_t)-1&&*end>*start&&*end-*start<=90000;}
