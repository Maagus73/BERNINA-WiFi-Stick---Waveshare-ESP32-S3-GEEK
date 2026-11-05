#ifndef DRY_RUN_H
#define DRY_RUN_H
#include "dav_tree.h"
#define DRY_EXAMPLES 4
typedef enum { DRY_NEW,DRY_CHANGED,DRY_UNKNOWN,DRY_SD_ONLY,DRY_CONFLICT,DRY_CATEGORY_COUNT,DRY_EXISTING_DIRECTORY } dry_category_t;
typedef struct {const dav_tree_entry_t *nas,*sd;} dry_example_t;
typedef struct {
 uint64_t entries,files,directories,other;
 size_t example_count;
 dry_example_t examples[DRY_EXAMPLES]; // references only; no copy of the PSRAM tree
} dry_counts_t;
typedef struct {
 dry_counts_t categories[DRY_CATEGORY_COUNT];
 uint64_t existing_directories,new_bytes,changed_bytes,unknown_bytes,possible_bytes;
 uint64_t growth_bytes,temporary_payload_bytes,unknown_sizes,ambiguous_entries,invalid_paths;
 bool complete,overflow,volumes_known,sync_allowed;
} dry_plan_t;
const char *dry_category_name(dry_category_t category);
dry_category_t dry_classify(const dav_tree_t *tree,const dav_tree_entry_t *nas,const dav_tree_entry_t *sd,bool ambiguous);
void dry_plan_release(dry_plan_t *);
void dry_plan_build(const dav_tree_t *tree,bool scan_complete,dry_plan_t *plan);
#endif
