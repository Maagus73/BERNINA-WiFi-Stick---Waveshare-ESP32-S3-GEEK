#include "mirror_sync.h"
#include "batch_sync.h"
#include "sd_index.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#ifdef ESP_PLATFORM
#define lstat stat
#endif
mirror_meta_t *mirror_meta(const dav_tree_entry_t *e){return (mirror_meta_t *)e->data;}
uint32_t mirror_ordinal(const dav_tree_entry_t *e){uint32_t n;memcpy(&n,mirror_meta(e)->ordinal,4);return n;}
const dav_tree_entry_t *mirror_find(const dav_tree_t *t,unsigned side,const char *path)
{
 return dav_tree_find_exact(t,side,path);
}
static bool reserved(const char *fat){return !strncmp(fat,".B16-",5)||!strncmp(fat,".B19-",5)||!strncmp(fat,".B20-",5);}
static void add(mirror_plan_t *p,uint64_t *sum,uint64_t n){if(UINT64_MAX-*sum<n)p->complete=false;else *sum+=n;}
static const dav_tree_entry_t *next_entry(const dav_tree_t *t,unsigned side,const char *after,bool reverse)
{
 const dav_tree_entry_t *best DAV_AUTO=NULL;
 for(const dav_tree_entry_t *e DAV_AUTO=dav_tree_first(t,side);e;e=dav_tree_next(t,e))if(*e->path&&(!after||(reverse?strcmp(e->path,after)<0:strcmp(e->path,after)>0))&&
  (!best||(reverse?strcmp(e->path,best->path)>0:strcmp(e->path,best->path)<0)))dav_entry_assign(&best,e);
 return best;
}
void mirror_plan_build(dav_tree_t *t,bool complete,uint64_t free_bytes,uint32_t nonce,mirror_plan_t *p)
{
 memset(p,0,sizeof(*p));p->complete=complete;p->free_bytes=free_bytes;p->nonce=nonce;
 for(unsigned side=0;side<2;side++)for(const dav_tree_entry_t *e DAV_AUTO=dav_tree_first(t,side);e;e=dav_tree_next(t,e))memset(mirror_meta(e),0,sizeof(mirror_meta_t));
 if(!complete)return;
 uint64_t largest_old=0;uint32_t ordinal=0;
 for(const dav_tree_entry_t *n DAV_AUTO=dav_tree_first(t,0);n;n=dav_tree_next(t,n)){
  if(!*n->path)continue;
  bool ambiguous=false;const dav_tree_entry_t *s DAV_AUTO=dav_entry_hold(dav_tree_match(t,1,n,&ambiguous));
  dry_category_t c=dry_classify(t,n,s,ambiguous);
  if(c==DRY_CONFLICT||(s&&strcmp(n->path,s->path))||reserved(n->fat)){p->conflicts++;continue;}
  if(n->kind==DAV_DIRECTORY){if(!s)p->create_dirs++;continue;}
  if(n->kind!=DAV_FILE||!n->size_known||n->size>UPDATE_LIMIT||!batch_strong_etag(n->etag)||(s&&(!s->size_known||s->size>UPDATE_LIMIT))){p->conflicts++;continue;}
  ordinal++;memcpy(mirror_meta(n)->ordinal,&ordinal,4);p->files++;
  add(p,&p->download,n->size);add(p,&p->required,n->size);
  if(!s)p->new_files++;
  else{
   if(c==DRY_CHANGED)p->changed_files++;else p->unknown_files++;
   add(p,&p->required,s->size);add(p,&p->generated_max,3);if(s->size>largest_old)largest_old=s->size;
  }
 }
 for(const dav_tree_entry_t *s DAV_AUTO=dav_tree_first(t,1);s;s=dav_tree_next(t,s))if(*s->path){
  bool ambiguous=false;const dav_tree_entry_t *n DAV_AUTO=dav_entry_hold(dav_tree_match(t,0,s,&ambiguous));
  if(dry_classify(t,n,s,ambiguous)==DRY_CONFLICT){p->conflicts++;continue;}
  if(!n){if(s->kind==DAV_FILE){p->delete_files++;if(reserved(s->fat))p->internal_files++;if(!s->size_known)p->conflicts++;}else if(s->kind==DAV_DIRECTORY)p->remove_dirs++;else p->conflicts++;}
 }
 p->empty_source=t->count[0]==1&&t->head[0]&&!*t->head[0]->path&&t->head[0]->kind==DAV_DIRECTORY;
 uint64_t deletions=p->delete_files+p->remove_dirs,sd=t->count[1]?t->count[1]-1:0;
 p->dangerous=p->empty_source||deletions>=20||(deletions&&deletions>sd/2);
 add(p,&p->required,largest_old);add(p,&p->required,UPDATE_RESERVE);
 uint64_t allocations=p->new_files+p->generated_max+p->create_dirs+4;
 if(allocations>UINT64_MAX/65536)p->complete=false;else add(p,&p->required,allocations*65536);
 p->complete=p->complete&&dav_tree_healthy(t);
 p->valid=p->complete&&!p->conflicts&&p->required<=free_bytes;
}
static bool join(char *out,size_t cap,const char *base,const char *path){int n=snprintf(out,cap,"%s/%s",base,path);return n>=0&&(size_t)n<cap;}
bool mirror_hash_file(const char *base,const char *relative,uint64_t size,const single_copy_ops_t *o,unsigned char hash[32])
{
 if(!single_copy_path_state(base,relative,2,true,o))return false;
 char path[DAV_PATH_LIMIT+64];if(!join(path,sizeof(path),base,relative)||!o->owned(o->context))return false;
 int fd=open(path,O_RDONLY);if(fd<0)return false;unsigned char block[1024];uint64_t got=0;bool ok=o->hash_start(o->context);
 while(ok){
  if(o->cooperate)o->cooperate(o->context);
  if(!o->owned(o->context)){ok=false;break;}
  ssize_t n=read(fd,block,sizeof(block));if(n<0){ok=false;break;}if(!n)break;
  if((uint64_t)n>size-got||!o->hash_update(o->context,block,(size_t)n)){ok=false;break;}got+=(uint64_t)n;
 }
 if(!o->owned(o->context))return false; // keep handle quarantined, never close after ownership loss
 if(close(fd))ok=false;
 return ok&&got==size&&o->hash_finish(o->context,hash);
}
static bool hash_matches(const char *base,const char *path,uint64_t size,const unsigned char *expected,const single_copy_ops_t *o)
{
 unsigned char hash[32];return mirror_hash_file(base,path,size,o,hash)&&!memcmp(hash,expected,32);
}
void mirror_source_reset(dav_tree_t *t)
{
 for(const dav_tree_entry_t *n DAV_AUTO=dav_tree_first(t,0);n;n=dav_tree_next(t,n))mirror_meta(n)->seen=!*n->path;
}
bool mirror_source_entry(dav_tree_t *t,const char *parent,const dav_entry_t *e)
{
 char path[DAV_PATH_LIMIT];int n=snprintf(path,sizeof(path),"%s%s%s",parent,*parent?"/":"",e->name);if(n<0||(size_t)n>=sizeof(path))return false;
 const dav_tree_entry_t *known DAV_AUTO=dav_entry_hold(mirror_find(t,0,path));
 if(!known||mirror_meta(known)->seen||known->kind!=e->kind||known->size_known!=e->size_known||
  (e->size_known&&known->size!=e->size)||strcmp(known->etag,e->etag)||strcmp(known->modified,e->modified))return false;
 mirror_meta(known)->seen=true;return true;
}
bool mirror_source_complete(const dav_tree_t *t)
{
 for(const dav_tree_entry_t *n DAV_AUTO=dav_tree_first(t,0);n;n=dav_tree_next(t,n))if(!mirror_meta(n)->seen)return false;
 return true;
}
static const dav_tree_entry_t *generated_owner(const dav_tree_t *t,const mirror_plan_t *p,const char *name,const char **extension)
{
 if(strncmp(name,".B19-",5)||strlen(name)<17||name[13]!='.')return NULL;
 for(unsigned i=5;i<13;i++)if(!((name[i]>='0'&&name[i]<='9')||(name[i]>='a'&&name[i]<='f')||(name[i]>='A'&&name[i]<='F')))return NULL;
 char hex[9];memcpy(hex,name+5,8);hex[8]=0;uint32_t ordinal=(uint32_t)strtoul(hex,NULL,16)-p->nonce;
 if(!ordinal||ordinal>p->files)return NULL;
 const char *ext=name+14;if(strcmp(ext,"BAK")&&strcmp(ext,"OLD")&&strcmp(ext,"NEW")&&strcmp(ext,"JRN"))return NULL;
 for(const dav_tree_entry_t *n DAV_AUTO=dav_tree_first(t,0);n;n=dav_tree_next(t,n))if(n->kind==DAV_FILE&&mirror_ordinal(n)==ordinal&&mirror_meta(n)->done){
  bool ambiguous=false;if(!dav_tree_match(t,1,n,&ambiguous)||ambiguous)return NULL;
  *extension=ext;return n;
 }
 return NULL;
}
static bool generated_hash(const char *base,const dav_tree_t *t,const mirror_plan_t *p,const char *name,const single_copy_ops_t *o,uint64_t *size,unsigned char expected[32])
{
 const char *ext=NULL;const dav_tree_entry_t *n DAV_AUTO=dav_entry_hold(generated_owner(t,p,name,&ext));if(!n)return false;
 const dav_tree_entry_t *s DAV_AUTO=dav_entry_hold(mirror_find(t,1,n->path));if(!s)return false;
 if(!strcmp(ext,"JRN")){
  char path[DAV_PATH_LIMIT+64];if(!single_copy_path_state(base,name,2,true,o)||!join(path,sizeof(path),base,name)||!o->owned(o->context))return false;
  update_record_t r;int fd=open(path,O_RDONLY);if(fd<0)return false;
  ssize_t got=read(fd,&r,sizeof(r));unsigned char extra;ssize_t more=read(fd,&extra,1);
  if(!o->owned(o->context))return false;
  bool closed=close(fd)==0;
  if(!closed||got!=(ssize_t)sizeof(r)||more!=0||!update_record_valid(&r,o)||r.nonce!=p->nonce+mirror_ordinal(n)||strcmp(r.target,n->path)||
   r.old_size!=s->size||r.new_size!=n->size||memcmp(r.old_hash,mirror_meta(s)->hash,32)||memcmp(r.new_hash,mirror_meta(n)->hash,32))return false;
  *size=sizeof(r);return o->hash_start(o->context)&&o->hash_update(o->context,(const unsigned char *)&r,sizeof(r))&&o->hash_finish(o->context,expected);
 }
 *size=!strcmp(ext,"NEW")?n->size:s->size;memcpy(expected,!strcmp(ext,"NEW")?mirror_meta(n)->hash:mirror_meta(s)->hash,32);return true;
}
// Stream each known directory; unexpected entries or FAT spellings fail closed.
static bool live_tree(const char *base,dav_tree_t *t,const mirror_plan_t *p,const single_copy_ops_t *o,bool after,bool exact,bool capture)
{
 for(unsigned side=0;side<2;side++)for(const dav_tree_entry_t *e DAV_AUTO=dav_tree_first(t,side);e;e=dav_tree_next(t,e))mirror_meta(e)->seen=!*e->path;
 for(unsigned side=0;side<2;side++)for(const dav_tree_entry_t *d DAV_AUTO=dav_tree_first(t,side);d;d=dav_tree_next(t,d)){
  if(d->kind!=DAV_DIRECTORY)continue;
  const dav_tree_entry_t *other DAV_AUTO=dav_entry_hold(mirror_find(t,1-side,d->path));
  if(side==0&&(!after||(!other&&!mirror_meta(d)->created)))continue;
  if(side==1&&after&&other)continue;
  if(side==1&&mirror_meta(d)->done)continue;
  if(*d->path&&!single_copy_path_state(base,d->path,1,true,o))return false;
  char absolute[DAV_PATH_LIMIT+64];if(!join(absolute,sizeof(absolute),base,d->path)||!o->owned(o->context))return false;
  DIR *dir=opendir(absolute);if(!dir)return false;bool ok=true;
  for(;;){
   if(o->cooperate)o->cooperate(o->context);
   if(!o->owned(o->context)){ok=false;break;}
   errno=0;struct dirent *v=readdir(dir);if(!v){if(errno)ok=false;break;}
   if(!strcmp(v->d_name,".")||!strcmp(v->d_name,".."))continue;
   if(t->disk&&!*d->path&&sd_index_reserved(v->d_name))continue;
   if(t->ignore_path){char ignored[DAV_PATH_LIMIT];int n=snprintf(ignored,sizeof(ignored),"%s%s%s",d->path,*d->path?"/":"",v->d_name);struct stat st;char full[DAV_PATH_LIMIT+64];if(n<0||(size_t)n>=sizeof(ignored)||!join(full,sizeof(full),base,ignored)||lstat(full,&st)){ok=false;break;}if(t->ignore_path(ignored,S_ISDIR(st.st_mode)))continue;}
   char rel[DAV_PATH_LIMIT];int len=snprintf(rel,sizeof(rel),"%s%s%s",d->path,*d->path?"/":"",v->d_name);
   if(len<0||(size_t)len>=sizeof(rel)){ok=false;break;}
   const dav_tree_entry_t *n DAV_AUTO=dav_entry_hold(mirror_find(t,0,rel));
   const dav_tree_entry_t *s DAV_AUTO=dav_entry_hold(mirror_find(t,1,rel));
   const dav_tree_entry_t *expected=NULL;
   if(after&&n)expected=n;else if(s&&!mirror_meta(s)->done)expected=s;
   if(!expected){
    uint64_t size;unsigned char hash[32];
    if(!after||exact||*d->path||!generated_hash(base,t,p,rel,o,&size,hash)||!hash_matches(base,rel,size,hash,o)){ok=false;break;}
    continue;
   }
   mirror_meta(expected)->seen=true;if(n&&expected==s)mirror_meta(n)->seen=true;
   if(expected->kind==DAV_DIRECTORY){if(!single_copy_path_state(base,rel,1,true,o)){ok=false;break;}}
   else if(expected->kind==DAV_FILE){
    mirror_meta_t *m=mirror_meta(expected);
    if(capture){if(!mirror_hash_file(base,rel,expected->size,o,m->hash)){ok=false;break;}m->hashed=true;}
    else if(!m->hashed||!hash_matches(base,rel,expected->size,m->hash,o)){ok=false;break;}
   }else {ok=false;break;}
  }
  if(closedir(dir))ok=false;
  if(!ok)return false;
 }
 for(const dav_tree_entry_t *s DAV_AUTO=dav_tree_first(t,1);s;s=dav_tree_next(t,s))if(*s->path&&!mirror_meta(s)->done&&(!after||!mirror_find(t,0,s->path))){if(exact||!mirror_meta(s)->seen)return false;}
 if(after)for(const dav_tree_entry_t *n DAV_AUTO=dav_tree_first(t,0);n;n=dav_tree_next(t,n))if(!mirror_meta(n)->seen)return false;
 return dav_tree_healthy(t);
}
bool mirror_capture(const char *base,dav_tree_t *t,const mirror_plan_t *p,const single_copy_ops_t *o)
{
 if(!p->valid||!live_tree(base,t,p,o,false,false,true))return false;
 for(const dav_tree_entry_t *n DAV_AUTO=dav_tree_first(t,0);n;n=dav_tree_next(t,n))if(n->kind==DAV_FILE&&mirror_find(t,1,n->path)){
  update_record_t r={0};update_names(&r,p->nonce+mirror_ordinal(n));
  const char *names[]={r.stage,r.backup,r.displaced,r.journal,r.restore};
  for(unsigned i=0;i<5;i++)if(!single_copy_path_state(base,names[i],0,true,o))return false;
 }
 return dav_tree_healthy(t);
}
static bool remove_one(const char *base,const char *relative,bool directory,uint64_t size,const unsigned char hash[32],const mirror_ops_t *ops)
{
 const single_copy_ops_t *o=&ops->io;
 if(!ops->absent(o->context,relative,directory)||!single_copy_path_state(base,relative,directory?1:2,true,o))return false;
 if(!directory&&!hash_matches(base,relative,size,hash,o))return false;
 if(!ops->journal(o->context,MIRROR_PHASE_DELETE,relative,directory,hash,size))return false;
 if(ops->cut&&!ops->cut(o->context,1))return false;
 char path[DAV_PATH_LIMIT+64];if(!join(path,sizeof(path),base,relative)||!o->owned(o->context))return false;
 if(directory?rmdir(path)!=0:unlink(path)!=0)return false;
 if(ops->cut&&!ops->cut(o->context,2))return false;
 struct stat st;errno=0;return lstat(path,&st)!=0&&errno==ENOENT;
}
void mirror_run(const char *base,dav_tree_t *t,const mirror_plan_t *p,const mirror_ops_t *ops,mirror_result_t *r)
{
 memset(r,0,sizeof(*r));r->state=MIRROR_FAILED;r->error="Plan/NAS/APP/Zielzustand ungueltig; keine Loeschfreigabe";
 const single_copy_ops_t *o=&ops->io;
#define PHASE(text) do{if(ops->phase)ops->phase(o->context,text);}while(0)
#define REQUIRE(c,msg) do{if(!dav_tree_healthy(t)||!(c)){r->error=(msg);goto done;}}while(0)
 REQUIRE(p->valid&&p->complete&&!p->conflicts&&o->owned(o->context),"Plan oder APP ungueltig");
 REQUIRE(!p->delete_allowed||!p->dangerous||p->danger_confirmed,"Zusatzbestaetigung fuer leeres NAS/viele Loeschungen fehlt");
 PHASE("NAS-Generation vor Ausfuehrung pruefen");
 REQUIRE(ops->source(o->context,t),"NAS-Baum unvollstaendig oder seit Vorschau veraendert");
 PHASE("SD-Zustand vor Ausfuehrung pruefen");
 REQUIRE(live_tree(base,t,p,o,false,false,false),"SD seit Vorschau veraendert; neu planen");
 REQUIRE(o->space(o->context,p->required),"Freier Speicher fuer Transfer, Backups und Recovery reicht nicht");
 REQUIRE(ops->journal(o->context,MIRROR_PHASE_COPY,"",false,NULL,0),"Persistentes Sync-Journal nicht bestaetigt");r->armed=true;
 PHASE("Fehlende Verzeichnisse vorbereiten");
 // The existing breadth-first listing stores each parent before its children.
 for(const dav_tree_entry_t *n DAV_AUTO=dav_tree_first(t,0);n;n=dav_tree_next(t,n)){
  if(!*n->path)continue;
  if(o->cooperate)o->cooperate(o->context);
  if(n->kind!=DAV_DIRECTORY)continue;
  if(mirror_find(t,1,n->path))continue;
  REQUIRE(single_copy_path_state(base,n->path,0,false,o),"Ordnerziel seit Plan veraendert");
  char path[DAV_PATH_LIMIT+64];REQUIRE(join(path,sizeof(path),base,n->path)&&o->owned(o->context)&&mkdir(path,0777)==0,"NAS-Unterordner konnte nicht sicher angelegt werden");mirror_meta(n)->created=true;
 }
 PHASE("Dateien uebertragen und verifizieren");
 uint64_t bytes=0;
 for(const dav_tree_entry_t *n DAV_AUTO=dav_tree_first(t,0);n;n=dav_tree_next(t,n)){
  if(n->kind!=DAV_FILE)continue;
  const dav_tree_entry_t *s DAV_AUTO=dav_entry_hold(mirror_find(t,1,n->path));mirror_meta_t *m=mirror_meta(n);
  if(ops->progress)ops->progress(o->context,r->completed,p->files,bytes);
  REQUIRE(o->owned(o->context),"APP vor Dateioperation verloren");
  if(s)REQUIRE(ops->replace_file(o->context,n,s,p->nonce+mirror_ordinal(n),mirror_meta(s)->hash,m->hash),"Einzeldatei-Update fehlgeschlagen; keine nachfolgenden Loeschungen");
  else REQUIRE(ops->new_file(o->context,n,m->hash),"Neue Datei fehlgeschlagen; keine nachfolgenden Loeschungen");
  m->hashed=m->done=true;r->completed++;bytes+=n->size;
  if(ops->progress)ops->progress(o->context,r->completed,p->files,bytes);
 }
 PHASE("NAS und SD vor Loeschphase vollstaendig pruefen");
 REQUIRE(ops->source(o->context,t),"NAS vor Loeschphase veraendert oder unvollstaendig; keine Loeschungen");
 REQUIRE(live_tree(base,t,p,o,true,false,false),"Transfer-Zielpruefung fehlgeschlagen; keine Loeschungen");
 if(!p->delete_allowed){
  bool extra=p->delete_files||p->remove_dirs;
  for(const dav_tree_entry_t *n DAV_AUTO=dav_tree_first(t,0);n&&!extra;n=dav_tree_next(t,n))if(n->kind==DAV_FILE&&mirror_find(t,1,n->path))extra=true;
  if(extra){REQUIRE(ops->finish(o->context,false),"Transfer fertig, Journalsperre bleibt");r->armed=false;r->state=MIRROR_HELD;r->error="Dateien geprueft uebertragen; Loeschungen gesperrt, noch keine vollstaendige Spiegelung";goto done;}
 }else{
  PHASE("Bestaetigte SD-only-Dateien und Ordner entfernen");
  for(const dav_tree_entry_t *s DAV_AUTO=dav_tree_first(t,1);s;s=dav_tree_next(t,s)){
   if(s->kind!=DAV_FILE||mirror_find(t,0,s->path))continue;
   REQUIRE(remove_one(base,s->path,false,s->size,mirror_meta(s)->hash,ops),"Dateiloeschung verweigert/unterbrochen; neu planen und bestaetigen");mirror_meta(s)->done=true;r->deleted_files++;
  }
  if(t->disk){
   // Deepest directories first, bounded passes rather than one full disk scan per file.
   unsigned deepest=0;
   for(const dav_tree_entry_t *s DAV_AUTO=dav_tree_first(t,1);s;s=dav_tree_next(t,s))if(*s->path&&s->kind==DAV_DIRECTORY){unsigned depth=1;for(const char *c=s->path;*c;c++)if(*c=='/')depth++;if(depth>deepest)deepest=depth;}
   for(unsigned level=deepest;level;level--)for(const dav_tree_entry_t *s DAV_AUTO=dav_tree_first(t,1);s;s=dav_tree_next(t,s)){
    if(!*s->path||s->kind!=DAV_DIRECTORY)continue;
    unsigned depth=1;for(const char *c=s->path;*c;c++)if(*c=='/')depth++;
    if(depth!=level||mirror_find(t,0,s->path))continue;
    REQUIRE(remove_one(base,s->path,true,0,NULL,ops),"Ordnerentfernung verweigert/unterbrochen; keine unbekannten Inhalte entfernen");mirror_meta(s)->done=true;r->deleted_dirs++;
   }
  }else{
   char after[DAV_PATH_LIMIT]={0};
   for(;;){
    const dav_tree_entry_t *s DAV_AUTO=dav_entry_hold(next_entry(t,1,*after?after:NULL,true));if(!s)break;strcpy(after,s->path);
    if(s->kind!=DAV_DIRECTORY||mirror_find(t,0,s->path))continue;
    REQUIRE(remove_one(base,s->path,true,0,NULL,ops),"Ordnerentfernung verweigert/unterbrochen; keine unbekannten Inhalte entfernen");mirror_meta(s)->done=true;r->deleted_dirs++;
   }
  }
  PHASE("Verifizierte eigene Sicherungen bereinigen");
  for(;;){
   REQUIRE(o->owned(o->context),"APP vor Bereinigung verloren");
   char candidate[32]={0};DIR *dir=opendir(base);REQUIRE(dir!=NULL,"Eigene Artefakte nicht pruefbar");bool ok=true;
   for(;;){errno=0;struct dirent *v=readdir(dir);if(!v){if(errno)ok=false;break;}const char *ext; if(generated_owner(t,p,v->d_name,&ext)){
     if(strlen(v->d_name)>=sizeof(candidate)){ok=false;break;}strcpy(candidate,v->d_name);break;
   }}
   if(closedir(dir))ok=false;
   REQUIRE(ok,"Artefaktinventar unvollstaendig");if(!*candidate)break;
   uint64_t size;unsigned char hash[32];REQUIRE(generated_hash(base,t,p,candidate,o,&size,hash),"Eigene Sicherung unklar; keine Bereinigung");
   REQUIRE(remove_one(base,candidate,false,size,hash,ops),"Eigene Sicherung nicht sicher bereinigbar; USB bleibt gesperrt");r->cleaned++;
  }
 }
 PHASE("Abschluss: NAS-Quelle und SD-Spiegel vollstaendig pruefen");
 REQUIRE(ops->source(o->context,t)&&live_tree(base,t,p,o,true,true,false),"Abschlussvergleich nicht identisch; Lauf unvollstaendig");
 REQUIRE(ops->journal(o->context,MIRROR_PHASE_VERIFY,"",false,NULL,0)&&ops->finish(o->context,true),"Spiegel geprueft, persistentes Journal bleibt gesperrt");
 r->armed=false;r->state=MIRROR_COMPLETE;r->error="Vollstaendige manuelle Spiegelung geprueft; APP bleibt aktiv";
done:
#undef REQUIRE
#undef PHASE
 return;
}
