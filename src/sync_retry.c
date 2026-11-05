#include "sync_retry.h"
#include <string.h>
bool sync_retry_config_valid(const sync_retry_config_t *c){if(!c||c->enabled>1)return false;for(unsigned i=0;i<4;i++)if(!c->seconds[i]||c->seconds[i]>86400||(i&&c->seconds[i]<c->seconds[i-1]))return false;return true;}
bool sync_retry_delays_parse(const char *s,uint32_t out[4]){if(!s||!out)return false;uint32_t values[4]={0};for(unsigned i=0;i<4;i++){if(*s<'0'||*s>'9')return false;while(*s>='0'&&*s<='9'){if(values[i]>8640)return false;values[i]=values[i]*10+(unsigned)(*s++-'0');}if(i<3){if(*s++!=',')return false;}else if(*s)return false;}sync_retry_config_t c={.enabled=1};memcpy(c.seconds,values,sizeof(values));if(!sync_retry_config_valid(&c))return false;memcpy(out,values,sizeof(values));return true;}
uint32_t sync_retry_delay(const sync_retry_config_t *c,uint32_t failures){return c->seconds[failures>3?3:failures?failures-1:0];}
sync_failure_t sync_retry_http_class(int status,const char *code){
 if(code&&*code&&strcmp(code,"file_changed")&&strcmp(code,"source_changed_during_scan")&&strcmp(code,"database_error"))return SYNC_FAIL_PERMANENT;
 if(status==0||status<0||status==408||status==409||status==412||status==425||status==429||status==500||status==502||status==503||status==504)return SYNC_FAIL_TRANSIENT;
 return SYNC_FAIL_PERMANENT;
}
