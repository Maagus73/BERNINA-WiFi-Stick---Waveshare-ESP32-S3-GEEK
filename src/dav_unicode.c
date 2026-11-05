#include "dav_listing.h"
#include <string.h>
#include <ctype.h>
#include "unicode_nfc_data.h"
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static unsigned ccc(uint32_t c)
{
 size_t lo=0,hi=COUNT(combining_classes);
 while(lo<hi){size_t m=(lo+hi)/2;if(combining_classes[m].code<c)lo=m+1;else hi=m;}
 return lo<COUNT(combining_classes)&&combining_classes[lo].code==c?combining_classes[lo].value:0;
}
static bool expand(uint32_t c,uint32_t *out,size_t *n,unsigned depth)
{
 if(depth>16||*n>=DAV_PATH_LIMIT)return false;
 if(c>=0xAC00&&c<=0xD7A3) {
  unsigned s=c-0xAC00;
  if(!expand(0x1100+s/588,out,n,depth+1)||!expand(0x1161+(s%588)/28,out,n,depth+1))return false;
  return !(s%28)||expand(0x11A7+s%28,out,n,depth+1);
 }
 size_t lo=0,hi=COUNT(decompositions);
 while(lo<hi){size_t m=(lo+hi)/2;if(decompositions[m].code<c)lo=m+1;else hi=m;}
 if(lo<COUNT(decompositions)&&decompositions[lo].code==c) {
  if(!expand(decompositions[lo].a,out,n,depth+1))return false;
  return !decompositions[lo].b||expand(decompositions[lo].b,out,n,depth+1);
 }
 size_t i=(*n)++;unsigned cl=ccc(c);
 while(i&&cl&&ccc(out[i-1])>cl){out[i]=out[i-1];i--;}
 out[i]=c;return true;
}
static uint32_t compose(uint32_t a,uint32_t b)
{
 if(a>=0x1100&&a<=0x1112&&b>=0x1161&&b<=0x1175)return 0xAC00+(a-0x1100)*588+(b-0x1161)*28;
 if(a>=0xAC00&&a<=0xD7A3&&!((a-0xAC00)%28)&&b>=0x11A8&&b<=0x11C2)return a+b-0x11A7;
 size_t lo=0,hi=COUNT(compositions);
 while(lo<hi){size_t m=(lo+hi)/2;if(compositions[m].a<a||(compositions[m].a==a&&compositions[m].b<b))lo=m+1;else hi=m;}
 return lo<COUNT(compositions)&&compositions[lo].a==a&&compositions[lo].b==b?compositions[lo].code:0;
}
static bool encode(uint32_t c,char *out,size_t *n,size_t cap)
{
 unsigned bytes=c<128?1:c<2048?2:c<65536?3:4;
 if(*n+bytes>=cap)return false;
 if(bytes==1)out[(*n)++]=(char)c;
 else {out[*n]=(char)((bytes==2?0xC0:bytes==3?0xE0:0xF0)|(c>>(6*(bytes-1))));
  for(unsigned i=1;i<bytes;i++)out[*n+i]=(char)(0x80|((c>>(6*(bytes-i-1)))&63));
  *n+=bytes;
 }
 return true;
}
bool dav_normalize_path(const char *path,char *nfc,char *fat,size_t cap,uint32_t (*upper)(uint32_t),bool *invalid)
{
 if(!dav_utf8_valid(path)||strlen(path)>=DAV_PATH_LIMIT)return false;
 uint32_t points[DAV_PATH_LIMIT];size_t count=0;
 const unsigned char *p=(const unsigned char *)path;
 while(*p) {
  uint32_t c=*p++;unsigned more=0;
  if(c>=0xF0){c&=7;more=3;}else if(c>=0xE0){c&=15;more=2;}else if(c>=0xC0){c&=31;more=1;}
  while(more--)c=(c<<6)|(*p++&63);
  if(!expand(c,points,&count,0))return false;
 }
 size_t used=0,starter=0;unsigned previous=0;
 for(size_t i=0;i<count;i++) {
  uint32_t c=points[i];unsigned cl=ccc(c);uint32_t combined=used?compose(points[starter],c):0;
  if(combined&&(previous<cl||previous==0))points[starter]=combined;
  else {if(!cl)starter=used;points[used++]=c;previous=cl;}
 }
 size_t a=0,b=0;*invalid=false;size_t component=0;unsigned utf16=0;
 for(size_t i=0;i<used;i++) {
  uint32_t c=points[i];
  if(c=='/') {
   if(!component||points[i-1]=='.'||points[i-1]==' ')*invalid=true;
   component=0;utf16=0;
  } else {
   component++;utf16+=c>0xFFFF?2:1;
   if(utf16>255||c<32||c==127||c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|')*invalid=true;
  }
  if(!encode(c,nfc,&a,cap)||!encode(upper(c),fat,&b,cap))return false;
 }
 if(used&&(points[used-1]=='.'||points[used-1]==' '))*invalid=true;
 nfc[a]=fat[b]=0;
 // Never resolve dot/empty components or an absolute path across the root.
 for(const char *part=nfc;*part;) {
  const char *end=strchr(part,'/');size_t len=end?(size_t)(end-part):strlen(part);
  if(!len||(len==1&&part[0]=='.')||(len==2&&part[0]=='.'&&part[1]=='.'))return false;
  if(!end)break;
  if(!end[1])return false;
  part=end+1;
 }
 return true;
}
