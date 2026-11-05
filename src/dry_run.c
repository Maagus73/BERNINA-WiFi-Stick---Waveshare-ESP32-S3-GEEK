#include "dry_run.h"
#include <stdint.h>
#include <string.h>
const char *dry_category_name(dry_category_t c)
{
 static const char *names[]={"NEU","GEAENDERT / VERDAECHTIG","UNGEKLAERT","NUR AUF SD","KONFLIKT"};
 return (unsigned)c<DRY_CATEGORY_COUNT?names[c]:"VORHANDENER ORDNER (keine Aktion)";
}
dry_category_t dry_classify(const dav_tree_t *t,const dav_tree_entry_t *n,const dav_tree_entry_t *s,bool ambiguous)
{
 if((!n&&!s)||ambiguous||(n&&(n->conflict||n->invalid_fat||n->kind==DAV_OTHER||dav_tree_ancestor_conflict(t,n)))||
    (s&&(s->conflict||s->invalid_fat||s->kind==DAV_OTHER||dav_tree_ancestor_conflict(t,s)))||
    (n&&s&&n->kind!=s->kind))return DRY_CONFLICT;
 if(!n)return DRY_SD_ONLY;
 if(!s)return DRY_NEW;
 if(n->kind==DAV_DIRECTORY)return DRY_EXISTING_DIRECTORY;
 if(n->size_known&&s->size_known&&n->size!=s->size)return DRY_CHANGED;
 // ETag has no SD equivalent; FAT time is not an independent content proof.
 return DRY_UNKNOWN;
}
static void add(dry_plan_t *p,uint64_t *sum,uint64_t value)
{
 if(UINT64_MAX-*sum<value){p->overflow=true;return;}
 *sum+=value;
}
static void account(dry_plan_t *p,const dav_tree_t *t,const dav_tree_entry_t *n,const dav_tree_entry_t *s,bool ambiguous)
{
 dry_category_t category=dry_classify(t,n,s,ambiguous);
 if(category==DRY_EXISTING_DIRECTORY){add(p,&p->existing_directories,1);return;}
 dry_counts_t *count=&p->categories[category];
 add(p,&count->entries,1);
 if((n&&n->kind==DAV_FILE)||(s&&s->kind==DAV_FILE))add(p,&count->files,1);
 if((n&&n->kind==DAV_DIRECTORY)||(s&&s->kind==DAV_DIRECTORY))add(p,&count->directories,1);
 if((n&&n->kind==DAV_OTHER)||(s&&s->kind==DAV_OTHER))add(p,&count->other,1);
 if(count->example_count<DRY_EXAMPLES)count->examples[count->example_count++]=(dry_example_t){dav_entry_hold(n),dav_entry_hold(s)};
 if((!n&&!s)||ambiguous||(n&&n->conflict)||(s&&s->conflict))add(p,&p->ambiguous_entries,1);
 if((n&&n->invalid_fat)||(s&&s->invalid_fat))add(p,&p->invalid_paths,1);
 if(!n||n->kind!=DAV_FILE||category==DRY_CONFLICT)return;
 if(category!=DRY_NEW&&category!=DRY_CHANGED&&category!=DRY_UNKNOWN)return;
 if(!n->size_known){add(p,&p->unknown_sizes,1);return;}
 uint64_t *download=category==DRY_NEW?&p->new_bytes:category==DRY_CHANGED?&p->changed_bytes:&p->unknown_bytes;
 add(p,download,n->size);
 add(p,&p->temporary_payload_bytes,n->size);
 if(category==DRY_NEW)add(p,&p->growth_bytes,n->size);
 else if(!s||!s->size_known)add(p,&p->unknown_sizes,1);
 else if(n->size>s->size)add(p,&p->growth_bytes,n->size-s->size);
}
void dry_plan_build(const dav_tree_t *t,bool complete,dry_plan_t *p)
{
 memset(p,0,sizeof(*p));
 for(const dav_tree_entry_t *n DAV_AUTO=dav_tree_first(t,0);n;n=dav_tree_next(t,n))if(*n->path) {
  bool ambiguous=false;const dav_tree_entry_t *s DAV_AUTO=dav_entry_hold(dav_tree_match(t,1,n,&ambiguous));
  account(p,t,n,ambiguous?NULL:s,ambiguous);
 }
 for(const dav_tree_entry_t *s DAV_AUTO=dav_tree_first(t,1);s;s=dav_tree_next(t,s))if(*s->path) {
  bool ambiguous=false;const dav_tree_entry_t *n DAV_AUTO=dav_entry_hold(dav_tree_match(t,0,s,&ambiguous));
  if(!n||ambiguous)account(p,t,NULL,s,ambiguous);
 }
 add(p,&p->possible_bytes,p->changed_bytes);add(p,&p->possible_bytes,p->unknown_bytes);
 p->complete=complete&&!p->overflow&&dav_tree_healthy(t);
 p->volumes_known=p->complete&&!p->unknown_sizes;
 p->sync_allowed=false; // Test 15 is permanently read-only, even on an empty plan.
}

void dry_plan_release(dry_plan_t *p){for(unsigned c=0;c<DRY_CATEGORY_COUNT;c++)for(unsigned i=0;i<p->categories[c].example_count;i++){dav_entry_release(p->categories[c].examples[i].nas);dav_entry_release(p->categories[c].examples[i].sd);}memset(p,0,sizeof(*p));}
