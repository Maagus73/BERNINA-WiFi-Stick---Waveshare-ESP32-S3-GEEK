#ifndef MIRROR_SYNC_H
#define MIRROR_SYNC_H
#include "dry_run.h"
#include "safe_update.h"
typedef struct {
 unsigned char hash[32];
 bool hashed,seen,done,created;
 unsigned char ordinal[4];
} mirror_meta_t;
_Static_assert(sizeof(mirror_meta_t)<=DAV_SYNC_META_BYTES,"mirror metadata must fit existing node tail");
typedef struct {
 uint64_t new_files,changed_files,unknown_files,delete_files,create_dirs,remove_dirs,conflicts,internal_files;
 uint64_t files,download,required,free_bytes,generated_max;
 uint32_t nonce;
 bool complete,valid,empty_source,dangerous,delete_allowed,danger_confirmed;
} mirror_plan_t;
typedef enum { MIRROR_IDLE,MIRROR_PLANNING,MIRROR_PREVIEW,MIRROR_RUNNING,MIRROR_COMPLETE,MIRROR_HELD,MIRROR_FAILED } mirror_state_t;
typedef enum { MIRROR_PHASE_COPY=1,MIRROR_PHASE_DELETE=2,MIRROR_PHASE_VERIFY=3 } mirror_phase_t;
typedef struct {
 single_copy_ops_t io;
 bool (*source)(void *,dav_tree_t *); // streaming full source snapshot revalidation, never a second tree
 bool (*absent)(void *,const char *,bool); // authoritative HEAD 404 only
 bool (*new_file)(void *,const dav_tree_entry_t *,unsigned char *);
 bool (*replace_file)(void *,const dav_tree_entry_t *,const dav_tree_entry_t *,uint32_t,const unsigned char *,unsigned char *);
 bool (*journal)(void *,mirror_phase_t,const char *,bool,const unsigned char *,uint64_t);
 bool (*finish)(void *,bool); // false = safe transfers done, deletion explicitly held
 void (*progress)(void *,uint64_t,uint64_t,uint64_t);
 bool (*cut)(void *,unsigned); // optional host-only restart injection
 void (*phase)(void *,const char *); // optional bounded phase log/status, no per-entry traffic
} mirror_ops_t;
typedef struct {
 mirror_state_t state;
 uint64_t completed,deleted_files,deleted_dirs,cleaned;
 bool armed;
 const char *error;
} mirror_result_t;
mirror_meta_t *mirror_meta(const dav_tree_entry_t *entry);
uint32_t mirror_ordinal(const dav_tree_entry_t *entry);
const dav_tree_entry_t *mirror_find(const dav_tree_t *tree,unsigned side,const char *path);
void mirror_plan_build(dav_tree_t *tree,bool complete,uint64_t free_bytes,uint32_t nonce,mirror_plan_t *plan);
bool mirror_hash_file(const char *base,const char *relative,uint64_t size,const single_copy_ops_t *ops,unsigned char hash[32]);
bool mirror_capture(const char *base,dav_tree_t *tree,const mirror_plan_t *plan,const single_copy_ops_t *ops);
bool mirror_source_entry(dav_tree_t *tree,const char *parent,const dav_entry_t *entry);
void mirror_source_reset(dav_tree_t *tree);
bool mirror_source_complete(const dav_tree_t *tree);
void mirror_run(const char *base,dav_tree_t *tree,const mirror_plan_t *plan,const mirror_ops_t *ops,mirror_result_t *result);
#endif
