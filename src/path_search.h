#ifndef PATH_SEARCH_H
#define PATH_SEARCH_H
#include "dry_run.h"
typedef struct { char nfc[DAV_PATH_LIMIT],fat[DAV_PATH_LIMIT]; bool basename; } path_query_t;
bool path_query_prepare(path_query_t *query,const char *text,uint32_t (*uppercase)(uint32_t));
bool path_query_matches(const path_query_t *query,const dav_tree_entry_t *entry);
bool path_query_decode(const char *encoded,char *decoded,size_t capacity);
uint64_t path_search_sd_only_files(const dav_tree_t *tree,uint64_t *sd_files);
#endif
