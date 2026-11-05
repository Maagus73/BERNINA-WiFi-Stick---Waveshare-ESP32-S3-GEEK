#ifndef DAV_TREE_H
#define DAV_TREE_H
#include "dav_listing.h"
#define DAV_SYNC_META_BYTES 40 // bounded in-node hashes/flags; no second directory tree
typedef struct dav_tree_entry {
 struct dav_tree_entry *next,*hash_next;
 char *path,*key,*fat,*etag,*modified,*created,*content_type;
 uint64_t size;
 dav_kind_t kind;
 bool size_known,scanned,conflict,invalid_fat;
 char data[];
} dav_tree_entry_t;
struct dav_intern;
struct dav_store;
struct sd_index;
typedef struct {
 struct dav_intern **strings;
 uint32_t (*uppercase)(uint32_t);
 struct dav_store *store; // optional immutable flash-backed string records
 struct sd_index *disk;
 bool (*ignore_path)(const char *,bool);
 dav_tree_entry_t *head[2],*tail[2]; // 0 NAS, 1 SD
 dav_tree_entry_t **buckets[2];
 size_t used,budget,count[2],files[2],directories[2],scanned[2];
} dav_tree_t;
// Iteration references stay pinned until advance or scope exit (also break/return).
#define DAV_AUTO __attribute__((cleanup(dav_entry_auto_release)))
const dav_tree_entry_t *dav_entry_hold(const dav_tree_entry_t *);
void dav_entry_release(const dav_tree_entry_t *);
void dav_entry_auto_release(void *);
void dav_entry_assign(const dav_tree_entry_t **,const dav_tree_entry_t *);
dav_tree_entry_t *dav_tree_first(const dav_tree_t *,unsigned);
dav_tree_entry_t *dav_tree_next(const dav_tree_t *,const dav_tree_entry_t *);
bool dav_tree_ordered(const dav_tree_t *,unsigned);
bool dav_tree_healthy(const dav_tree_t *);
void dav_tree_clear(dav_tree_t *tree);
dav_error_t dav_tree_add(dav_tree_t *tree,unsigned side,const char *path,const dav_entry_t *entry,uint32_t (*uppercase)(uint32_t));
const dav_tree_entry_t *dav_tree_match(const dav_tree_t *tree,unsigned other,const dav_tree_entry_t *entry,bool *ambiguous);
const dav_tree_entry_t *dav_tree_find_exact(const dav_tree_t *,unsigned,const char *);
bool dav_tree_ancestor_conflict(const dav_tree_t *tree,const dav_tree_entry_t *entry);
#endif
