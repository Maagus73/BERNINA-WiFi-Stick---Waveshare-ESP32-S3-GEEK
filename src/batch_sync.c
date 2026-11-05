#include "batch_sync.h"
#include <string.h>
bool batch_strong_etag(const char *s)
{
 if(!s||s[0]!='"')return false;
 size_t n=strlen(s);if(n<2||n>=128||s[n-1]!='"')return false;
 for(size_t i=1;i+1<n;i++)if((unsigned char)s[i]<0x21||s[i]=='"'||s[i]==127)return false;
 return true;
}
static const dav_tree_entry_t *ancestor(const dav_tree_t *t,const char *path,size_t len)
{
 char relative[DAV_PATH_LIMIT];if(len>=sizeof(relative))return NULL;memcpy(relative,path,len);relative[len]=0;return dav_tree_find_exact(t,0,relative);
}
static bool directories(const dav_tree_t *t,const dav_tree_entry_t *f,batch_plan_t *p,bool add)
{
 for(size_t len=0;;len++){
  if(len&&f->path[len]!='/'){if(!f->path[len])break;continue;}
  const dav_tree_entry_t *d DAV_AUTO=dav_entry_hold(ancestor(t,f->path,len));bool ambiguous=false;
  if(!d||d->kind!=DAV_DIRECTORY||d->invalid_fat||d->conflict)return false;
  const dav_tree_entry_t *sd DAV_AUTO=dav_entry_hold(dav_tree_match(t,1,d,&ambiguous));
  if(ambiguous||(sd&&(sd->kind!=DAV_DIRECTORY||sd->invalid_fat||strcmp(sd->path,d->path))))return false;
  unsigned i;for(i=0;i<p->directory_count;i++)if(p->dirs[i]==d)break;
  if(add&&i==p->directory_count){if(i==BATCH_DIRS)return false;p->dirs[i]=dav_entry_hold(d);p->existed[i]=sd!=NULL;p->directory_count++;if(!sd)p->new_directories++;}
 }
 return true;
}
static bool consider(const dav_tree_t *t,const dav_tree_entry_t *e,uint64_t limit,batch_plan_t *p)
{
 if(e->kind!=DAV_FILE)return true;
 bool ambiguous=false;const dav_tree_entry_t *sd DAV_AUTO=dav_entry_hold(dav_tree_match(t,1,e,&ambiguous));
 dry_category_t kind=dry_classify(t,e,sd,ambiguous);
 if(kind==DRY_CONFLICT){p->conflicts++;return true;}
 if(kind!=DRY_NEW)return true;
 p->new_files++;if(!e->size_known){p->unknown_sizes++;return true;}
 if(!strncmp(e->fat,".B16-",5)||!strncmp(e->fat,".B19-",5)){p->reserved++;return true;}
 if(!batch_strong_etag(e->etag)){p->unversioned++;return true;}
 if(!directories(t,e,p,false)){p->conflicts++;return true;}
 if(e->size>limit-p->bytes){p->oversized++;return true;}
 if(p->count==BATCH_FILES)return true;
 if(!directories(t,e,p,true)){p->valid=false;p->directory_limit=true;return false;}
 p->files[p->count++]=dav_entry_hold(e);p->bytes+=e->size;return true;
}
void batch_plan_build(const dav_tree_t *t,bool complete,uint64_t limit,batch_plan_t *p)
{
 memset(p,0,sizeof(*p));if(!complete)return;
 if(dav_tree_ordered(t,0)){
  for(const dav_tree_entry_t *e DAV_AUTO=dav_tree_first(t,0);e;e=dav_tree_next(t,e))if(!consider(t,e,limit,p))return;
 }else{
  char after[DAV_PATH_LIMIT]={0};
  for(;;){
   const dav_tree_entry_t *e DAV_AUTO=NULL;
   for(const dav_tree_entry_t *n DAV_AUTO=dav_tree_first(t,0);n;n=dav_tree_next(t,n))
    if(n->kind==DAV_FILE&&(!*after||strcmp(n->path,after)>0)&&(!e||strcmp(n->path,e->path)<0))dav_entry_assign(&e,n);
   if(!e)break;
   strcpy(after,e->path);if(!consider(t,e,limit,p))return;
  }
 }
 p->valid=p->count>0&&dav_tree_healthy(t);
}

void batch_plan_release(batch_plan_t *p){for(unsigned i=0;i<p->count;i++)dav_entry_release(p->files[i]);for(unsigned i=0;i<p->directory_count;i++)dav_entry_release(p->dirs[i]);memset(p,0,sizeof(*p));}
