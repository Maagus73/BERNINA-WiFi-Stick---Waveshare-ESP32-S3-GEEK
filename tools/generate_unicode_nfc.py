"""Generate the small canonical NFC tables from Python's Unicode database.
No compatibility folding: original NAS/SD paths are retained separately.
"""
from pathlib import Path
import unicodedata as u
root=Path(__file__).resolve().parents[1]
decomp=[];combine=[];compose=[]
for c in range(0x110000):
 if 0xAC00 <= c <= 0xD7A3:continue # algorithmic Hangul
 d=u.decomposition(chr(c))
 if d and not d.startswith('<'):
  seq=[int(x,16) for x in d.split()];assert len(seq)<=2
  decomp.append((c,seq[0],seq[1] if len(seq)>1 else 0))
  if len(seq)==2 and u.normalize('NFC',''.join(map(chr,seq)))==chr(c):compose.append((seq[0],seq[1],c))
 if u.combining(chr(c)):combine.append((c,u.combining(chr(c))))
text=f'// Generated Unicode {u.unidata_version} canonical data. tools/generate_unicode_nfc.py\n'
for name,fields,rows in [('decompositions','code,a,b',decomp),('compositions','a,b,code',sorted(compose)),('combining_classes','code,value',combine)]:
 text+='static const struct { uint32_t '+fields+'; } '+name+'[] = {\n'
 text+=''.join(' {'+','.join(hex(v) for v in row)+'},\n' for row in rows)+'};\n'
(root/'src/unicode_nfc_data.h').write_text(text)
print('Generated canonical Unicode tables:',len(decomp),len(compose),len(combine),u.unidata_version)
