#ifndef SINGLE_COPY_H
#define SINGLE_COPY_H
#include "dav_listing.h"
#define SINGLE_COPY_LIMIT (1024U*1024U)
typedef struct {
 void *context;
 bool (*owned)(void *);
 bool (*space)(void *,uint64_t);
 bool (*begin)(void *,uint64_t);
 int (*read)(void *,unsigned char *,size_t);
 bool (*complete)(void *);
 bool (*hash_start)(void *);
 bool (*hash_update)(void *,const unsigned char *,size_t);
 bool (*hash_finish)(void *,unsigned char *);
 int (*publish)(void *,const char *,const char *); // MUST fail if destination exists
 uint32_t (*uppercase)(uint32_t);
 uint32_t nonce;
 void (*progress)(void *,uint64_t,uint64_t,bool); // optional observation, no I/O/ownership decisions
 uint64_t size_limit; // zero retains the proven 1 MiB single-file limit
 bool (*validate)(void *); // optional preview-state check immediately before mutation
 void (*cooperate)(void *); // optional scheduler opportunity, never an ownership decision
 bool (*verify_content)(void *,const unsigned char *);
 bool (*before_publish)(void *);
} single_copy_ops_t;
typedef struct {
 bool success,temporary_exists,mutated,handle_quarantined,storage_error,publication_attempted;
 uint64_t received,verified;
 const char *error;
 char temporary[DAV_PATH_LIMIT+64];
 unsigned char download_hash[32],sd_hash[32];
} single_copy_result_t;
bool single_copy_target_available(const char *base,const char *relative,bool allow,const single_copy_ops_t *ops);
bool single_copy_path_state(const char *base,const char *relative,int kind,bool allow_reserved,const single_copy_ops_t *ops);
bool single_copy_existing_target(const char *base,const char *relative,const single_copy_ops_t *ops);
void single_copy_run(const char *base,const char *relative,uint64_t size,bool allow_directories,
                     const single_copy_ops_t *ops,single_copy_result_t *result);
#endif
