#ifndef SYNC_RULES_H
#define SYNC_RULES_H
#include <stdbool.h>
#include <stddef.h>
#define SYNC_RULE_COUNT 32
#define SYNC_RULE_LENGTH 128
#define SYNC_RULE_TEXT 8192
// Small independent policy objects: immutable for the lifetime of a plan.
typedef struct {char rules[SYNC_RULE_COUNT][SYNC_RULE_LENGTH];unsigned count;} sync_rules_t;
bool sync_rules_parse(sync_rules_t *,const char *,size_t);
bool sync_rules_match(const sync_rules_t *,const char *,bool directory);
#endif
