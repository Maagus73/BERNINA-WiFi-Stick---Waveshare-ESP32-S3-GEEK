#include "single_copy.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#ifdef ESP_PLATFORM
// This engine is used only on the MSC FatFs volume, which has no symlinks.
#define lstat stat
#endif
// Scan every actual directory component too: stale snapshots must never create
// a second NFC/FAT spelling, traverse a symlink or replace any existing object.
static int inspect(const char *parent,const char *name,int directory,const single_copy_ops_t *o)
{
 char path[DAV_PATH_LIMIT+64],fat[DAV_PATH_LIMIT],key[DAV_PATH_LIMIT];bool invalid;
 if(!dav_normalize_path(name,path,fat,sizeof(fat),o->uppercase,&invalid)||invalid)return -1;
 DIR *d=opendir(parent);if(!d)return -1;
 int found=0;struct dirent *e;
 for(;;){
  errno=0;e=readdir(d);if(!e){if(errno)found=-1;break;}
  if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
  if(!dav_normalize_path(e->d_name,path,key,sizeof(key),o->uppercase,&invalid)){found=-1;break;}
  if(strcmp(key,fat))continue;
  if(found||!directory||strcmp(e->d_name,name)){found=-1;break;}
  struct stat st;
  int n=snprintf(path,sizeof(path),"%s/%s",parent,e->d_name);
  if(n<0||(size_t)n>=sizeof(path)||lstat(path,&st)||(directory==1?!S_ISDIR(st.st_mode):!S_ISREG(st.st_mode))){found=-1;break;}
  found=1;
 }
 if(closedir(d))found=-1;
 return found;
}
static bool parents_mode(const char *base,const char *relative,bool allow,bool create,int final_kind,const single_copy_ops_t *o)
{
 char path[DAV_PATH_LIMIT+64];size_t n=strlen(base);if(n>=sizeof(path))return false;memcpy(path,base,n+1);
 const char *p=relative;
 while(strchr(p,'/')){
  const char *end=strchr(p,'/');size_t len=(size_t)(end-p);char component[DAV_PATH_LIMIT];
  memcpy(component,p,len);component[len]=0;
  if(!o->owned(o->context))return false;
  int state=inspect(path,component,true,o);
  if(state<0)return false;
  if(!state&&!allow)return false;
  if(n+len+2>sizeof(path))return false;
  path[n++]='/';memcpy(path+n,component,len+1);n+=len;
  if(!state){
   if(!create)return true; // Rest cannot yet exist beneath an absent directory.
   if(!o->owned(o->context)||mkdir(path,0777))return false;
  }
  p=end+1;
 }
 return o->owned(o->context)&&inspect(path,p,final_kind,o)==(final_kind?1:0);
}
static bool parents(const char *base,const char *relative,bool allow,bool create,const single_copy_ops_t *o)
{
 return parents_mode(base,relative,allow,create,0,o);
}
static __attribute__((noinline)) bool valid_relative(const char *relative,bool allow_reserved,const single_copy_ops_t *o)
{
 char nfc[DAV_PATH_LIMIT],fat[DAV_PATH_LIMIT];bool invalid;
 return relative&&*relative&&*relative!='/'&&dav_normalize_path(relative,nfc,fat,sizeof(fat),o->uppercase,&invalid)&&
  !invalid&&(allow_reserved||strncmp(fat,".B16-",5));
}
static bool valid_target(const char *relative,const single_copy_ops_t *o){return valid_relative(relative,false,o);}
bool single_copy_path_state(const char *base,const char *relative,int kind,bool allow_reserved,const single_copy_ops_t *o)
{
 return kind>=0&&kind<=2&&valid_relative(relative,allow_reserved,o)&&parents_mode(base,relative,false,false,kind,o);
}
bool single_copy_target_available(const char *base,const char *relative,bool allow,const single_copy_ops_t *o)
{
 // Complete normalization before entering the directory scan; reuse stack space.
 return valid_target(relative,o)&&parents(base,relative,allow,false,o);
}
bool single_copy_existing_target(const char *base,const char *relative,const single_copy_ops_t *o)
{
 return valid_target(relative,o)&&parents_mode(base,relative,false,false,2,o);
}
void single_copy_run(const char *base,const char *relative,uint64_t size,bool allow,const single_copy_ops_t *o,single_copy_result_t *r)
{
 memset(r,0,sizeof(*r));r->error="Ungueltiger Pfad oder Dateigroesse";
 char nfc[DAV_PATH_LIMIT],fat[DAV_PATH_LIMIT];bool invalid;
 if(!relative||!*relative||*relative=='/'||size>(o->size_limit?o->size_limit:SINGLE_COPY_LIMIT)||
    !dav_normalize_path(relative,nfc,fat,sizeof(fat),o->uppercase,&invalid)||invalid||!strncmp(fat,".B16-",5))return;
 char final[DAV_PATH_LIMIT+64];int n=snprintf(final,sizeof(final),"%s/%s",base,relative);
 if(n<0||(size_t)n>=sizeof(final))return;
 int fd=-1,verify=-1;unsigned char data[1024];
#define REQUIRE(condition,message) do{if(!(condition)){r->error=(message);goto done;}}while(0)
#define REQUIRE_IO(condition,message) do{if(!(condition)){r->storage_error=true;r->error=(message);goto done;}}while(0)
#define OWN() REQUIRE(o->owned(o->context),"APP-Besitz verweigert")
 OWN();REQUIRE(parents(base,relative,allow,false,o),"Ziel vorhanden, Pfad-/FAT-Konflikt oder Ordnerfreigabe fehlt");
 REQUIRE(o->space(o->context,size+65536),"Nicht ausreichend freier SD-Speicher (inkl. 64 KiB Reserve)");
 REQUIRE(o->begin(o->context,size),"HTTPS-Status, Laenge oder NAS-Vorbedingung ungueltig");
 REQUIRE(o->hash_start(o->context),"SHA256 Initialisierung");
 OWN();REQUIRE(!o->validate||o->validate(o->context),"Ziel seit Vorschau veraendert; erneut vergleichen");r->mutated=true;
 REQUIRE(parents(base,relative,allow,true,o),"Elternpfad konnte nicht sicher angelegt/geprueft werden");
 // Never reuse a pre-existing temporary file; no cleanup/deletion, including on failure.
 for(unsigned i=0;i<32;i++){
  snprintf(r->temporary,sizeof(r->temporary),"%s/.B16-%08lx-%02u.TMP",base,(unsigned long)o->nonce,i);
  OWN();fd=open(r->temporary,O_WRONLY|O_CREAT|O_EXCL,0600);
  if(fd>=0)break;
  if(errno!=EEXIST)break;
 }
 REQUIRE(fd>=0,"Exklusive temporaere Datei konnte nicht erstellt werden");r->temporary_exists=true;
 for(;;){
  if(o->cooperate)o->cooperate(o->context);
  OWN();int got=o->read(o->context,data,sizeof(data));REQUIRE(got>=0,"HTTPS-Abbruch");if(!got)break;
  REQUIRE((uint64_t)got<=size-r->received,"Antwort groesser als ausgewaehlte NAS-Datei");
  REQUIRE(o->hash_update(o->context,data,(size_t)got),"Download-SHA256");
  OWN();REQUIRE_IO(write(fd,data,(size_t)got)==got,"SD-Schreibfehler");r->received+=(unsigned)got;
  if(o->progress)o->progress(o->context,r->received,size,false);
 }
 REQUIRE(o->complete(o->context)&&r->received==size,"HTTP-Antwort unvollstaendig oder Dateigroesse abweichend");
 REQUIRE(o->hash_finish(o->context,r->download_hash),"Download-SHA256 Abschluss");
 OWN();REQUIRE_IO(fsync(fd)==0,"SD-Flush fehlgeschlagen");
 OWN();{int closing=fd;fd=-1;REQUIRE_IO(close(closing)==0,"SD-Close fehlgeschlagen");}
 if(o->progress)o->progress(o->context,0,size,true);
 OWN();verify=open(r->temporary,O_RDONLY);REQUIRE(verify>=0,"SD-Pruefdatei nicht lesbar");
 REQUIRE(o->hash_start(o->context),"SD-SHA256 Initialisierung");
 for(;;){
  if(o->cooperate)o->cooperate(o->context);
  OWN();ssize_t got=read(verify,data,sizeof(data));REQUIRE_IO(got>=0,"SD-Lesefehler");if(!got)break;
  REQUIRE((uint64_t)got<=size-r->verified,"SD-Pruefdatei zu gross");
  REQUIRE(o->hash_update(o->context,data,(size_t)got),"SD-SHA256");r->verified+=(unsigned)got;
  if(o->progress)o->progress(o->context,r->verified,size,true);
 }
 OWN();{int closing=verify;verify=-1;REQUIRE_IO(close(closing)==0,"SD-Pruefdatei Close");}
 REQUIRE_IO(o->hash_finish(o->context,r->sd_hash)&&r->verified==size&&!memcmp(r->sd_hash,r->download_hash,32),"SD-SHA256 weicht vom Download ab");
 OWN();REQUIRE(parents(base,relative,false,false,o),"Ziel/Pfad unmittelbar vor Veroeffentlichung veraendert");
 REQUIRE(!o->verify_content||o->verify_content(o->context,r->download_hash),"NAS-SHA256 verweigert");
 REQUIRE(!o->before_publish||o->before_publish(o->context),"NAS-Version vor Bereitstellung veraendert");
 OWN();r->publication_attempted=true;REQUIRE(o->publish(o->context,r->temporary,final)==0,"Veroeffentlichung verweigert; kein Ueberschreiben");
 r->temporary_exists=false;r->success=true;r->error="OK: SHA256 Download gegen SD; keine unabhaengige NAS-Pruefsumme";
done:
 // f_close on FAT can sync metadata: do not invoke it after loss of APP ownership.
 if(fd>=0){if(o->owned(o->context)){if(close(fd)){r->success=false;r->storage_error=true;}}else r->handle_quarantined=true;}
 if(verify>=0){if(o->owned(o->context)){if(close(verify)){r->success=false;r->storage_error=true;}}else r->handle_quarantined=true;}
#undef OWN
#undef REQUIRE
#undef REQUIRE_IO
}
