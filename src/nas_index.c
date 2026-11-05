#include "nas_index.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
static int hex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;}
static bool token(const char *s){if(strlen(s)!=64)return false;for(;*s;s++)if(hex(*s)<0)return false;return true;}
static bool number(const char *s,uint64_t *v){if(!*s)return false;for(const char *p=s;*p;p++)if(*p<'0'||*p>'9')return false;errno=0;char *end;unsigned long long n=strtoull(s,&end,10);if(errno||*end)return false;*v=n;return true;}
static bool line(nas_index_parser_t *p)
{
 char *s=p->line;
 if(p->ended)return false;
 if(!p->header){if(strncmp(s,"BINDEX1\t",8)||!token(s+8))return false;strcpy(p->token,s+8);p->header=true;return true;}
 if(!strncmp(s,"END\t",4)){
  char *tab=strchr(s+4,'\t');uint64_t count;if(!tab)return false;*tab++=0;
  if(!number(s+4,&count)||count!=p->count||strcmp(tab,p->token))return false;
  p->ended=true;return true;
 }
 char *size=strchr(s,'\t');if(!size)return false;*size++=0;
 char *hash=strchr(size,'\t');if(!hash)return false;*hash++=0;
 char *encoded=strchr(hash,'\t');if(!encoded)return false;*encoded++=0;
 dav_entry_t e={0};
 if(!strcmp(s,"F"))e.kind=DAV_FILE;else if(!strcmp(s,"D"))e.kind=DAV_DIRECTORY;else return false;
 if(!number(size,&e.size))return false;
 if(e.kind==DAV_FILE){if(!token(hash))return false;e.size_known=true;snprintf(e.etag,sizeof(e.etag),"\"%s\"",hash);}
 else if(e.size||strcmp(hash,"-"))return false;
 char path[DAV_PATH_LIMIT];size_t n=0;
 for(size_t i=0;encoded[i];i++){
  unsigned char c=(unsigned char)encoded[i];
  if(c=='%'){if(!encoded[i+1]||!encoded[i+2])return false;int a=hex(encoded[i+1]),b=hex(encoded[i+2]);if(a<0||b<0)return false;c=(unsigned char)((a<<4)|b);i+=2;}
  if(c<32||c==127||c=='\\'||n+1>=sizeof(path))return false;
  path[n++]=(char)c;
 }
 path[n]=0;if(!n||path[0]=='/'||path[n-1]=='/'||!dav_utf8_valid(path))return false;
 for(char *part=path;;){char *end=strchr(part,'/');size_t len=end?(size_t)(end-part):strlen(part);if(!len||len>255||(len==1&&part[0]=='.')||(len==2&&part[0]=='.'&&part[1]=='.'))return false;if(!end)break;part=end+1;}
 const char *name=strrchr(path,'/');name=name?name+1:path;strcpy(e.name,name);
 if(!p->emit(p->context,path,&e))return false;
 p->count++;return true;
}
void nas_index_init(nas_index_parser_t *p,nas_index_emit_t emit,void *context){memset(p,0,sizeof(*p));p->emit=emit;p->context=context;}
bool nas_index_feed(nas_index_parser_t *p,const char *s,size_t n)
{
 if(p->failed||!p->emit)return false;
 for(size_t i=0;i<n;i++){
  if(s[i]=='\n'){p->line[p->used]=0;if(!line(p)){p->failed=true;return false;}p->used=0;}
  else{if(!s[i]||p->used+1>=sizeof(p->line)){p->failed=true;return false;}p->line[p->used++]=s[i];}
 }
 return true;
}
bool nas_index_finish(const nas_index_parser_t *p){return p->header&&p->ended&&!p->failed&&!p->used;}
