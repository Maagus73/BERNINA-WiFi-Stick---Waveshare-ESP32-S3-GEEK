#ifndef DAV_MEMORY_H
#define DAV_MEMORY_H
#include <stdlib.h>
#include <stddef.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
static inline void *dav_tree_allocate(size_t n){return heap_caps_calloc(1,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
static inline void dav_tree_release(void *p){heap_caps_free(p);}
static inline size_t dav_tree_platform_budget(size_t *total,size_t *available)
{
 *total=heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
 *available=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
 size_t reserve=512*1024;
 if(*available<=reserve)return 0;
 size_t budget=*available-reserve;
 if(budget>*total/4*3)budget=*total/4*3;
 if(budget>1536*1024)budget=1536*1024;
 return budget;
}
#else
static inline void *dav_tree_allocate(size_t n){return calloc(1,n);}
static inline void dav_tree_release(void *p){free(p);}
static inline size_t dav_tree_platform_budget(size_t *total,size_t *available)
{*total=*available=2*1024*1024;return 1536*1024;}
#endif
#endif
