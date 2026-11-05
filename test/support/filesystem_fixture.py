source=r'''
#include "fast_sync.h"
#include <assert.h>
#include <CommonCrypto/CommonDigest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef struct {int mode,copies,arms;bool pending;uint64_t progress;} fixture;
static unsigned baseline_lookups;
const dav_tree_entry_t *counted_find(const dav_tree_t *t,unsigned side,const char *path){if(side==1)baseline_lookups++;return mirror_find(t,side,path);}
static uint32_t upper(uint32_t c){return c>='a'&&c<='z'?c-32:c;}
static void put(const char *p,const char *s){FILE *f=fopen(p,"wb");assert(f);assert(fwrite(s,1,strlen(s),f)==strlen(s));assert(!fclose(f));}
static bool owner(void *v){return ((fixture*)v)->mode!=1;}
static bool ignored(void *v,const char *p,bool d){(void)v;(void)d;return !strcmp(p,"keep")||!strncmp(p,"keep/",5);}
static bool arm(void *v){fixture *f=v;f->arms++;if(f->mode==2)return false;f->pending=true;return true;}
static bool space(void *v,uint64_t n){(void)n;return ((fixture*)v)->mode!=3;}
static void progress(void *v,uint64_t a,uint64_t b,uint64_t c,uint64_t d){(void)a;(void)b;(void)d;((fixture*)v)->progress=c;}
static bool copy(void *v,const dav_tree_entry_t *e){fixture *f=v;if(f->mode==4)return false;char p[1200];snprintf(p,sizeof(p),"sd/%s",e->path);put(p,"new");f->copies++;return true;}
static void entry(dav_tree_t *t,unsigned side,const char *p,const char *text,bool dir){dav_entry_t e={.kind=dir?DAV_DIRECTORY:DAV_FILE,.size=text?strlen(text):0,.size_known=!dir};if(!dir){unsigned char h[32];CC_SHA256(text,(CC_LONG)strlen(text),h);e.etag[0]='"';for(int i=0;i<32;i++)sprintf(e.etag+1+i*2,"%02x",h[i]);strcat(e.etag,"\"");}assert(dav_tree_add(t,side,p,&e,upper)==DAV_OK);}
static void setup(const char *name){assert(!mkdir(name,0700)&&!chdir(name));assert(!mkdir("sd",0700)&&!mkdir("sd/keep",0700)&&!mkdir("sd/BNCACHE",0700));put("sd/keep/safe","protected");put("sd/BNCACHE/cache","cache");}
static dav_tree_t tree(void){dav_tree_t t={.budget=2000000};entry(&t,0,"",NULL,true);entry(&t,1,"",NULL,true);return t;}
static fast_sync_ops_t options(fixture *f){return (fast_sync_ops_t){.context=f,.owned=owner,.ignored=ignored,.copy=copy,.arm=arm,.space=space,.allocate=malloc,.release=free,.progress=progress};}
static void finish(dav_tree_t *t){assert(access("sd/keep/safe",F_OK)==0&&access("sd/BNCACHE/cache",F_OK)==0);dav_tree_clear(t);assert(!chdir(".."));}
int main(int argc,char **argv){assert(argc==2&&!chdir(argv[1]));
 for(int n=25;n<=26;n++){char name[32];sprintf(name,"threshold%d",n);setup(name);dav_tree_t t=tree();for(int i=0;i<n;i++){char p[32];sprintf(p,"f%03d",i);entry(&t,0,p,"new",false);}fixture f={0};fast_sync_ops_t o=options(&f);fast_sync_result_t r;fast_sync_run("sd",&t,false,25,&o,&r);assert(r.success&&r.rebuild==(n==26)&&r.differences==(unsigned)n&&r.completed==(unsigned)n&&f.copies==n&&f.pending&&r.armed);assert(r.received==(unsigned)n*3&&f.progress==r.received);finish(&t);}
 setup("early-exit");dav_tree_t many=tree();for(int i=0;i<40;i++){char path[64];sprintf(path,"f%03d",i);entry(&many,0,path,"new",false);entry(&many,1,path,"old",false);char sd[80];sprintf(sd,"sd/%s",path);put(sd,"old");}fixture many_f={0};fast_sync_ops_t many_o=options(&many_f);fast_sync_result_t many_r;baseline_lookups=0;fast_sync_run("sd",&many,true,25,&many_o,&many_r);assert(many_r.success&&many_r.rebuild&&many_f.copies==40&&baseline_lookups==26);finish(&many);
 setup("delta");dav_tree_t t=tree();entry(&t,0,"same","new",false);entry(&t,1,"same","new",false);put("sd/same","new");entry(&t,0,"changed","new",false);entry(&t,1,"changed","old",false);put("sd/changed","old");entry(&t,1,"gone","old",false);put("sd/gone","old");fixture f={0};fast_sync_ops_t o=options(&f);fast_sync_result_t r;fast_sync_run("sd",&t,true,25,&o,&r);assert(r.success&&!r.rebuild&&r.differences==2&&f.copies==1&&access("sd/gone",F_OK)!=0&&access("sd/same",F_OK)==0);finish(&t);
 setup("deletion-threshold");t=tree();for(int i=0;i<26;i++){char p[64];sprintf(p,"sd/old%03d",i);put(p,"old");}f=(fixture){0};o=options(&f);fast_sync_run("sd",&t,true,25,&o,&r);assert(r.success&&r.rebuild&&r.differences==26&&r.deleted==26);finish(&t);
 for(int mode=1;mode<=4;mode++){char name[32];sprintf(name,"fault%d",mode);setup(name);t=tree();entry(&t,0,"file","new",false);f=(fixture){.mode=mode};o=options(&f);fast_sync_run("sd",&t,false,25,&o,&r);assert(!r.success);assert((mode<=2&&!f.pending)||(mode>=3&&f.pending));assert(access("sd/file",F_OK)!=0);finish(&t);}
 setup("nested");t=tree();entry(&t,0,"a",NULL,true);entry(&t,0,"a/child",NULL,true);entry(&t,0,"a/child/file","new",false);f=(fixture){0};o=options(&f);fast_sync_run("sd",&t,false,1,&o,&r);assert(r.success&&access("sd/a/child/file",F_OK)==0);finish(&t);
 puts("PASS Test24.2 actual fast engine: exact 25/26 boundary, SHA-based same-size delta, SD-only deletion threshold, protected subtrees/cache, nested directories, ownership/arm/space/transfer failures leave incomplete transaction pending");
}
'''
