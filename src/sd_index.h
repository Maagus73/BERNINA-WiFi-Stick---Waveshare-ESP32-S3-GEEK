#ifndef SD_INDEX_H
#define SD_INDEX_H
#include "dav_tree.h"
#define SD_INDEX_FOLDER "BNCACHE"
#define SD_INDEX_SLOTS 160U
#define SD_INDEX_SLOT_BYTES 4096U
typedef struct {
 void *context;
 bool (*owned)(void *);
 void (*fault)(void *);
 void (*cooperate)(void *);
} sd_index_ops_t;
bool sd_index_open(dav_tree_t *,const char *,const char *,uint32_t,const sd_index_ops_t *);
bool sd_index_close_file(dav_tree_t *);
bool sd_index_seal(dav_tree_t *,const char *);
bool sd_index_healthy(const dav_tree_t *);
size_t sd_index_bytes(const dav_tree_t *);
bool sd_index_reserved(const char *);
#endif
