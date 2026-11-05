#include "safe_update.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
static bool join(char *out,size_t size,const char *base,const char *relative)
{
 int n=snprintf(out,size,"%s/%s",base,relative);return n>=0&&(size_t)n<size;
}
void update_names(update_record_t *r,uint32_t nonce)
{
 r->version=UPDATE_VERSION;r->nonce=nonce;
 snprintf(r->stage,sizeof(r->stage),".B19-%08lx.NEW",(unsigned long)nonce);
 snprintf(r->backup,sizeof(r->backup),".B19-%08lx.BAK",(unsigned long)nonce);
 snprintf(r->displaced,sizeof(r->displaced),".B19-%08lx.OLD",(unsigned long)nonce);
 snprintf(r->journal,sizeof(r->journal),".B19-%08lx.JRN",(unsigned long)nonce);
 if(r->restore_attempt)snprintf(r->restore,sizeof(r->restore),".B19-%08lx.R%02u",(unsigned long)nonce,(unsigned)r->restore_attempt);
 else snprintf(r->restore,sizeof(r->restore),".B19-%08lx.REC",(unsigned long)nonce);
}
bool update_hash_file(const char *base,const char *relative,uint64_t size,const single_copy_ops_t *o,unsigned char hash[32])
{
 if(!single_copy_existing_target(base,relative,o))return false;
 char path[DAV_PATH_LIMIT+64];if(!join(path,sizeof(path),base,relative)||!o->owned(o->context))return false;
 int fd=open(path,O_RDONLY);if(fd<0)return false;
 unsigned char block[1024];uint64_t got=0;bool ok=o->hash_start(o->context);
 while(ok){
  if(o->cooperate)o->cooperate(o->context);
  if(!o->owned(o->context)){ok=false;break;}
  ssize_t n=read(fd,block,sizeof(block));if(n<0){ok=false;break;}if(!n)break;
  if((uint64_t)n>size-got||!o->hash_update(o->context,block,(size_t)n)){ok=false;break;}got+=(uint64_t)n;
 }
 if(o->owned(o->context)){if(close(fd))ok=false;}else return false; // quarantine handle until reboot
 return ok&&got==size&&o->hash_finish(o->context,hash);
}
static bool record_hash(const update_record_t *r,const single_copy_ops_t *o,unsigned char hash[32])
{
 return o->hash_start(o->context)&&o->hash_update(o->context,(const unsigned char *)r,offsetof(update_record_t,checksum))&&o->hash_finish(o->context,hash);
}
bool update_record_seal(update_record_t *r,const single_copy_ops_t *o){return record_hash(r,o,r->checksum);}
bool update_record_valid(const update_record_t *r,const single_copy_ops_t *o)
{
 if(r->version!=UPDATE_VERSION||r->restoring>1||r->restore_attempt>32||r->old_size>UPDATE_LIMIT||r->new_size>UPDATE_LIMIT||!memchr(r->target,0,sizeof(r->target))||
  !*r->target||!strncmp(r->target,".B19-",5)||!memchr(r->etag,0,sizeof(r->etag))||!memchr(r->modified,0,sizeof(r->modified)))return false;
 update_record_t names={.restore_attempt=r->restore_attempt};update_names(&names,r->nonce);
 if(memcmp(names.stage,r->stage,sizeof(r->stage))||memcmp(names.backup,r->backup,sizeof(r->backup))||
  memcmp(names.displaced,r->displaced,sizeof(r->displaced))||memcmp(names.journal,r->journal,sizeof(r->journal))||memcmp(names.restore,r->restore,sizeof(r->restore)))return false;
 unsigned char digest[32];return record_hash(r,o,digest)&&!memcmp(digest,r->checksum,32);
}
static bool matches(const char *base,const char *path,uint64_t size,const unsigned char expected[32],const single_copy_ops_t *o)
{
 unsigned char digest[32];return update_hash_file(base,path,size,o,digest)&&!memcmp(digest,expected,32);
}
// Copy, never rename, the old payload to an independent allocation of clusters.
static bool copy_backup(const char *base,const char *from,const char *to,uint64_t size,const unsigned char hash[32],const single_copy_ops_t *o)
{
 if(!single_copy_existing_target(base,from,o)||!single_copy_target_available(base,to,false,o))return false;
 char source[DAV_PATH_LIMIT+64],dest[DAV_PATH_LIMIT+64];
 if(!join(source,sizeof(source),base,from)||!join(dest,sizeof(dest),base,to)||!o->owned(o->context))return false;
 int in=open(source,O_RDONLY),out=-1;bool ok=in>=0;uint64_t received=0;unsigned char block[1024];
 if(ok&&o->owned(o->context)){out=open(dest,O_WRONLY|O_CREAT|O_EXCL,0600);ok=out>=0;}else ok=false;
 while(ok){
  if(o->cooperate)o->cooperate(o->context);
  if(!o->owned(o->context)){ok=false;break;}
  ssize_t n=read(in,block,sizeof(block));if(n<0){ok=false;break;}if(!n)break;
  if((uint64_t)n>size-received||!o->owned(o->context)||write(out,block,(size_t)n)!=n){ok=false;break;}received+=(uint64_t)n;
 }
 if(out>=0){if(!o->owned(o->context))return false;if(fsync(out))ok=false;if(!o->owned(o->context))return false;if(close(out))ok=false;}
 if(in>=0){if(!o->owned(o->context))return false;if(close(in))ok=false;}
 return ok&&received==size&&matches(base,to,size,hash,o);
}
static bool move_new(const char *base,const char *from,const char *to,const single_copy_ops_t *o)
{
 char a[DAV_PATH_LIMIT+64],b[DAV_PATH_LIMIT+64];
 return single_copy_existing_target(base,from,o)&&single_copy_target_available(base,to,false,o)&&
  join(a,sizeof(a),base,from)&&join(b,sizeof(b),base,to)&&o->owned(o->context)&&o->publish(o->context,a,b)==0;
}
void update_recover(const char *base,update_record_t *r,const update_ops_t *ops,update_result_t *out)
{
 const single_copy_ops_t *o=&ops->io;memset(out,0,sizeof(*out));out->armed=true;out->error="Recovery gesperrt: Journal, APP, Sicherung oder Ziel unklar";
 if(!o->owned(o->context)||!update_record_valid(r,o)||!matches(base,r->backup,r->old_size,r->old_hash,o))return;
 bool old=matches(base,r->target,r->old_size,r->old_hash,o),fresh=matches(base,r->target,r->new_size,r->new_hash,o);
 bool restore_absent=single_copy_target_available(base,r->restore,false,o);
 bool displaced_absent=single_copy_target_available(base,r->displaced,false,o);
 bool stage_absent=single_copy_target_available(base,r->stage,false,o);
 // A rename interrupted inside FatFs can leave two names for one allocation.
 // Content hashes cannot prove that these are independent cluster chains.
 if((old&&restore_absent&&(displaced_absent||r->restoring))||(fresh&&stage_absent&&restore_absent)){
  if(ops->disarm(o->context)){out->success=true;out->armed=false;out->error="Ziel gueltig; Backup erhalten; Recovery abgeschlossen";}return;
 }
 if(old||fresh){out->error="Mehrdeutige Namen nach FatFs-Unterbrechung; USB gesperrt, Dateisystem extern pruefen";return;}
 // An unexpected existing target is never overwritten, moved or deleted.
 if(!single_copy_target_available(base,r->target,false,o))return;
 if(!matches(base,r->restore,r->old_size,r->old_hash,o)){
  // Never overwrite a partial recovery copy left by a cut during its download.
  // Keep it as evidence and reserve a fresh, journal-bound recovery name.
  if(!single_copy_target_available(base,r->restore,false,o)){
   do{if(r->restore_attempt>=32){out->error="Recovery-Namenslimit; Backup erhalten, USB gesperrt";return;}r->restore_attempt++;update_names(r,r->nonce);}
   while(!single_copy_target_available(base,r->restore,false,o));
   if(!update_record_seal(r,o)||!ops->arm(o->context,r))return;
  }
  if(!o->space(o->context,r->old_size+131072)||!copy_backup(base,r->backup,r->restore,r->old_size,r->old_hash,o))return;
 }
 if(ops->checkpoint&&!ops->checkpoint(o->context,8))return;
 r->restoring=1;if(!update_record_seal(r,o)||!ops->arm(o->context,r))return;
 if(ops->checkpoint&&!ops->checkpoint(o->context,9))return;
 if(!move_new(base,r->restore,r->target,o)||!matches(base,r->target,r->old_size,r->old_hash,o))return;
 out->restored=true;
 if(ops->checkpoint&&!ops->checkpoint(o->context,10))return;
 if(!ops->disarm(o->context)){out->error="Original wiederhergestellt; NVS-Journalsperre bleibt";return;}
 out->success=true;out->armed=false;out->error="Original aus unabhaengiger Sicherung wiederhergestellt; neue Version und Backup erhalten";
}
void update_run(const char *base,update_record_t *r,const update_ops_t *ops,update_result_t *out)
{
 const single_copy_ops_t *o=&ops->io;memset(out,0,sizeof(*out));out->error="APP, Pfad, Zielhash oder Dateigroesse ungueltig";
#define CHECK(c,msg) do{if(!(c)){out->error=(msg);goto done;}}while(0)
#define CUT(n) CHECK(!ops->checkpoint||ops->checkpoint(o->context,n),"Simulierter Neustart; keine weiteren Operationen")
 CHECK(r->old_size<=UPDATE_LIMIT&&r->new_size<=UPDATE_LIMIT&&o->owned(o->context)&&strncmp(r->target,".B19-",5)&&matches(base,r->target,r->old_size,r->old_hash,o),"Bestaetigtes Original seit Vorschau veraendert / APP fehlt");
 CHECK(single_copy_target_available(base,r->stage,false,o)&&single_copy_target_available(base,r->backup,false,o)&&
  single_copy_target_available(base,r->displaced,false,o)&&single_copy_target_available(base,r->journal,false,o)&&single_copy_target_available(base,r->restore,false,o),"Backup/Temp/Journal bereits vorhanden; nichts wiederverwenden");
 CHECK(o->space(o->context,r->old_size*2+r->new_size+UPDATE_RESERVE),"Nicht genug freier Speicher fuer Download, Backup, Recovery-Kopie und 512-KiB-Reserve");
 single_copy_result_t downloaded;single_copy_ops_t transfer=*o;transfer.size_limit=UPDATE_LIMIT;
 single_copy_run(base,r->stage,r->new_size,false,&transfer,&downloaded);
 out->received=downloaded.received;out->storage_error=downloaded.storage_error;out->quarantined=downloaded.handle_quarantined;
 if(!downloaded.success&&downloaded.temporary_exists&&!downloaded.publication_attempted&&!downloaded.storage_error&&!downloaded.handle_quarantined){
  const char *name=strrchr(downloaded.temporary,'/');name=name?name+1:downloaded.temporary;
  if(strlen(name)<sizeof(out->partial)){strcpy(out->partial,name);out->partial_size=downloaded.received;out->partial_safe=true;}
 }
 CHECK(downloaded.success,downloaded.error);CHECK(!ops->content||ops->content(o->context,downloaded.sd_hash),"NAS-Inhaltspruefsumme stimmt nicht / ungueltig");memcpy(r->new_hash,downloaded.sd_hash,32);CUT(1);
 CHECK(ops->current(o->context)&&matches(base,r->target,r->old_size,r->old_hash,o),"NAS-Version oder Original seit Bestaetigung veraendert");
 if(ops->skip_identical&&r->new_size==r->old_size&&!memcmp(r->new_hash,r->old_hash,32)){out->success=true;out->error="SHA256 bestaetigt identisch; Original bleibt, Staging-Datei separat bereinigen";goto done;}
 if(!copy_backup(base,r->target,r->backup,r->old_size,r->old_hash,o)){out->storage_error=true;CHECK(false,"Unabhaengige Sicherung nicht vollstaendig verifiziert");}CUT(2);
 CHECK(update_record_seal(r,o),"Journal SHA256 fehlgeschlagen");
 char path[DAV_PATH_LIMIT+64];CHECK(join(path,sizeof(path),base,r->journal)&&o->owned(o->context),"Journalpfad/APP ungueltig");
 int journal=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);CHECK(journal>=0,"Journal bereits vorhanden oder nicht erstellbar");
 bool ok=o->owned(o->context)&&write(journal,r,sizeof(*r))==(ssize_t)sizeof(*r);
 if(o->owned(o->context)){if(fsync(journal))ok=false;if(!o->owned(o->context)){out->quarantined=true;goto done;}if(close(journal))ok=false;}else{out->quarantined=true;goto done;}
 CHECK(ok,"Journal Flush/Close fehlgeschlagen");
 unsigned char journal_hash[32];
 CHECK(o->hash_start(o->context)&&o->hash_update(o->context,(const unsigned char *)r,sizeof(*r))&&o->hash_finish(o->context,journal_hash)&&
  matches(base,r->journal,sizeof(*r),journal_hash,o),"SD-Journal Ruecklesepruefung fehlgeschlagen");CUT(3);
 CHECK(ops->arm(o->context,r),"NVS-Journal nicht dauerhaft bestaetigt; Original bleibt unberuehrt");out->armed=true;CUT(4);
 CHECK(ops->current(o->context)&&matches(base,r->target,r->old_size,r->old_hash,o)&&matches(base,r->stage,r->new_size,r->new_hash,o)&&matches(base,r->backup,r->old_size,r->old_hash,o),"Vor Ersetzung: NAS, Original, neue Version oder Backup veraendert");
 CHECK(move_new(base,r->target,r->displaced,o),"Original konnte nicht gesichert umbenannt werden; Recovery erforderlich");CUT(5);
 if(!move_new(base,r->stage,r->target,o)){
  update_result_t restored;update_recover(base,r,ops,&restored);out->armed=restored.armed;out->restored=restored.restored;
  CHECK(false,"Bereitstellung fehlgeschlagen; Original soweit moeglich wiederhergestellt; Diagnose pruefen");
 }
 CUT(6);CHECK(matches(base,r->target,r->new_size,r->new_hash,o),"Endgueltige neue Datei nicht verifiziert; Recovery erforderlich");CUT(7);
 CHECK(ops->disarm(o->context),"Neue Datei gueltig, NVS-Sperre nicht loeschbar; Recovery bestaetigen");out->armed=false;
 out->success=true;out->error="Eine Datei aktualisiert; alte Version als unabhaengiges Backup erhalten; APP aktiv";
done:
#undef CUT
#undef CHECK
 return;
}
