#ifndef REMOTE_PLAN_H
#define REMOTE_PLAN_H
#include "nas_index.h"
typedef bool (*remote_emit_t)(void *,char,const char *,const dav_entry_t *);
typedef struct {
 nas_index_parser_t index;
 char line[3400],generation[65],action;
 size_t used;
 uint64_t files,bytes,operations,differences,seen_files,seen_bytes;
 bool full,header,ended,failed;unsigned phase;
 remote_emit_t emit;void *context;
} remote_plan_t;
void remote_plan_init(remote_plan_t *,remote_emit_t,void *);
bool remote_plan_feed(remote_plan_t *,const char *,size_t);
bool remote_plan_finish(remote_plan_t *);
#endif
