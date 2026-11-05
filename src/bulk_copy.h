#ifndef BULK_COPY_H
#define BULK_COPY_H
#include "nas_index.h"
typedef struct {
    const char *path;
    uint64_t size;
    unsigned char hash[32];
} bulk_entry_t;
typedef struct {
    void *context;
    bool (*owned)(void *);
    int (*read)(void *, unsigned char *, size_t);
    bool (*complete)(void *);
    bool (*hash_start)(void *);
    bool (*hash_update)(void *, const unsigned char *, size_t);
    bool (*hash_finish)(void *, unsigned char *);
    void (*progress)(void *, uint64_t, unsigned, bool);
    void (*cooperate)(void *);
    void (*fault)(void *);
    uint64_t (*clock_us)(void *);
    void *(*allocate)(size_t);
    void *(*allocate_read)(size_t);
    void *(*allocate_write)(size_t);
    void (*release)(void *);
} bulk_ops_t;
typedef struct {
    unsigned completed;
    uint64_t bytes, read_us, open_us, write_us, finish_us;
    uint64_t read_calls, read_bytes, write_calls, write_bytes;
    bool success;
    const char *error;
} bulk_result_t;
/* Entries come from the fully validated immutable NAS plan; success additionally
   requires the matching bundle footer and complete HTTP response. */
void bulk_receive(const char *, const char *, const char *, uint64_t,
                  const bulk_entry_t *, unsigned, const bulk_ops_t *, bulk_result_t *);
#endif
