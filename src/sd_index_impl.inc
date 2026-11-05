// Included only by dav_tree.c. Portable file-backed index; no writable raw pointers on disk.
#include "sd_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#define SD_BUCKETS 32768U
#define SD_IO_BYTES 32768U
#define SD_HEADER_BYTES 512U
#define SD_RECORD_MAGIC 0x42494333U
typedef struct {
 uint32_t magic,bytes,next,hash_next,side,flags;
 uint64_t size;
 unsigned char meta[DAV_SYNC_META_BYTES];
 uint16_t length[7];
 uint32_t crc;
} sd_record_t;
typedef struct {
 uint32_t id,next,hash_next,side,bytes,pins,stamp,saved_flags;
 unsigned char saved_meta[DAV_SYNC_META_BYTES];
 union {uint64_t alignment;unsigned char bytes[SD_INDEX_SLOT_BYTES];} memory;
} sd_slot_t;
struct sd_index {
 struct sd_index *registry_next;
 dav_tree_t *tree;
 sd_index_ops_t ops;
 FILE *file;
 sd_slot_t *slots;
 uint32_t *buckets;
 unsigned char *buffer,*io_buffer;
 uint32_t end,clock,heads[2],tails[2];
 bool failed,sealed,ordered[2];
 char path[DAV_PATH_LIMIT+80];
};
static uint32_t sd_hash(const char *path,size_t length){uint32_t value=2166136261U;for(size_t i=0;i<length;i++)value=(value^(unsigned char)path[i])*16777619U;return value%SD_BUCKETS;}
static struct sd_index *sd_registry;
static bool sd_bad(struct sd_index *d){if(!d->failed&&d->ops.fault)d->ops.fault(d->ops.context);d->failed=true;return false;}
static bool sd_owned(struct sd_index *d){if(d->failed)return false;
 return (d->ops.owned&&d->ops.owned(d->ops.context))?true:sd_bad(d);}
