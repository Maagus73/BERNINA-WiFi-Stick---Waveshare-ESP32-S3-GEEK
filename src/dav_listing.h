#ifndef DAV_LISTING_H
#define DAV_LISTING_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define DAV_TOKEN_LIMIT 2048
#define DAV_RESPONSE_LIMIT 8192
#define DAV_PATH_LIMIT 1024
#define DAV_TREE_BUDGET (1536 * 1024)
typedef enum { DAV_FILE, DAV_DIRECTORY, DAV_OTHER } dav_kind_t;
typedef enum {
 DAV_OK, DAV_ERR_XML, DAV_ERR_XML_INCOMPLETE, DAV_ERR_PROPERTY, DAV_ERR_PATH,
 DAV_ERR_MEMORY, DAV_ERR_RESOURCE, DAV_ERR_HTTP, DAV_ERR_HTTP_INCOMPLETE,
 DAV_ERR_SD, DAV_ERR_OWNERSHIP, DAV_ERR_DUPLICATE, DAV_ERR_ENTRY_BUDGET, DAV_ERR_STORE, DAV_ERR_SOURCE,DAV_ERR_RESERVED
} dav_error_t;
typedef struct {
 char name[256]; dav_kind_t kind; uint64_t size; bool size_known;
 char etag[128], modified[96], created[96], content_type[96];
} dav_entry_t;
const char *dav_error_name(dav_error_t error);
bool dav_utf8_valid(const char *text);
typedef bool (*dav_emit_t)(void *context,const dav_entry_t *entry);
typedef struct dav_stream dav_stream_t;
dav_stream_t *dav_stream_new(const char *collection_url,dav_emit_t emit,void *context);
dav_error_t dav_stream_feed(dav_stream_t *parser,const char *bytes,size_t length);
dav_error_t dav_stream_finish(dav_stream_t *parser);
size_t dav_stream_offset(const dav_stream_t *parser);
void dav_stream_free(dav_stream_t *parser);
// Unicode NFC and the actual FatFs uppercase mapping supplied by caller.
bool dav_normalize_path(const char *path,char *nfc,char *fat,size_t capacity,
                        uint32_t (*uppercase)(uint32_t),bool *invalid_fat);
#endif
