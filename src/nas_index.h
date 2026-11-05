#ifndef NAS_INDEX_H
#define NAS_INDEX_H
#include "dav_listing.h"
typedef bool (*nas_index_emit_t)(void *,const char *,const dav_entry_t *);
typedef struct {
 char line[3400],token[65];
 size_t used,count;
 bool header,ended,failed;
 nas_index_emit_t emit;
 void *context;
} nas_index_parser_t;
void nas_index_init(nas_index_parser_t *,nas_index_emit_t,void *);
bool nas_index_feed(nas_index_parser_t *,const char *,size_t);
bool nas_index_finish(const nas_index_parser_t *);
#endif
