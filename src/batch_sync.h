#ifndef BATCH_SYNC_H
#define BATCH_SYNC_H
#include "dry_run.h"
#define BATCH_FILES 10
#define BATCH_DIRS 64
#ifndef BATCH_BYTE_LIMIT
#define BATCH_BYTE_LIMIT (10ULL*1024*1024)
#endif
#define BATCH_MAX_AGE 300
// References into the existing comparison, valid only while its revision is pinned.
typedef struct {
 const dav_tree_entry_t *files[BATCH_FILES],*dirs[BATCH_DIRS];
 bool existed[BATCH_DIRS];
 unsigned count,directory_count,new_directories;
 uint64_t bytes,conflicts,unversioned,oversized,new_files,unknown_sizes,reserved;
 bool valid,directory_limit;
} batch_plan_t;
typedef enum { BATCH_IDLE,BATCH_PREVIEW,BATCH_RUNNING,BATCH_SUCCESS,BATCH_PARTIAL,BATCH_FAILED,BATCH_ABORTED } batch_state_t;
bool batch_strong_etag(const char *etag);
void batch_plan_release(batch_plan_t *);
void batch_plan_build(const dav_tree_t *tree,bool complete,uint64_t limit,batch_plan_t *plan);
#endif
