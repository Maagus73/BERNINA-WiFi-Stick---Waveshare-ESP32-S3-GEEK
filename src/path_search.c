#include "path_search.h"
#include <string.h>
bool path_query_prepare(path_query_t *q,const char *text,uint32_t (*uppercase)(uint32_t))
{
 if(!q||!text||!*text||strlen(text)>=DAV_PATH_LIMIT||*text=='/')return false;
 bool invalid=false;
 q->basename=strchr(text,'/')==NULL;
 // Invalid FAT names remain searchable, so conflicts can be inspected.
 return dav_normalize_path(text,q->nfc,q->fat,sizeof(q->fat),uppercase,&invalid);
}
bool path_query_matches(const path_query_t *q,const dav_tree_entry_t *e)
{
 if(!*e->path)return false;
 const char *key=e->fat;
 if(q->basename){const char *slash=strrchr(key,'/');if(slash)key=slash+1;}
 return strcmp(q->fat,key)==0;
}
static int hex(unsigned char c){return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;}
bool path_query_decode(const char *encoded,char *decoded,size_t capacity)
{
 size_t n=0;
 while(*encoded){
  unsigned char c=(unsigned char)*encoded++;
  if(c=='%'){
   if(!encoded[0]||!encoded[1])return false;
   int a=hex((unsigned char)encoded[0]),b=hex((unsigned char)encoded[1]);
   if(a<0||b<0)return false;
   c=(unsigned char)(a*16+b);encoded+=2;
  }else if(c=='+')c=' ';
  if(!c||n+1>=capacity)return false;
  decoded[n++]=(char)c;
 }
 if(!capacity)return false;
 decoded[n]=0;return dav_utf8_valid(decoded);
}
uint64_t path_search_sd_only_files(const dav_tree_t *t,uint64_t *sd_files)
{
 uint64_t count=0;*sd_files=0;
 for(const dav_tree_entry_t *s DAV_AUTO=dav_tree_first(t,1);s;s=dav_tree_next(t,s))if(*s->path&&s->kind==DAV_FILE){
  ++*sd_files;bool ambiguous=false;const dav_tree_entry_t *n DAV_AUTO=dav_entry_hold(dav_tree_match(t,0,s,&ambiguous));
  if(dry_classify(t,n,s,ambiguous)==DRY_SD_ONLY)++count;
 }
 return count;
}
