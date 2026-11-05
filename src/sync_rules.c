#include "sync_rules.h"
#include <string.h>
static unsigned char fold(unsigned char c){return c>='a'&&c<='z'?c-32:c;}
static size_t advance(const char *s,size_t at,size_t n){unsigned char c=(unsigned char)s[at];size_t step=c<128?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:4;return step>n-at?n:at+step;}
static bool component(const char *p,size_t pn,const char *s,size_t sn)
{
 size_t i=0,j=0,star=(size_t)-1,retry=0;
 while(j<sn){if(i<pn&&p[i]=='?'){i++;j=advance(s,j,sn);}
 else if(i<pn&&fold((unsigned char)p[i])==fold((unsigned char)s[j])){i++;j++;}
 else if(i<pn&&p[i]=='*'){star=i++;retry=j;}
 else if(star!=(size_t)-1){i=star+1;retry=advance(s,retry,sn);j=retry;}else return false;}
 while(i<pn&&p[i]=='*')i++;
 return i==pn;
}
bool sync_rules_parse(sync_rules_t *out,const char *text,size_t size)
{
 memset(out,0,sizeof(*out));if(size>SYNC_RULE_TEXT||memchr(text,0,size))return false;
 for(size_t at=0;at<size;){size_t end=at;while(end<size&&text[end]!='\n')end++;size_t a=at,b=end;at=end+1;
  while(a<b&&(text[a]==' '||text[a]=='\t'||text[a]=='\r'))a++;
  while(b>a&&(text[b-1]==' '||text[b-1]=='\t'||text[b-1]=='\r'))b--;
  if(a==b||text[a]=='#')continue;
  if(text[a]!='/'||b-a>=SYNC_RULE_LENGTH||out->count==SYNC_RULE_COUNT)return false;
  for(size_t i=a;i<b;i++)if((unsigned char)text[i]<32||(unsigned char)text[i]>126||text[i]=='['||text[i]==']'||text[i]=='\\'||text[i]==':')return false;
  size_t stop=b;if(text[b-1]=='/')stop--;
  if(stop<=a+1)return false;
  for(size_t i=a+1;i<stop;){size_t e=i;while(e<stop&&text[e]!='/')e++;size_t n=e-i;if(!n||(n==1&&text[i]=='.')||(n==2&&text[i]=='.'&&text[i+1]=='.'))return false;i=e+1;}
  if(stop>a+1&&text[stop-1]=='/')return false;
  memcpy(out->rules[out->count],text+a,b-a);out->rules[out->count++][b-a]=0;
 }
 return true;
}
/* Component-level NFA: ** consumes zero or more complete path components.
   Fixed rule-sized state arrays avoid recursion and exponential backtracking. */
static void closure(const char *p,size_t n,unsigned char *states)
{
 for(size_t i=0;i<n;i++)if(states[i]){
  size_t e=i;while(e<n&&p[e]!='/')e++;
  if(e-i==2&&p[i]=='*'&&p[i+1]=='*')states[e<n?e+1:e]=1;
 }
}
bool sync_rules_match(const sync_rules_t *rules,const char *path,bool directory)
{
 for(unsigned r=0;r<rules->count;r++){
  const char *p=rules->rules[r]+1;size_t n=strlen(p);
  bool subtree=n&&p[n-1]=='/';if(subtree)n--;
  unsigned char states[SYNC_RULE_LENGTH]={0},next[SYNC_RULE_LENGTH];states[0]=1;
  closure(p,n,states);
  const char *s=path;
  while(*s){const char *end=strchr(s,'/');if(!end)end=s+strlen(s);
   memset(next,0,sizeof(next));
   for(size_t i=0;i<n;i++)if(states[i]){
    size_t e=i;while(e<n&&p[e]!='/')e++;
    if(e-i==2&&p[i]=='*'&&p[i+1]=='*')next[i]=1;
    else if(component(p+i,e-i,s,(size_t)(end-s)))next[e<n?e+1:e]=1;
   }
   closure(p,n,next);memcpy(states,next,sizeof(states));
   if(states[n]&&(subtree?(*end=='/'||directory):(!*end&&!directory)))return true;
   if(!*end)break;
   s=end+1;
  }
 }
 return false;
}
