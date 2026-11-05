#ifndef DAV_STORE_H
#define DAV_STORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define DAV_STORE_BLOCK 4096U
#define DAV_STORE_HEADER 4096U
// Append-only, immutable records. Only the compact tree headers stay in PSRAM.
// The mapped record addresses are session-local. Never recover pointers after boot.
typedef struct {
 void *context;
 const unsigned char *mapped;
 size_t capacity;
 bool (*owned)(void *);
 bool (*erase)(void *,size_t,size_t);
 bool (*write)(void *,size_t,const void *,size_t);
 bool (*read)(void *,size_t,void *,size_t);
 void (*close)(void *);
 void (*cooperate)(void *);
} dav_store_ops_t;
typedef struct dav_store {
 dav_store_ops_t ops;
 size_t used,erased,records;
 uint32_t crc;
 bool failed,sealed;
 const char *failure;
 size_t failure_offset;
} dav_store_t;
static inline bool dav_store_fail(dav_store_t *s,const char *stage,size_t offset)
{
 s->failed=true;if(!s->failure){s->failure=stage;s->failure_offset=offset;}return false;
}
static inline uint32_t dav_store_crc(uint32_t crc,const unsigned char *p,size_t n)
{
 for(size_t i=0;i<n;i++){crc^=p[i];for(unsigned j=0;j<8;j++)crc=(crc>>1)^(0xedb88320U&((uint32_t)0-(crc&1)));}
 return crc;
}
static inline bool dav_store_begin(dav_store_t *s,const dav_store_ops_t *ops)
{
 memset(s,0,sizeof(*s));s->ops=*ops;s->crc=0xffffffffU;s->used=DAV_STORE_HEADER;
 s->failed=!ops->mapped||ops->capacity<DAV_STORE_HEADER+DAV_STORE_BLOCK||!ops->owned||!ops->erase||!ops->write||!ops->read||!ops->owned(ops->context)||!ops->erase(ops->context,0,DAV_STORE_HEADER);
 if(!s->failed)s->erased=DAV_STORE_HEADER;
 else dav_store_fail(s,"initialisierung/erase",0);
 return !s->failed;
}
static inline const void *dav_store_append(dav_store_t *s,const void *data,size_t n)
{
 if(s->failed||s->sealed||!n||n>DAV_STORE_BLOCK||n%8||s->used>s->ops.capacity||n>s->ops.capacity-s->used||!s->ops.owned(s->ops.context)){dav_store_fail(s,"zustand/kapazitaet/besitz",s->used);return NULL;}
 size_t end=s->used+n;
 while(end>s->erased){
  if(!s->ops.owned(s->ops.context)||s->erased+DAV_STORE_BLOCK>s->ops.capacity||!s->ops.erase(s->ops.context,s->erased,DAV_STORE_BLOCK)){dav_store_fail(s,"erase/cache-abgleich",s->erased);return NULL;}
  s->erased+=DAV_STORE_BLOCK;if(s->ops.cooperate)s->ops.cooperate(s->ops.context);
 }
 if(!s->ops.write(s->ops.context,s->used,data,n)){dav_store_fail(s,"write/cache-abgleich",s->used);return NULL;}
 unsigned char verify[256];
 for(size_t at=0;at<n;at+=sizeof(verify)){
  size_t take=n-at;if(take>sizeof(verify))take=sizeof(verify);
  if(!s->ops.read(s->ops.context,s->used+at,verify,take)){dav_store_fail(s,"read",s->used+at);return NULL;}
  if(memcmp(verify,(const unsigned char *)data+at,take)){dav_store_fail(s,"physisches-readback",s->used+at);return NULL;}
  if(memcmp(s->ops.mapped+s->used+at,verify,take)){dav_store_fail(s,"mmap-readback",s->used+at);return NULL;}
 }
 const void *result=s->ops.mapped+s->used;s->used=end;s->records++;s->crc=dav_store_crc(s->crc,data,n);
 if(s->ops.cooperate)s->ops.cooperate(s->ops.context);
 return result;
}
static inline bool dav_store_seal(dav_store_t *s,const char *generation,size_t nas,size_t sd)
{
 if(s->failed||s->sealed||!s->ops.owned(s->ops.context)||strlen(generation)!=64)return false;
 unsigned char block[512];uint32_t crc=0xffffffffU;
 for(size_t at=DAV_STORE_HEADER;at<s->used;at+=sizeof(block)){
  size_t take=s->used-at;if(take>sizeof(block))take=sizeof(block);
  if(!s->ops.owned(s->ops.context)||!s->ops.read(s->ops.context,at,block,take)){s->failed=true;return false;}
  crc=dav_store_crc(crc,block,take);if(s->ops.cooperate)s->ops.cooperate(s->ops.context);
 }
 if(crc!=s->crc){s->failed=true;return false;}
 struct {char magic[8],generation[64];uint32_t bytes,records,nas,sd,crc,header_crc;} header={0};
 memcpy(header.magic,"BIDXBLK1",8);memcpy(header.generation,generation,64);
 header.bytes=(uint32_t)s->used;header.records=(uint32_t)s->records;header.nas=(uint32_t)nas;header.sd=(uint32_t)sd;header.crc=crc;
 header.header_crc=dav_store_crc(0xffffffffU,(const unsigned char *)&header,sizeof(header)-sizeof(uint32_t));
 if(!s->ops.write(s->ops.context,0,&header,sizeof(header))||!s->ops.read(s->ops.context,0,block,sizeof(header))||memcmp(block,&header,sizeof(header))){s->failed=true;return false;}
 s->sealed=true;return true;
}
static inline void dav_store_close(dav_store_t *s)
{
 if(s&&s->ops.close)s->ops.close(s->ops.context);
 if(s)memset(s,0,sizeof(*s));
}
#endif
