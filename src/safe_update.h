#ifndef SAFE_UPDATE_H
#define SAFE_UPDATE_H
#include "single_copy.h"
#define UPDATE_TEST_LIMIT (10ULL*1024*1024)
#define UPDATE_LIMIT (0x7FFFFFFFULL) // signed 32-bit ESP VFS stat/off_t; Test19 UI remains bounded separately
#define UPDATE_RESERVE (512ULL*1024)
#define UPDATE_VERSION 0x42313901U
typedef struct {
 uint32_t version,nonce,restoring,restore_attempt;
 uint32_t cid[7]; // canonical SD identification, bound by the record checksum
 uint64_t old_size,new_size;
 char target[DAV_PATH_LIMIT],stage[32],backup[32],displaced[32],journal[32],restore[32];
 char etag[128],modified[96];
 unsigned char old_hash[32],new_hash[32],checksum[32];
} update_record_t;
typedef struct {
 single_copy_ops_t io;
 bool (*content)(void *,const unsigned char *); // optional server-provided SHA256
 bool (*current)(void *); // conditional NAS version check immediately before replacement
 bool (*arm)(void *,const update_record_t *); // durable independent journal before first rename
 bool (*disarm)(void *);
 bool (*checkpoint)(void *,unsigned); // test-only simulated interruption; NULL in firmware
 bool skip_identical; // mirror may retain equal content after verified conditional download
} update_ops_t;
typedef struct {
 bool success,armed,restored,storage_error,quarantined;
 const char *error;
 uint64_t received,partial_size;
 bool partial_safe; // failed download closed successfully before any rename
 char partial[40];
} update_result_t;
void update_names(update_record_t *record,uint32_t nonce);
bool update_hash_file(const char *base,const char *relative,uint64_t size,const single_copy_ops_t *ops,unsigned char hash[32]);
bool update_record_seal(update_record_t *record,const single_copy_ops_t *ops);
bool update_record_valid(const update_record_t *record,const single_copy_ops_t *ops);
void update_run(const char *base,update_record_t *record,const update_ops_t *ops,update_result_t *result);
void update_recover(const char *base,update_record_t *record,const update_ops_t *ops,update_result_t *result);
#endif
