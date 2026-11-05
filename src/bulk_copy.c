#include "bulk_copy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#define BULK_BUFFER 16384U

typedef struct {
    const bulk_ops_t *ops;
    bulk_result_t *result;
    unsigned char *buffer;
    size_t at, used;
} reader_t;

static uint64_t now(const bulk_ops_t *ops)
{
    return ops->clock_us ? ops->clock_us(ops->context) : 0;
}

static bool fill(reader_t *r)
{
    if (r->at < r->used) return true;
    if (r->ops->cooperate) r->ops->cooperate(r->ops->context);
    if (!r->ops->owned(r->ops->context)) return false;
    uint64_t started = now(r->ops);
    int n = r->ops->read(r->ops->context, r->buffer, BULK_BUFFER);
    r->result->read_us += now(r->ops) - started;
    r->result->read_calls++;
    if (n <= 0 || n > (int)BULK_BUFFER) return false;
    r->result->read_bytes += (unsigned)n;
    r->at = 0;
    r->used = (size_t)n;
    return true;
}

static bool text_line(reader_t *r, char *out, size_t capacity)
{
    size_t count = 0;
    for (;;) {
        if (!fill(r)) return false;
        const unsigned char *p = memchr(r->buffer + r->at, '\n', r->used - r->at);
        size_t n = p ? (size_t)(p - r->buffer - r->at) : r->used - r->at;
        if (n >= capacity - count || memchr(r->buffer + r->at, 0, n)) return false;
        memcpy(out + count, r->buffer + r->at, n);
        count += n;
        r->at += n;
        if (p) {
            r->at++;
            out[count] = 0;
            return true;
        }
    }
}

typedef struct { const bulk_entry_t *entry; bool matched; } match_t;
static int nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static bool match_frame(void *context, const char *path, const dav_entry_t *entry)
{
    match_t *m = context;
    if (entry->kind != DAV_FILE || strcmp(path, m->entry->path) ||
        entry->size != m->entry->size || strlen(entry->etag) != 66) return false;
    for (unsigned i = 0; i < 32; i++) {
        int a = nibble(entry->etag[1 + i*2]), b = nibble(entry->etag[2 + i*2]);
        if (a < 0 || b < 0 || (unsigned char)((a << 4) | b) != m->entry->hash[i]) return false;
    }
    m->matched = true;
    return true;
}

