#include "remote_plan.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
static bool number(const char *s,uint64_t *out){if(!*s)return false;for(const char *p=s;*p;p++)if(*p<'0'||*p>'9')return false;errno=0;char *end;*out=strtoull(s,&end,10);return !errno&&!*end;}
static bool emit(void *v,const char *path,const dav_entry_t *entry){remote_plan_t *p=v;if(p->action=='F'){p->seen_files++;if(UINT64_MAX-p->seen_bytes<entry->size)return false;p->seen_bytes+=entry->size;}return p->emit(p->context,p->action,path,entry);}
void remote_plan_init(remote_plan_t *p,remote_emit_t cb,void *context){memset(p,0,sizeof(*p));p->emit=cb;p->context=context;nas_index_init(&p->index,emit,p);}
static bool line(remote_plan_t *p){
 if(p->ended)return false;
 if(!p->header){char *parts[7],*at=p->line;for(unsigned i=0;i<7;i++){parts[i]=at;char *tab=strchr(at,'\t');if(i<6){if(!tab)return false;*tab=0;at=tab+1;}else if(tab)return false;}
  if(strcmp(parts[0],"BPLAN2")||strlen(parts[1])!=64||(strcmp(parts[2],"FULL")&&strcmp(parts[2],"DELTA"))||!number(parts[3],&p->files)||!number(parts[4],&p->bytes)||!number(parts[5],&p->operations)||!number(parts[6],&p->differences)||p->operations>100000)return false;
  strcpy(p->generation,parts[1]);p->full=!strcmp(parts[2],"FULL");char head[80];int n=snprintf(head,sizeof(head),"BINDEX1\t%s\n",p->generation);if(!nas_index_feed(&p->index,head,(size_t)n))return false;p->header=true;return true;
 }
 if(!strncmp(p->line,"END\t",4)){size_t n=strlen(p->line);p->line[n]='\n';bool ok=nas_index_feed(&p->index,p->line,n+1)&&nas_index_finish(&p->index);p->ended=ok;return ok;}
 if(!p->line[0]||p->line[1]!='\t'||!strchr("DFVXRK",p->line[0]))return false;
 p->action=p->line[0];if(strchr("XR",p->action)){if(p->phase)return false;}else if(strchr("DK",p->action)){if(p->phase>1)return false;p->phase=1;}else p->phase=2;
 if(p->full&&strchr("XRVK",p->action))return false;
 p->line[0]=strchr("DRK",p->action)?'D':'F';size_t n=strlen(p->line);p->line[n]='\n';return nas_index_feed(&p->index,p->line,n+1);
}
bool remote_plan_feed(remote_plan_t *p,const char *s,size_t n){if(p->failed||!p->emit)return false;for(size_t i=0;i<n;i++){if(s[i]=='\n'){p->line[p->used]=0;if(!line(p)){p->failed=true;return false;}p->used=0;}else{if(!s[i]||p->used+2>=sizeof(p->line)){p->failed=true;return false;}p->line[p->used++]=s[i];}}return true;}
bool remote_plan_finish(remote_plan_t *p){return p->header&&p->ended&&!p->failed&&!p->used&&p->index.count==p->operations&&p->seen_files==p->files&&p->seen_bytes==p->bytes;}
