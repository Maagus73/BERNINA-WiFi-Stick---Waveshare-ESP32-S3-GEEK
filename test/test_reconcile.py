#!/usr/bin/env python3
from pathlib import Path
import tempfile,subprocess
from support import cleanup_fixture as fixture
root=Path(__file__).resolve().parents[1]
source=fixture.source.replace('int main(int argc', 'static int present(void *v,const char *p,bool d){(void)v;return (d&&!strcmp(p,"nas"))||(!d&&!strcmp(p,"nas/file"));}\nint main(int argc')
source=source.replace(' puts("PASS Test29 protected', r'''
 setup("repair-extras");assert(!mkdir("sd/nas",0700));put("sd/nas/file","valid");put("sd/nas/extra","remove");assert(!mkdir("sd/extra-dir",0700));put("sd/extra-dir/data","remove");f=(fixture){0};o=options(&f);o.present=present;r=(fast_sync_result_t){0};assert(fast_sync_reconcile("sd",&o,&r));assert(!access("sd/nas/file",F_OK)&&access("sd/nas/extra",F_OK)&&access("sd/extra-dir",F_OK));assert(!access("sd/keep/safe",F_OK)&&!access("sd/BNCACHE/cache",F_OK));assert(r.deleted==2);assert(!chdir(".."));
 puts("PASS Test35 reconciliation: canonical present callback retains NAS files/directories, removes only extras including nested siblings and preserves exclusions/cache");
 puts("PASS Test29 protected''')
with tempfile.TemporaryDirectory(prefix='bernina35-clean-') as d:
 p=Path(d);(p/'test.c').write_text(source);(p/'engine.c').write_text('#include <unistd.h>\nint guarded_rmdir(const char *);\n#define rmdir guarded_rmdir\n#define mirror_find counted_find\n#include "'+str(root/'src/fast_sync.c')+'"\n')
 subprocess.run(['clang','-std=gnu11','-g','-fsanitize=address,undefined','-Wno-deprecated-declarations','-I'+str(root/'src'),str(p/'test.c'),str(p/'engine.c')]+[str(root/'src'/n) for n in ['mirror_sync.c','safe_update.c','single_copy.c','batch_sync.c','dav_listing.c','dav_unicode.c','dav_tree.c','dry_run.c']]+['-o',str(p/'run')],check=True);subprocess.run([str(p/'run'),d],check=True)
