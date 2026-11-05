#include "dav_tree.h"
#include "dav_memory.h"
#include "dav_store.h"
#include <string.h>
#define BUCKETS 4096
struct dav_intern { struct dav_intern *next; char text[]; };
// Conservative per-allocation accounting, including alignment/header allowance.
static size_t charge(size_t bytes){return ((bytes+7)&~(size_t)7)+8;}
static size_t hash_path(const char *s,size_t len)
{
 uint32_t hash=2166136261u;
 for(size_t i=0;i<len;i++)hash=(hash^(unsigned char)s[i])*16777619u;
 return hash%BUCKETS;
}
#include "sd_index_impl.inc"
void dav_tree_clear(dav_tree_t *t)
{
 if(t->disk){sd_close_tree(t);return;}
 if(t->strings) {
  for(size_t i=0;!t->store&&i<BUCKETS;i++){struct dav_intern *p=t->strings[i];while(p){struct dav_intern *next=p->next;dav_tree_release(p);p=next;}}
  dav_tree_release(t->strings);
 }
 for(unsigned s=0;s<2;s++){
  dav_tree_entry_t *e=t->head[s];while(e){dav_tree_entry_t *next=e->next;dav_tree_release(e);e=next;}
  dav_tree_release(t->buckets[s]);
 }
 dav_store_close(t->store);
 memset(t,0,sizeof(*t));
}
// Gather all new strings of one entry into a single bounded flash record.
// Publish hash-bucket pointers only after write/readback verification succeeds.
static bool store_strings(dav_tree_t *t,const char *strings[7],char *interned[7])
{
 union {void *alignment;unsigned char bytes[DAV_STORE_BLOCK];} record;
 memset(&record,0,sizeof(record));size_t used=0,offsets[7],buckets[7],count=0;
 for(size_t i=0;i<7;i++){
  size_t b=hash_path(strings[i],strlen(strings[i]));struct dav_intern *found=t->strings[b];
  while(found&&strcmp(found->text,strings[i]))found=found->next;
  if(found){interned[i]=found->text;continue;}
  bool pending=false;
  for(size_t j=0;j<i;j++)if(!strcmp(strings[j],strings[i])){interned[i]=interned[j];pending=true;break;}
  if(pending)continue;
  size_t bytes=(sizeof(struct dav_intern)+strlen(strings[i])+1+7)&~(size_t)7;
  if(bytes>sizeof(record.bytes)-used)return false;
  struct dav_intern *node=(void *)(record.bytes+used);
  node->next=t->strings[b];
  for(size_t j=0;j<count;j++)if(buckets[j]==b)node->next=(void *)(t->store->ops.mapped+t->store->used+offsets[j]);
  strcpy(node->text,strings[i]);
  interned[i]=(char *)(t->store->ops.mapped+t->store->used+used+offsetof(struct dav_intern,text));
  offsets[count]=used;buckets[count++]=b;used+=bytes;
 }
 if(!used)return true;
 const unsigned char *mapped=dav_store_append(t->store,record.bytes,used);if(!mapped)return false;
 for(size_t i=0;i<count;i++)t->strings[buckets[i]]=(void *)(mapped+offsets[i]);
 return true;
}
dav_error_t dav_tree_add(dav_tree_t *t,unsigned side,const char *path,const dav_entry_t *v,uint32_t (*upper)(uint32_t))
{
 if(t->disk)return sd_add(t,side,path,v,upper);
 if(side>1)return DAV_ERR_PATH;
 if(t->store&&(t->store->failed||t->store->sealed))return DAV_ERR_STORE;
 if(!t->uppercase)t->uppercase=upper;
 char key[DAV_PATH_LIMIT],fat[DAV_PATH_LIMIT];bool invalid=false;
 if(!dav_normalize_path(path,key,fat,sizeof(key),upper,&invalid))return DAV_ERR_PATH;
 size_t bucket=hash_path(fat,strlen(fat));
 if(t->buckets[side])for(dav_tree_entry_t *e=t->buckets[side][bucket];e;e=e->hash_next)
  if(!strcmp(e->path,path))return DAV_ERR_DUPLICATE;
 const char *strings[]={path,key,fat,v->etag,v->modified,v->created,v->content_type};
 if(!t->budget)t->budget=DAV_TREE_BUDGET;
 size_t index_bytes=BUCKETS*sizeof(dav_tree_entry_t *);
 if(!t->strings) {
  if(charge(index_bytes)>t->budget-t->used)return DAV_ERR_ENTRY_BUDGET;
  t->strings=dav_tree_allocate(index_bytes);if(!t->strings)return DAV_ERR_MEMORY;
  t->used+=charge(index_bytes);
 }
 if(!t->buckets[side]) {
  if(charge(index_bytes)>t->budget-t->used)return DAV_ERR_ENTRY_BUDGET;
  t->buckets[side]=dav_tree_allocate(index_bytes);if(!t->buckets[side])return DAV_ERR_MEMORY;
  t->used+=charge(index_bytes);
 }
 char *interned[7];
 if(t->store){if(!store_strings(t,strings,interned))return DAV_ERR_STORE;}
 else for(size_t i=0;i<7;i++) {
  size_t b=hash_path(strings[i],strlen(strings[i]));struct dav_intern *found=t->strings[b];
  while(found&&strcmp(found->text,strings[i]))found=found->next;
  if(!found) {
   size_t bytes=sizeof(*found)+strlen(strings[i])+1;
   if(charge(bytes)>t->budget-t->used)return DAV_ERR_ENTRY_BUDGET;
   found=dav_tree_allocate(bytes);if(!found)return DAV_ERR_MEMORY;
   found->next=t->strings[b];t->strings[b]=found;strcpy(found->text,strings[i]);t->used+=charge(bytes);
  }
  interned[i]=found->text;
 }
 size_t bytes=sizeof(dav_tree_entry_t);
 bytes+=DAV_SYNC_META_BYTES;
 if(charge(bytes)>t->budget-t->used)return DAV_ERR_ENTRY_BUDGET;
 dav_tree_entry_t *e=dav_tree_allocate(bytes);if(!e)return DAV_ERR_MEMORY;
 e->path=interned[0];e->key=interned[1];e->fat=interned[2];e->etag=interned[3];
 e->modified=interned[4];e->created=interned[5];e->content_type=interned[6];
 e->kind=v->kind;e->size=v->size;e->size_known=v->size_known;e->invalid_fat=invalid;
 bool directory_collision=false;
 for(dav_tree_entry_t *other=t->buckets[side][bucket];other;other=other->hash_next)if(!strcmp(other->fat,e->fat)) {
  e->conflict=other->conflict=true;
  if(e->kind==DAV_DIRECTORY||other->kind==DAV_DIRECTORY)directory_collision=true;
 }
 // Ancestor lookup uses prefix hashes, avoiding a full-tree scan per insertion.
 for(size_t len=0;e->fat[len];len++)if(e->fat[len]=='/') {
  size_t b=hash_path(e->fat,len);
  for(const dav_tree_entry_t *p=t->buckets[side][b];p;p=p->hash_next)
   if(p->conflict&&p->kind==DAV_DIRECTORY&&strlen(p->fat)==len&&!memcmp(p->fat,e->fat,len))e->conflict=true;
 }
 e->hash_next=t->buckets[side][bucket];t->buckets[side][bucket]=e;
 if(t->tail[side])t->tail[side]->next=e;else t->head[side]=e;t->tail[side]=e;
 // Rare late directory collisions invalidate existing descendants too.
 if(directory_collision) {
  size_t len=strlen(e->fat);
  for(dav_tree_entry_t *child=t->head[side];child;child=child->next)
   if(len&&!strncmp(e->fat,child->fat,len)&&child->fat[len]=='/')child->conflict=true;
 }
 t->used+=charge(bytes);t->count[side]++;
 if(v->kind==DAV_FILE)t->files[side]++;
 if(v->kind==DAV_DIRECTORY)t->directories[side]++;
 return DAV_OK;
}
const dav_tree_entry_t *dav_tree_find_exact(const dav_tree_t *t,unsigned side,const char *path)
{
 if(t->disk){
  char key[DAV_PATH_LIMIT],fat[DAV_PATH_LIMIT];bool invalid,ambiguous;
  if(side>1||!dav_normalize_path(path,key,fat,sizeof(key),t->uppercase,&invalid))return NULL;
  dav_tree_entry_t entry={.fat=fat};return sd_match(t,side,&entry,&ambiguous,true,path);
 }
 if(side>1)return NULL;
 if(!t->buckets[side]||!t->uppercase){
  for(const dav_tree_entry_t *e=t->head[side];e;e=e->next)if(!strcmp(e->path,path))return e;
  return NULL;
 }
 char key[DAV_PATH_LIMIT],fat[DAV_PATH_LIMIT];bool invalid;
 if(!dav_normalize_path(path,key,fat,sizeof(key),t->uppercase,&invalid))return NULL;
 size_t bucket=hash_path(fat,strlen(fat));
 for(const dav_tree_entry_t *e=t->buckets[side][bucket];e;e=e->hash_next)if(!strcmp(e->path,path))return e;
 return NULL;
}
const dav_tree_entry_t *dav_tree_match(const dav_tree_t *t,unsigned other,const dav_tree_entry_t *entry,bool *ambiguous)
{
 if(t->disk)return sd_match(t,other,entry,ambiguous,false,NULL);
 const dav_tree_entry_t *found=NULL;*ambiguous=entry->conflict;
 if(!t->buckets[other])return NULL;
 size_t bucket=hash_path(entry->fat,strlen(entry->fat));
 for(const dav_tree_entry_t *e=t->buckets[other][bucket];e;e=e->hash_next)if(!strcmp(e->fat,entry->fat)){
  if(found||e->conflict)*ambiguous=true;
  if(!found)found=e;
 }
 return found;
}
// A file/ambiguous directory at any ancestor blocks a later target operation.
bool dav_tree_ancestor_conflict(const dav_tree_t *t,const dav_tree_entry_t *entry)
{
 if(t->disk){
  struct sd_index *d=t->disk;
  for(size_t len=0;entry->fat[len];len++)if(entry->fat[len]=='/'){
   uint32_t b=sd_hash(entry->fat,len);
   for(unsigned side=0;side<2;side++)for(uint32_t id=d->buckets[side*SD_BUCKETS+b];id;){
    sd_slot_t *slot=sd_get(d,id);if(!slot)return true;dav_tree_entry_t *e=(void*)slot->memory.bytes;id=slot->hash_next;
    if(strlen(e->fat)==len&&!memcmp(e->fat,entry->fat,len)&&(e->kind!=DAV_DIRECTORY||e->conflict||e->invalid_fat))return true;
   }
  }
  return d->failed;
 }
 for(size_t len=0;entry->fat[len];len++)if(entry->fat[len]=='/') {
  size_t bucket=hash_path(entry->fat,len);
  for(unsigned side=0;side<2;side++)if(t->buckets[side])
   for(const dav_tree_entry_t *e=t->buckets[side][bucket];e;e=e->hash_next)
    if(strlen(e->fat)==len&&!memcmp(e->fat,entry->fat,len)&&
       (e->kind!=DAV_DIRECTORY||e->conflict||e->invalid_fat))return true;
 }
 return false;
}

dav_tree_entry_t *dav_tree_first(const dav_tree_t *t,unsigned side)
{
 if(side>1)return NULL;
 if(t->disk)return (dav_tree_entry_t *)dav_entry_hold(sd_entry(t->disk,t->disk->heads[side]));
 return t->head[side];
}
dav_tree_entry_t *dav_tree_next(const dav_tree_t *t,const dav_tree_entry_t *e)
{
 if(!e)return NULL;
 if(t->disk){
  struct sd_index *d=t->disk;sd_slot_t *slot=sd_locate(e,NULL);if(!slot){sd_bad(d);return NULL;}
  unsigned side=slot->side;uint32_t next=slot->id+slot->bytes;dav_entry_release(e);
  while(next<=d->tails[side]){sd_slot_t *candidate=sd_get(d,next);if(!candidate)return NULL;if(candidate->side==side)return (dav_tree_entry_t *)dav_entry_hold((void *)candidate->memory.bytes);next+=candidate->bytes;}
  return NULL;
 }
 return e->next;
}
bool dav_tree_healthy(const dav_tree_t *t){return sd_index_healthy(t);}

bool dav_tree_ordered(const dav_tree_t *t,unsigned side){return t->disk&&side<2&&t->disk->ordered[side];}
