#include "fast_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
/* One directory handle, bounded path queue; no recursive C stack or open-parent chain. */
typedef struct folder {
    struct folder * next, * prev, * parent;
    bool retained;
    char path [ ];
} folder;
static bool ignored(const fast_sync_ops_t * o, const char * p, bool d) {
    return ! strcasecmp(p, "BNCACHE") || ! strncasecmp(p, "BNCACHE/", 8) ||(o -> ignored && o -> ignored(o -> context, p, d));
}
static bool absolute(char * out, size_t cap, const char * base, const char * path) {
    int n = snprintf(out, cap, "%s/%s", base, path);
    return n >= 0 &&(size_t) n < cap;
}
static int file_info(const char * path, struct stat * out) {
#ifdef ESP_PLATFORM
    return stat(path, out);
    /* FAT has no symlinks. */
#else
    return lstat(path, out);
#endif
}
static bool walk_failure(const fast_sync_ops_t *o,fast_sync_result_t *r,const char *path,const char *operation)
{
 int code=errno;r->error=operation;
 if(o->failure)o->failure(o->context,path,operation,code);
 return false;
}
static bool walk(const char * base, dav_tree_t * tree, bool deleting, bool all, unsigned limit, const fast_sync_ops_t * o, fast_sync_result_t * r) {
    folder * head = o -> allocate(sizeof(folder) + 1), * tail = head;
    size_t budget = 0;
    bool ok = head != NULL;
    if (! head) return walk_failure(o,r,"","SD-Verzeichniswarteschlange: Speicher fehlt");
    head -> next = head -> prev = head -> parent = NULL;
    head -> retained = false;
    head -> path [ 0 ] = 0;
    uint64_t visited=0;
    for (folder * node = head; node && ok; node = node -> next) {
        char full [ DAV_PATH_LIMIT + 64 ];
        if (! o -> owned(o -> context) || ! absolute(full, sizeof(full), base, node -> path)) {
            ok = walk_failure(o,r,node->path,"SD-Bereinigung: APP oder Pfad ungueltig");
            break;
        }
        DIR * dir = opendir(full);
        if (! dir) {
            ok = walk_failure(o,r,node->path,"SD-Verzeichnis nicht geoeffnet");
            break;
        }
        struct dirent * entry;
        for (;;) {
            errno = 0;
            entry = readdir(dir);
            if (! entry) {
                if (errno) ok = walk_failure(o,r,node->path,"SD-Verzeichnis lesen fehlgeschlagen");
                break;
            }
            if (! strcmp(entry -> d_name, ".") || ! strcmp(entry -> d_name, "..")) continue;
            if (o -> cooperate) o -> cooperate(o -> context);
            visited++;if(o->scan&&(visited%128==0))o->scan(o->context,deleting?"SD bereinigen":"SD-only-Dateien zaehlen",visited,0);
            char path [ DAV_PATH_LIMIT ];
            int n = snprintf(path, sizeof(path), "%s%s%s", node -> path, * node -> path ? "/" : "", entry -> d_name);
            struct stat st;
            if (n < 0 ||(size_t) n >= sizeof(path) || ! o -> owned(o -> context) || ! absolute(full, sizeof(full), base, path) || file_info(full, & st)) {
                ok = walk_failure(o,r,path,"SD-Eintrag pruefen: APP/Pfad/Stat fehlgeschlagen");
                break;
            }
            bool directory = S_ISDIR(st . st_mode);
            if (! directory && ! S_ISREG(st . st_mode)) {
                ok = false;
                break;
            }
            if (ignored(o, path, directory)) {node->retained=true;continue;}
            if (directory) {
                size_t size = sizeof(folder) + strlen(path) + 1;
                if (budget + size > 131072) {
                    ok = walk_failure(o,r,path,"SD-Verzeichniswarteschlange: Budget erschoepft");
                    break;
                }
                folder * f = o -> allocate(size);
                if (! f) {
                    ok = walk_failure(o,r,path,"SD-Verzeichniswarteschlange: Speicher fehlt");
                    break;
                }
                budget += size;
                f -> parent = node;
                f -> retained = false;
                f -> prev = tail;
                f -> next = NULL;
                strcpy(f -> path, path);
                tail -> next = f;
                tail = f;
            } else {
                const dav_tree_entry_t * nas DAV_AUTO = deleting && (all||o->present) ? NULL : dav_entry_hold(mirror_find(tree, 0, path));
                int present=o->present?o->present(o->context,path,false):(nas&&nas->kind==DAV_FILE);
                if(present<0){ok=walk_failure(o,r,path,"SD-Zuordnung ungueltig");break;}
                bool extra = !present;
                if (! deleting && extra && ++ r -> differences > limit) {
                    r -> rebuild = true;
                    break;
                }
                if (deleting &&(all || extra)) {
                    if (! o -> owned(o -> context) || unlink(full)) {
                        ok = walk_failure(o,r,path,"SD-Datei entfernen fehlgeschlagen");
                        break;
                    }
                    r -> deleted ++;
                }
            }
        }
        if (closedir(dir)) { if(o->fault)o->fault(o->context); ok=walk_failure(o,r,node->path,"SD-Verzeichnis schliessen fehlgeschlagen"); }
        if (! deleting && r -> rebuild) break;
    }
    if (deleting && ok) for (folder * f = tail; f && f != head; f = f -> prev) {
        if(f->retained){if(f->parent)f->parent->retained=true;continue;}
        const dav_tree_entry_t * nas DAV_AUTO = (all||o->present) ? NULL : dav_entry_hold(mirror_find(tree, 0, f -> path));
        int present=o->present?o->present(o->context,f->path,true):(nas&&nas->kind==DAV_DIRECTORY);
        if(present<0){ok=walk_failure(o,r,f->path,"SD-Ordnerzuordnung ungueltig");break;}
        if (!all&&present)continue;
        char full [ DAV_PATH_LIMIT + 64 ];
        if (! o -> owned(o -> context) || ! absolute(full, sizeof(full), base, f -> path)) {
            ok = false;
            break;
        }
        if (rmdir(full) && errno != ENOTEMPTY && errno != EEXIST) {
            ok = walk_failure(o,r,f->path,"SD-Ordner entfernen fehlgeschlagen");
            break;
        }
    }
    while (head) {
        folder * next = head -> next;
        o -> release(head);
        head = next;
    }
    return ok;
}
void fast_sync_run(const char * base, dav_tree_t * tree, bool baseline, unsigned limit, const fast_sync_ops_t * o, fast_sync_result_t * r) {
    memset(r, 0, sizeof(* r));
    r -> error = "NAS-Bestand oder APP ungueltig";
    if (! o -> owned(o -> context) || ! dav_tree_healthy(tree) || ! tree -> count [ 0 ]) return;
    /* Single sequential NAS preflight. Every ancestor is itself an entry and is
       validated here; nas_index_compare has already proved directory ancestry.
       No random ancestor reads and no persistent per-file flags on full rebuild. */
    uint64_t all_files=0, all_bytes=0, checked=0;
    if(o->phase)o->phase(o->context,"NAS-Liste validieren und Umfang bestimmen");
    for(dav_tree_entry_t *e DAV_AUTO=dav_tree_first(tree,0);e;e=dav_tree_next(tree,e)){
        if(o->cooperate)o->cooperate(o->context);
        if(!o->owned(o->context)){r->error="APP-Besitz verloren";return;}
        if(!strncmp(e->fat,".B16-",5)||!strncmp(e->fat,".B19-",5)||!strncmp(e->fat,".B20-",5)||e->conflict||e->invalid_fat){r->error="FAT-/Pfadkonflikt";return;}
        if(e->kind==DAV_FILE){
            if(!e->size_known||e->size>2147483647ULL||UINT64_MAX-all_bytes<e->size){r->error="Dateigroesse ungueltig";return;}
            all_files++;all_bytes+=e->size;
        }
        checked++;
        if(o->scan&&(checked%128==0||checked==tree->count[0]))o->scan(o->context,"NAS-Liste validieren",checked,tree->count[0]);
    }
    if(!dav_tree_healthy(tree)){r->error="Index-Lesefehler";return;}
    if(!baseline&&all_files>limit){r->rebuild=true;r->differences=(uint64_t)limit+1;}
    else {
        if(o->phase)o->phase(o->context,"Dateiaenderungen zaehlen; Abbruch an Vollaufbau-Schwelle");
        checked=0;
        for(dav_tree_entry_t *e DAV_AUTO=dav_tree_first(tree,0);e;e=dav_tree_next(tree,e)){
            if(o->cooperate)o->cooperate(o->context);
            if(e->kind!=DAV_FILE)continue;
            const dav_tree_entry_t *old DAV_AUTO=baseline?dav_entry_hold(mirror_find(tree,1,e->path)):NULL;
            bool changed=!old||old->kind!=DAV_FILE||old->size!=e->size||strcmp(old->etag,e->etag);
            mirror_meta(e)->done=changed;
            if(changed){r->differences++;r->files++;r->bytes+=e->size;}
            if(r->differences>limit){r->rebuild=true;break;}
            checked++;
            if(o->scan&&(checked%128==0||checked==all_files))o->scan(o->context,"Dateiaenderungen zaehlen",checked,all_files);
        }
    }
    if(!r->rebuild){
        if(o->phase)o->phase(o->context,"SD-only-Dateien zaehlen");
        r->error="SD-Verzeichnispruefung fehlgeschlagen";
        if(!walk(base,tree,false,false,limit,o,r))return;
    }
    if(!dav_tree_healthy(tree)){r->error="Index-Lesefehler";return;}
    if(r->rebuild){r->files=all_files;r->bytes=all_bytes;}
    r -> error = "Sync-Journal konnte nicht bestaetigt werden";
    if (! o -> arm(o -> context)) return;
    r -> armed = true;
    if (o -> phase) o -> phase(o -> context, r -> rebuild ? "Vollaufbau: SD-Nutzdaten entfernen" : "Delta: entfernte NAS-Pfade auf SD bereinigen");
    r -> error = "SD-Bereinigung fehlgeschlagen";
    if (! walk(base, tree, true, r -> rebuild, limit, o, r)) return;
    r -> error = "SD-Speicher reicht nicht";
    if (! o -> space(o -> context, r -> bytes + 65536)) return;
    if(o->phase)o->phase(o->context,"NAS-Ordner anlegen");
    uint64_t made=0;
    /* Parent directory creation follows the fully validated, lexical NAS listing. */
    for (dav_tree_entry_t * e DAV_AUTO = dav_tree_first(tree, 0); e; e = dav_tree_next(tree, e)) if (e -> kind == DAV_DIRECTORY && * e -> path) {
        char full [ DAV_PATH_LIMIT + 64 ];
        if (! o -> owned(o -> context) || ! absolute(full, sizeof(full), base, e -> path)) {
            r -> error = "APP/Pfad ungueltig";
            return;
        }
        if(o->cooperate)o->cooperate(o->context);
        if (mkdir(full, 0700)) {
            struct stat st;
            if (errno != EEXIST || stat(full, & st) || ! S_ISDIR(st . st_mode)) {
                r -> error = "NAS-Ordner konnte nicht erstellt werden";
                return;
            }
        }
        made++;
        if(o->scan)o->scan(o->context,"NAS-Ordner anlegen",made,tree->directories[0]-1);
    }
    if (o -> phase) o -> phase(o -> context, r -> rebuild ? "Vollaufbau: NAS-Dateien kopieren" : "Delta: geaenderte NAS-Dateien kopieren");
    for (dav_tree_entry_t * e DAV_AUTO = dav_tree_first(tree, 0); e; e = dav_tree_next(tree, e)) if (e -> kind == DAV_FILE &&(r -> rebuild || mirror_meta(e) -> done)) {
        if (o -> progress) o -> progress(o -> context, r -> completed, r -> files, r -> received, r -> bytes);
        r -> error = "Dateitransfer fehlgeschlagen";
        if (! o -> owned(o -> context) || ! o -> copy(o -> context, e)) return;
        r -> completed ++;
        r -> received += e -> size;
    }
    if (! dav_tree_healthy(tree)) {
        r -> error = "Index nach Transfer ungueltig";
        return;
    }
    if (o -> progress) o -> progress(o -> context, r -> completed, r -> files, r -> received, r -> bytes);
    r -> success = true;
    r -> error = "NAS auf SD uebernommen";
}

bool fast_sync_clear(const char *base,const fast_sync_ops_t *ops,fast_sync_result_t *result){dav_tree_t unused={0};return ops->owned(ops->context)&&walk(base,&unused,true,true,0,ops,result);}

bool fast_sync_reconcile(const char *base,const fast_sync_ops_t *ops,fast_sync_result_t *result){dav_tree_t unused={0};return ops->present&&ops->owned(ops->context)&&walk(base,&unused,true,false,0,ops,result);}
