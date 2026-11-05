#ifndef FAST_SYNC_H
#define FAST_SYNC_H
#include "mirror_sync.h"
typedef struct {
 void *context;
 bool (*owned)(void *);
 int (*present)(void *,const char *,bool);
 bool (*ignored)(void *,const char *,bool);
 bool (*copy)(void *,const dav_tree_entry_t *);
 bool (*arm)(void *);
 bool (*space)(void *,uint64_t);
 void *(*allocate)(size_t);
 void (*release)(void *);
 void (*phase)(void *,const char *);
 void (*progress)(void *,uint64_t,uint64_t,uint64_t,uint64_t);
 void (*cooperate)(void *);
 void (*fault)(void *);
 void (*failure)(void *,const char *,const char *,int);
 void (*scan)(void *,const char *,uint64_t,uint64_t);
} fast_sync_ops_t;
typedef struct {uint64_t differences,files,bytes,completed,received,deleted;bool rebuild,armed,success;const char *error;} fast_sync_result_t;
/* NAS must be complete and validated; baseline is last applied NAS, not SD timestamps. */
bool fast_sync_reconcile(const char *,const fast_sync_ops_t *,fast_sync_result_t *);
bool fast_sync_clear(const char *,const fast_sync_ops_t *,fast_sync_result_t *);
void fast_sync_run(const char *,dav_tree_t *,bool,unsigned,const fast_sync_ops_t *,fast_sync_result_t *);
#endif
