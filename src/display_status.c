#include "display_status.h"
display_state_t display_classify(const display_input_t *i)
{
 if(i->fault)return DISPLAY_ERROR;
 if(!i->initialized)return DISPLAY_START;
 if(i->retry_waiting&&!i->busy)return DISPLAY_RETRY;
 if(i->app_confirmed)return i->transferring?DISPLAY_SYNC:DISPLAY_APP;
 if(i->transferring)return DISPLAY_ERROR;
 if(i->usb_confirmed&&i->host_ready&&!i->busy)return DISPLAY_READY;
 return DISPLAY_WAIT;
}
void display_policy_step(display_policy_t *p,const display_input_t *i,uint32_t now,bool enabled,bool resources_ok)
{
 display_state_t next=display_classify(i);
 if(next!=DISPLAY_READY){p->ready_timing=false;p->matrix=false;}
 else{
  if(!p->ready_timing){p->ready_since=now;p->ready_timing=true;}
  p->matrix=enabled&&resources_ok&&(uint32_t)(now-p->ready_since)>=10000;
 }
 p->state=next;
}
int display_percent(const display_input_t *i)
{
 if(!i->total_known||!i->total||i->bytes>i->total)return -1;
 // Overflow-free floor(100 * bytes / total), even for UINT64_MAX inputs.
 uint64_t rest=i->bytes%i->total;unsigned percent=(unsigned)(i->bytes/i->total)*100;
 if(percent==100)return 100;
 // Sum the fractional value 100 times without multiplying uint64_t.
 uint64_t accumulated=0;
 for(unsigned n=0;n<100;n++){
  if(accumulated>=i->total-rest){accumulated-=i->total-rest;++percent;}
  else accumulated+=rest;
 }
 return (int)percent;
}