void bulk_receive(const char *base, const char *temporary, const char *plan,
                  uint64_t first, const bulk_entry_t *entries, unsigned count,
                  const bulk_ops_t *ops, bulk_result_t *result)
{
    memset(result, 0, sizeof(*result));
    result->error = "Sammeltransfer: ungueltige Parameter oder APP-Besitz";
    if (!count || count > 128 || strlen(plan) != 64 || !ops->owned(ops->context)) return;
    reader_t reader = {.ops = ops, .result = result};
    reader.buffer = ops->allocate_read ? ops->allocate_read(BULK_BUFFER) : ops->allocate(BULK_BUFFER);
    unsigned char *pending = ops->allocate_write ? ops->allocate_write(BULK_BUFFER) : NULL;
    nas_index_parser_t *parser = ops->allocate(sizeof(*parser));
    char *line = ops->allocate(3400);
    int fd = -1;
    if (!reader.buffer || !pending || !parser || !line) {
        result->error = "Sammeltransfer: Pufferspeicher fehlt";
        goto done;
    }
#define REQUIRE(ok, message) do { if (!(ok)) {result->error = (message); goto done;} } while (0)
#define OWN() REQUIRE(ops->owned(ops->context), "Sammeltransfer: APP-Besitz verloren")
#define IO(ok, message) do { if (!(ok)) {if (ops->fault) ops->fault(ops->context); result->error = (message); goto done;} } while (0)
    char expected[180];
    snprintf(expected, sizeof(expected), "BUNDLE1\t%s\t%llu\t%u", plan, (unsigned long long)first, count);
    REQUIRE(text_line(&reader, line, 3400) && !strcmp(line, expected), "Sammeltransfer: Kopf/Plan/Start/Anzahl ungueltig");
    for (unsigned i = 0; i < count; i++) {
        OWN();
        REQUIRE(text_line(&reader, line, 3400) && !strncmp(line, "FILE\t", 5), "Sammeltransfer: Dateirahmen fehlt");
        match_t match = {.entry = &entries[i]};
        nas_index_init(parser, match_frame, &match);
        snprintf(expected, sizeof(expected), "BINDEX1\t%s\n", plan);
        REQUIRE(nas_index_feed(parser, expected, strlen(expected)), "Sammeltransfer: Plan-ID ungueltig");
        REQUIRE(nas_index_feed(parser, "F\t", 2) && nas_index_feed(parser, line + 5, strlen(line + 5)) &&
                nas_index_feed(parser, "\n", 1) && match.matched, "Sammeltransfer: Pfad/Laenge/SHA256 weicht vom Plan ab");
        REQUIRE(ops->hash_start(ops->context), "Sammeltransfer: SHA256 Start fehlgeschlagen");
        OWN();
        uint64_t opened = now(ops);
        fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        result->open_us += now(ops) - opened;
        IO(fd >= 0, "Sammeltransfer: temporaere SD-Datei nicht geoeffnet");
        uint64_t used = 0;
        size_t pending_used = 0;
        while (used < entries[i].size) {
            OWN();
            REQUIRE(fill(&reader), "Sammeltransfer: Download unterbrochen");
            size_t n = reader.used - reader.at;
            if ((uint64_t)n > entries[i].size - used) n = (size_t)(entries[i].size - used);
            if (n > BULK_BUFFER - pending_used) n = BULK_BUFFER - pending_used;
            REQUIRE(ops->hash_update(ops->context, reader.buffer + reader.at, n), "Sammeltransfer: SHA256 Update fehlgeschlagen");
            memcpy(pending + pending_used, reader.buffer + reader.at, n);
            pending_used += n;
            reader.at += n;
            used += n;
            if (pending_used == BULK_BUFFER || used == entries[i].size) {
                OWN();
                uint64_t started = now(ops);
                ssize_t written = write(fd, pending, pending_used);
                result->write_us += now(ops) - started;
                result->write_calls++;
                if (written > 0) result->write_bytes += (size_t)written;
                IO(written == (ssize_t)pending_used, "Sammeltransfer: SD-Schreibfehler");
                pending_used = 0;
            }
            if (ops->progress) ops->progress(ops->context, result->bytes + used, result->completed, false);
        }
        unsigned char hash[32];
        REQUIRE(ops->hash_finish(ops->context, hash) && !memcmp(hash, entries[i].hash, 32), "Sammeltransfer: Datei-SHA256 ungueltig");
        REQUIRE(fill(&reader) && reader.buffer[reader.at++] == '\n', "Sammeltransfer: Dateiende ungueltig");
        OWN();
        uint64_t started = now(ops);
        IO(fsync(fd) == 0, "Sammeltransfer: SD-Flush fehlgeschlagen");
        int closing = fd;
        fd = -1;
        IO(close(closing) == 0, "Sammeltransfer: SD-Close fehlgeschlagen");
        char target[DAV_PATH_LIMIT + 64];
        int length = snprintf(target, sizeof(target), "%s/%s", base, entries[i].path);
        REQUIRE(length >= 0 && (size_t)length < sizeof(target), "Sammeltransfer: Zielpfad zu lang");
        OWN();
        IO(unlink(target) == 0 || errno == ENOENT, "Sammeltransfer: altes Ziel nicht entfernt");
        OWN();
        IO(rename(temporary, target) == 0, "Sammeltransfer: Veroeffentlichung fehlgeschlagen");
        result->finish_us += now(ops) - started;
        result->completed++;
        result->bytes += entries[i].size;
        if (ops->progress) ops->progress(ops->context, result->bytes, result->completed, true);
    }
    snprintf(expected, sizeof(expected), "END\t%s\t%u", plan, count);
    REQUIRE(text_line(&reader, line, 3400) && !strcmp(line, expected) && reader.at == reader.used &&
            ops->complete(ops->context), "Sammeltransfer: Abschluss fehlt oder HTTP-Antwort unvollstaendig");
    result->success = true;
    result->error = "Sammeltransfer vollstaendig";
done:
    if (fd >= 0 && close(fd) != 0 && ops->fault) ops->fault(ops->context);
    ops->release(reader.buffer);
    ops->release(pending);
    ops->release(parser);
    ops->release(line);
#undef REQUIRE
#undef OWN
#undef IO
}