static uint32_t sd_crc(const unsigned char *p,size_t n){return dav_store_crc(0xffffffffU,p,n);}
static uint32_t sd_flags(const dav_tree_entry_t *e){return (uint32_t)e->kind|((uint32_t)e->size_known<<4)|((uint32_t)e->scanned<<5)|((uint32_t)e->conflict<<6)|((uint32_t)e->invalid_fat<<7);}
static sd_slot_t *sd_locate(const dav_tree_entry_t *e,struct sd_index **owner)
{
 for(struct sd_index *d=sd_registry;d;d=d->registry_next){
  uintptr_t base=(uintptr_t)d->slots[0].memory.bytes,v=(uintptr_t)e;
  if(v>=base&&(v-base)%sizeof(sd_slot_t)==0&&(v-base)/sizeof(sd_slot_t)<SD_INDEX_SLOTS){if(owner)*owner=d;return &d->slots[(v-base)/sizeof(sd_slot_t)];}
 }
 return NULL;
}
const dav_tree_entry_t *dav_entry_hold(const dav_tree_entry_t *e){sd_slot_t *s=sd_locate(e,NULL);if(s)s->pins++;return e;}
void dav_entry_release(const dav_tree_entry_t *e){sd_slot_t *s=sd_locate(e,NULL);if(s&&s->pins)s->pins--;}
void dav_entry_auto_release(void *p){dav_entry_release(*(const dav_tree_entry_t **)p);}
void dav_entry_assign(const dav_tree_entry_t **p,const dav_tree_entry_t *e){dav_entry_hold(e);dav_entry_release(*p);*p=e;}
static bool sd_read(struct sd_index *d,uint32_t at,void *buffer,size_t n)
{
 if(!d->file||!sd_owned(d)||at>d->end||n>d->end-at||fseek(d->file,(long)at,SEEK_SET)||fread(buffer,1,n,d->file)!=n)return sd_bad(d);
 return true;
}
static bool sd_write(struct sd_index *d,uint32_t at,const void *buffer,size_t n)
{
 if(!d->file||!sd_owned(d)||at>0x7fffffffU||n>0x7fffffffU-at||fseek(d->file,(long)at,SEEK_SET)||fwrite(buffer,1,n,d->file)!=n)return sd_bad(d);
 return true;
}
static bool sd_pack(struct sd_index *d,sd_slot_t *slot)
{
 dav_tree_entry_t *e=(void *)slot->memory.bytes;memset(d->buffer,0,slot->bytes);
 sd_record_t *r=(void *)d->buffer;r->magic=SD_RECORD_MAGIC;r->bytes=slot->bytes;r->next=slot->next;r->hash_next=slot->hash_next;r->side=slot->side;r->flags=sd_flags(e);r->size=e->size;
 memcpy(r->meta,e->data,DAV_SYNC_META_BYTES);
 const char *strings[]={e->path,e->key,e->fat,e->etag,e->modified,e->created,e->content_type};size_t at=sizeof(*r);
 for(unsigned i=0;i<7;i++){size_t n=strlen(strings[i])+1;if(n>UINT16_MAX||n>slot->bytes-at)return sd_bad(d);
 r->length[i]=(uint16_t)n;memcpy(d->buffer+at,strings[i],n);at+=n;}
 r->crc=0;r->crc=sd_crc(d->buffer,slot->bytes);return true;
}
static bool sd_flush(struct sd_index *d,sd_slot_t *s)
{
 if(!s->id)return true;
 dav_tree_entry_t *e=(void *)s->memory.bytes;
 if(s->saved_flags==sd_flags(e)&&!memcmp(s->saved_meta,e->data,DAV_SYNC_META_BYTES))return true;
 if(!sd_pack(d,s)||!sd_write(d,s->id,d->buffer,s->bytes))return false;
 s->saved_flags=sd_flags(e);memcpy(s->saved_meta,e->data,DAV_SYNC_META_BYTES);return true;
}
static sd_slot_t *sd_get(struct sd_index *d,uint32_t id)
{
 if(!id||!sd_owned(d))return NULL;
 sd_slot_t *slot=NULL;
 for(unsigned i=0;i<SD_INDEX_SLOTS;i++){
  sd_slot_t *s=&d->slots[i];if(s->id==id){s->stamp=++d->clock;return s;}
  if(!s->pins&&(!slot||!s->id||(slot->id&&s->stamp<slot->stamp)))slot=s;
 }
 if(!slot||!sd_flush(d,slot)){sd_bad(d);return NULL;}
 if(!sd_read(d,id,d->buffer,512))return NULL;
 sd_record_t *r=(void *)d->buffer;uint32_t bytes=r->bytes;
 if(r->magic!=SD_RECORD_MAGIC||bytes<512||bytes>SD_INDEX_SLOT_BYTES||bytes%512||r->side>1||id<SD_HEADER_BYTES||id%512||bytes>d->end-id){sd_bad(d);return NULL;}
 if(bytes>512&&!sd_read(d,id+512,d->buffer+512,bytes-512))return NULL;
 uint32_t crc=r->crc;r->crc=0;if(sd_crc(d->buffer,bytes)!=crc){sd_bad(d);return NULL;}r->crc=crc;
 memset(slot,0,sizeof(*slot));slot->id=id;slot->next=r->next;slot->hash_next=r->hash_next;slot->side=r->side;slot->bytes=bytes;slot->stamp=++d->clock;
 dav_tree_entry_t *e=(void *)slot->memory.bytes;e->size=r->size;e->kind=(dav_kind_t)(r->flags&15);e->size_known=(r->flags>>4)&1;e->scanned=(r->flags>>5)&1;e->conflict=(r->flags>>6)&1;e->invalid_fat=(r->flags>>7)&1;
 memcpy(e->data,r->meta,DAV_SYNC_META_BYTES);slot->saved_flags=r->flags;memcpy(slot->saved_meta,r->meta,DAV_SYNC_META_BYTES);
 char **dest[]={&e->path,&e->key,&e->fat,&e->etag,&e->modified,&e->created,&e->content_type};size_t src=sizeof(*r),dst=sizeof(*e)+DAV_SYNC_META_BYTES;
 for(unsigned i=0;i<7;i++){
  size_t n=r->length[i];if(!n||n>bytes-src||n>SD_INDEX_SLOT_BYTES-dst||d->buffer[src+n-1]||memchr(d->buffer+src,0,n-1)){sd_bad(d);return NULL;}
  *dest[i]=(char *)slot->memory.bytes+dst;memcpy(*dest[i],d->buffer+src,n);src+=n;dst+=n;
 }
 if(d->ops.cooperate)d->ops.cooperate(d->ops.context);
 return slot;
}
static dav_tree_entry_t *sd_entry(struct sd_index *d,uint32_t id){sd_slot_t *s=sd_get(d,id);return s?(void *)s->memory.bytes:NULL;}
bool sd_index_reserved(const char *path)
{
 const char name[]=SD_INDEX_FOLDER;size_t n=sizeof(name)-1;
 for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)path[i];if(!c)return false;
 if(c>='a'&&c<='z')c-=32;
 if(c!=(unsigned char)name[i])return false;}
 return !path[n]||path[n]=='/';
}
bool sd_index_close_file(dav_tree_t *t)
{
 struct sd_index *d=t->disk;if(!d)return true;
 if(d->failed)return false;
 if(!d->file)return true;
 if(!sd_owned(d))return false;
 for(unsigned i=0;i<SD_INDEX_SLOTS;i++)if(!sd_flush(d,&d->slots[i]))return false;
 if(fflush(d->file)||fsync(fileno(d->file)))return sd_bad(d);
 FILE *f=d->file;d->file=NULL;if(fclose(f))return sd_bad(d);
 return true;
}
static void sd_close_tree(dav_tree_t *t)
{
 struct sd_index *d=t->disk;if(!d)return;
 if(d->file&&!sd_index_close_file(t))return; // quarantine until restart after ownership/IO loss
 struct sd_index **p=&sd_registry;while(*p&&*p!=d)p=&(*p)->registry_next;if(*p)*p=d->registry_next;
 dav_tree_release(d->slots);dav_tree_release(d->buckets);dav_tree_release(d->buffer);dav_tree_release(d->io_buffer);free(d);memset(t,0,sizeof(*t));
}
// Only files with our exact filename/header format inside the marked cache are ours.
static bool sd_remove_old(const char *dir,const sd_index_ops_t *ops)
{
 DIR *scan=opendir(dir);if(!scan)return false;
 bool ok=true;struct dirent *v;
 while((v=readdir(scan))){
  if(!ops->owned(ops->context)){ok=false;break;}
  if(strlen(v->d_name)!=12||strcmp(v->d_name+8,".IDX"))continue;
  bool hex=true;for(unsigned i=0;i<8;i++)if(!((v->d_name[i]>='0'&&v->d_name[i]<='9')||(v->d_name[i]>='a'&&v->d_name[i]<='f')))hex=false;
  if(!hex)continue;
  char path[DAV_PATH_LIMIT+80];if(snprintf(path,sizeof(path),"%s/%s",dir,v->d_name)>=(int)sizeof(path)){ok=false;break;}
  bool active=false;for(struct sd_index *d=sd_registry;d;d=d->registry_next)if(!strcmp(d->path,path))active=true;
  if(active)continue;
  struct stat st;
#ifndef ESP_PLATFORM
  if(lstat(path,&st)||!S_ISREG(st.st_mode))continue;
#else
  if(stat(path,&st)||!S_ISREG(st.st_mode))continue;
#endif
  unsigned char header[512]={0};FILE *f=fopen(path,"rb");if(!f){ok=false;break;}
  size_t n=fread(header,1,sizeof(header),f);bool clean=!ferror(f);if(fclose(f))clean=false;
  uint32_t crc=0;memcpy(&crc,header+508,4);
  if(clean&&n==512&&!memcmp(header,"BNSDIDX1",8)&&crc==sd_crc(header,508)&&(!ops->owned(ops->context)||unlink(path))){ok=false;break;}
 }
 if(closedir(scan))ok=false;
 return ok;
}
bool sd_index_open(dav_tree_t *t,const char *base,const char *binding,uint32_t nonce,const sd_index_ops_t *ops)
{
 if(t->head[0]||t->head[1]||t->strings||t->disk||!ops->owned||!ops->owned(ops->context))return false;
 char dir[DAV_PATH_LIMIT+64],path[DAV_PATH_LIMIT+80];struct stat st;
 if(snprintf(dir,sizeof(dir),"%s/%s",base,SD_INDEX_FOLDER)>=(int)sizeof(dir))return false;
 const unsigned char marker[]="BERNINA SD INDEX OWNED V1\n";
 // FAT ignores case. Reject a pre-existing alternate spelling rather than hiding it.
 DIR *root=opendir(base);if(!root)return false;
 bool collision=false;struct dirent *item;
 while((item=readdir(root)))if(sd_index_reserved(item->d_name)&&strcmp(item->d_name,SD_INDEX_FOLDER))collision=true;
 if(closedir(root)||collision)return false;
 bool created=false;
 #ifndef ESP_PLATFORM
 int stat_result=lstat(dir,&st);
#else
 int stat_result=stat(dir,&st);
#endif
 if(stat_result){if(errno!=ENOENT||!ops->owned(ops->context)||mkdir(dir,0700))return false;
 created=true;}
 else if(!S_ISDIR(st.st_mode))return false;
 snprintf(path,sizeof(path),"%s/OWNER.BIN",dir);
 if(created){if(!ops->owned(ops->context))return false;int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return false;
 bool ok=ops->owned(ops->context)&&write(fd,marker,sizeof(marker))==(ssize_t)sizeof(marker);if(fsync(fd))ok=false;if(close(fd))ok=false;if(!ok)return false;}
 else {
#ifndef ESP_PLATFORM
 if(lstat(path,&st)||!S_ISREG(st.st_mode))return false;
#endif
 unsigned char value[sizeof(marker)+1];FILE *f=fopen(path,"rb");if(!f)return false;
 size_t n=fread(value,1,sizeof(value),f);bool ok=n==sizeof(marker)&&!memcmp(value,marker,n)&&!ferror(f);if(fclose(f))ok=false;if(!ok)return false;}
 if(!sd_remove_old(dir,ops))return false;
 if(t->budget&&sizeof(sd_slot_t)*SD_INDEX_SLOTS+2*SD_BUCKETS*sizeof(uint32_t)+SD_INDEX_SLOT_BYTES+SD_IO_BYTES>t->budget)return false;
 struct sd_index *d=calloc(1,sizeof(*d));if(!d)return false;
 d->tree=t;d->ops=*ops;d->ordered[0]=d->ordered[1]=true;
 d->slots=dav_tree_allocate(sizeof(sd_slot_t)*SD_INDEX_SLOTS);d->buckets=dav_tree_allocate(2*SD_BUCKETS*sizeof(uint32_t));d->buffer=dav_tree_allocate(SD_INDEX_SLOT_BYTES);d->io_buffer=dav_tree_allocate(SD_IO_BYTES);
 if(!d->slots||!d->buckets||!d->buffer||!d->io_buffer){dav_tree_release(d->slots);dav_tree_release(d->buckets);dav_tree_release(d->buffer);dav_tree_release(d->io_buffer);free(d);return false;}
 int fd=-1;for(unsigned attempt=0;attempt<16;attempt++){if(!ops->owned(ops->context))break;snprintf(d->path,sizeof(d->path),"%s/%08lx.IDX",dir,(unsigned long)(nonce+attempt));fd=open(d->path,O_RDWR|O_CREAT|O_EXCL,0600);if(fd>=0||errno!=EEXIST)break;}
 if(fd<0){dav_tree_release(d->slots);dav_tree_release(d->buckets);dav_tree_release(d->buffer);dav_tree_release(d->io_buffer);free(d);return false;}
 d->file=fdopen(fd,"w+b");if(!d->file){close(fd);dav_tree_release(d->slots);dav_tree_release(d->buckets);dav_tree_release(d->buffer);dav_tree_release(d->io_buffer);free(d);return false;}
 setvbuf(d->file,(char *)d->io_buffer,_IOFBF,SD_IO_BYTES);
 d->end=SD_HEADER_BYTES;memset(d->buffer,0,SD_HEADER_BYTES);memcpy(d->buffer,"BNSDIDX1",8);size_t bind=strlen(binding);if(bind>128)bind=128;memcpy(d->buffer+16,binding,bind);uint32_t header_crc=sd_crc(d->buffer,508);memcpy(d->buffer+508,&header_crc,4);
 if(!sd_write(d,0,d->buffer,SD_HEADER_BYTES)||fflush(d->file)||fsync(fileno(d->file))){fclose(d->file);dav_tree_release(d->slots);dav_tree_release(d->buckets);dav_tree_release(d->buffer);dav_tree_release(d->io_buffer);free(d);return false;}
 d->registry_next=sd_registry;sd_registry=d;t->disk=d;t->used=sizeof(sd_slot_t)*SD_INDEX_SLOTS+2*SD_BUCKETS*sizeof(uint32_t)+SD_INDEX_SLOT_BYTES+SD_IO_BYTES;
 return true;
}
bool sd_index_healthy(const dav_tree_t *t){return !t->disk||(!t->disk->failed&&t->disk->file);}
size_t sd_index_bytes(const dav_tree_t *t){return t->disk?t->disk->end:0;}
bool sd_index_seal(dav_tree_t *t,const char *generation)
{
 struct sd_index *d=t->disk;if(!d||!sd_owned(d)||strlen(generation)!=64)return false;
 for(unsigned i=0;i<SD_INDEX_SLOTS;i++)if(!sd_flush(d,&d->slots[i]))return false;
 if(fflush(d->file)||fsync(fileno(d->file)))return sd_bad(d);
 // Verify every record on disk after writes, without trusting resident cache slots.
 for(uint32_t at=SD_HEADER_BYTES;at<d->end;){
  if(!sd_read(d,at,d->buffer,512))return false;
 sd_record_t *r=(void *)d->buffer;uint32_t n=r->bytes;
  if(r->magic!=SD_RECORD_MAGIC||n<512||n>4096||n%512||n>d->end-at)return sd_bad(d);
  if(n>512&&!sd_read(d,at+512,d->buffer+512,n-512))return false;
 uint32_t crc=r->crc;r->crc=0;if(sd_crc(d->buffer,n)!=crc)return sd_bad(d);
 at+=n;
  if(d->ops.cooperate)d->ops.cooperate(d->ops.context);
 }
 if(!sd_read(d,0,d->buffer,512))return false;
 memcpy(d->buffer+160,generation,64);memcpy(d->buffer+224,"COMPLETE",8);uint32_t count=(uint32_t)(t->count[0]+t->count[1]);memcpy(d->buffer+232,&count,4);uint32_t crc=sd_crc(d->buffer,508);memcpy(d->buffer+508,&crc,4);
 if(!sd_write(d,0,d->buffer,512)||fflush(d->file)||fsync(fileno(d->file)))return sd_bad(d);
 unsigned char expected[512];memcpy(expected,d->buffer,512);
 if(!sd_read(d,0,d->buffer,512)||memcmp(expected,d->buffer,512))return sd_bad(d);
 d->sealed=true;return true;
}
static dav_error_t sd_add(dav_tree_t *t,unsigned side,const char *path,const dav_entry_t *v,uint32_t (*upper)(uint32_t))
{
 struct sd_index *d=t->disk;if(sd_index_reserved(path))return DAV_ERR_RESERVED;
 if(side>1||!sd_owned(d)||d->sealed)return DAV_ERR_STORE;
 char key[DAV_PATH_LIMIT],fat[DAV_PATH_LIMIT];bool invalid;
 if(!dav_normalize_path(path,key,fat,sizeof(key),upper,&invalid))return DAV_ERR_PATH;
 if(!t->uppercase)t->uppercase=upper;
 uint32_t bucket=(uint32_t)sd_hash(fat,strlen(fat));
 for(uint32_t id=d->buckets[side*SD_BUCKETS+bucket];id;){sd_slot_t *s=sd_get(d,id);if(!s)return DAV_ERR_STORE;
 dav_tree_entry_t *e=(void *)s->memory.bytes;id=s->hash_next;if(!strcmp(e->path,path))return DAV_ERR_DUPLICATE;}
 sd_slot_t *slot=NULL;for(unsigned i=0;i<SD_INDEX_SLOTS;i++)if(!d->slots[i].pins&&(!slot||!d->slots[i].id||(slot->id&&d->slots[i].stamp<slot->stamp)))slot=&d->slots[i];
 if(!slot||!sd_flush(d,slot)){sd_bad(d);return DAV_ERR_STORE;}
 memset(slot,0,sizeof(*slot));slot->id=d->end;slot->pins=1;slot->side=side;slot->stamp=++d->clock;slot->hash_next=d->buckets[side*SD_BUCKETS+bucket];
 dav_tree_entry_t *e=(void *)slot->memory.bytes;size_t dst=sizeof(*e)+DAV_SYNC_META_BYTES,total=sizeof(sd_record_t);const char *strings[]={path,key,fat,v->etag,v->modified,v->created,v->content_type};char **dest[]={&e->path,&e->key,&e->fat,&e->etag,&e->modified,&e->created,&e->content_type};
 for(unsigned i=0;i<7;i++){size_t n=strlen(strings[i])+1;if(n>4096-dst){memset(slot,0,sizeof(*slot));return DAV_ERR_RESOURCE;}*dest[i]=(char*)slot->memory.bytes+dst;memcpy(*dest[i],strings[i],n);dst+=n;total+=n;}
 slot->bytes=(uint32_t)((total+511)&~(size_t)511);if(slot->bytes>4096){memset(slot,0,sizeof(*slot));return DAV_ERR_RESOURCE;}e->size=v->size;e->kind=v->kind;e->size_known=v->size_known;e->invalid_fat=invalid;
 for(uint32_t id=slot->hash_next;id;){sd_slot_t *other=sd_get(d,id);if(!other){slot->pins=0;return DAV_ERR_STORE;}dav_tree_entry_t *o=(void*)other->memory.bytes;id=other->hash_next;if(!strcmp(o->fat,e->fat))o->conflict=e->conflict=true;}
 if(!sd_pack(d,slot)||!sd_write(d,slot->id,d->buffer,slot->bytes)){slot->pins=0;return DAV_ERR_STORE;}d->end+=slot->bytes;slot->saved_flags=sd_flags(e);memcpy(slot->saved_meta,e->data,DAV_SYNC_META_BYTES);
 if(d->tails[side]){sd_slot_t *tail=sd_get(d,d->tails[side]);if(!tail){slot->pins=0;return DAV_ERR_STORE;}if(strcmp(((dav_tree_entry_t *)(void*)tail->memory.bytes)->path,e->path)>=0)d->ordered[side]=false;// Physical append order supplies the iterator; no random tail rewrite.
 }
 else {d->heads[side]=slot->id;t->head[side]=e;dav_entry_hold(e);}
 d->tails[side]=slot->id;d->buckets[side*SD_BUCKETS+bucket]=slot->id;slot->pins--;
 t->count[side]++;if(v->kind==DAV_FILE)t->files[side]++;if(v->kind==DAV_DIRECTORY)t->directories[side]++;return DAV_OK;
}
static const dav_tree_entry_t *sd_match(const dav_tree_t *t,unsigned side,const dav_tree_entry_t *entry,bool *ambiguous,bool exact,const char *path)
{
 struct sd_index *d=t->disk;const dav_tree_entry_t *found=NULL;uint32_t bucket=(uint32_t)sd_hash(entry->fat,strlen(entry->fat));*ambiguous=entry->conflict;
 for(uint32_t id=d->buckets[side*SD_BUCKETS+bucket];id;){sd_slot_t *slot=sd_get(d,id);if(!slot)break;dav_tree_entry_t *e=(void*)slot->memory.bytes;id=slot->hash_next;
  if(exact?strcmp(e->path,path)==0:strcmp(e->fat,entry->fat)==0){if(found||e->conflict)*ambiguous=true;if(!found)found=dav_entry_hold(e);if(exact)break;}
 }
 dav_entry_release(found);return found;
}
