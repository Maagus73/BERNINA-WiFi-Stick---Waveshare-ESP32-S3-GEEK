#include "dav_listing.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <stdio.h>

#define XML_NODES 128
#define XML_DEPTH 24
typedef struct {
    uint32_t name, name_len, attrs, attrs_end, text, end;
    int parent;
    bool dav;
} node_t;
typedef struct { const char *xml; node_t *nodes; unsigned count; bool resource; } document_t;

static bool xml_char(uint32_t c)
{
    return c == 9 || c == 10 || c == 13 || (c >= 32 && c <= 0xD7FF) ||
           (c >= 0xE000 && c <= 0xFFFD) || (c >= 0x10000 && c <= 0x10FFFF);
}
bool dav_utf8_valid(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t c = *p++; unsigned more = 0; uint32_t min = 0;
        if (c >= 0xC2 && c <= 0xDF) { c &= 31; more = 1; min = 0x80; }
        else if (c >= 0xE0 && c <= 0xEF) { c &= 15; more = 2; min = 0x800; }
        else if (c >= 0xF0 && c <= 0xF4) { c &= 7; more = 3; min = 0x10000; }
        else if (c >= 0x80) return false;
        for (unsigned i = 0; i < more; i++) {
            if ((*p & 0xC0) != 0x80) return false;
            c = (c << 6) | (*p++ & 63);
        }
        if (c < min || !xml_char(c)) return false;
    }
    return true;
}
static bool append_code(char *out, size_t cap, size_t *n, uint32_t c)
{
    if (!xml_char(c)) return false;
    unsigned bytes = c < 128 ? 1 : c < 2048 ? 2 : c < 65536 ? 3 : 4;
    if (*n + bytes >= cap) return false;
    if (bytes == 1) out[(*n)++] = (char)c;
    else {
        out[*n] = (char)((bytes == 2 ? 0xC0 : bytes == 3 ? 0xE0 : 0xF0) | (c >> (6 * (bytes - 1))));
        for (unsigned i = 1; i < bytes; i++) out[*n + i] = (char)(0x80 | ((c >> (6 * (bytes - i - 1))) & 63));
        *n += bytes;
    }
    return true;
}
static bool decode(const char *s, size_t len, char *out, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] != '&') {
            if (n + 1 >= cap) return false;
            out[n++] = s[i]; continue;
        }
        size_t end = i + 1;
        while (end < len && s[end] != ';' && end - i <= 12) end++;
        if (end == len || s[end] != ';') return false;
        const char *e = s + i + 1; size_t k = end - i - 1; uint32_t c = 0;
        if (k == 3 && !memcmp(e, "amp", 3)) c = '&';
        else if (k == 2 && !memcmp(e, "lt", 2)) c = '<';
        else if (k == 2 && !memcmp(e, "gt", 2)) c = '>';
        else if (k == 4 && !memcmp(e, "quot", 4)) c = '"';
        else if (k == 4 && !memcmp(e, "apos", 4)) c = '\'';
        else if (k > 1 && *e == '#') {
            size_t j = 1; unsigned base = 10;
            if (j < k && e[j] == 'x') { base = 16; j++; }
            if (j == k) return false;
            for (; j < k; j++) {
                int v = e[j] >= '0' && e[j] <= '9' ? e[j] - '0' :
                        e[j] >= 'a' && e[j] <= 'f' ? e[j] - 'a' + 10 :
                        e[j] >= 'A' && e[j] <= 'F' ? e[j] - 'A' + 10 : 99;
                if ((unsigned)v >= base || c > 0x10FFFF / base) return false;
                c = c * base + (unsigned)v;
            }
        } else return false;
        if (!append_code(out, cap, &n, c)) return false;
        i = end;
    }
    out[n] = 0;
    return dav_utf8_valid(out);
}
static bool name_char(char c) { return isalnum((unsigned char)c) || c == ':' || c == '_' || c == '-' || c == '.'; }
static void spaces(const char **p) { while (isspace((unsigned char)**p)) (*p)++; }
// Parse one quoted attribute, without allocating. Namespace declarations use this too.
static bool attribute(const char **p, const char **name, size_t *nl, const char **value, size_t *vl)
{
    spaces(p); *name = *p;
    while (name_char(**p)) (*p)++;
    *nl = (size_t)(*p - *name); if (!*nl) return false;
    spaces(p); if (*(*p)++ != '=') return false;
    spaces(p); char quote = *(*p)++; if (quote != '\'' && quote != '"') return false;
    *value = *p;
    while (**p && **p != quote && **p != '<') (*p)++;
    if (**p != quote) return false;
    *vl = (size_t)(*p - *value); (*p)++; return true;
}
static bool namespace_dav(document_t *d, unsigned index)
{
    node_t *node = &d->nodes[index]; const char *q = d->xml + node->name;
    const char *colon = memchr(q, ':', node->name_len);
    size_t prefix = colon ? (size_t)(colon - q) : 0;
    for (int i = (int)index; i >= 0; i = d->nodes[i].parent) {
        const char *p = d->xml + d->nodes[i].attrs, *end = d->xml + d->nodes[i].attrs_end;
        spaces(&p);
        while (p < end) {
            const char *a, *v; size_t al, vl;
            if (!attribute(&p, &a, &al, &v, &vl)) return false;
            if ((!prefix && al == 5 && !memcmp(a,"xmlns",5)) ||
                (prefix && al == prefix + 6 && !memcmp(a,"xmlns:",6) && !memcmp(a+6,q,prefix))) {
                char uri[128]; return decode(v,vl,uri,sizeof(uri)) && !strcmp(uri,"DAV:");
            }
            spaces(&p);
        }
    }
    return false;
}
static bool is_node(document_t *d, unsigned i, const char *local)
{
    node_t *n = &d->nodes[i]; const char *p = d->xml + n->name;
    const char *c = memchr(p, ':', n->name_len);
    size_t len = n->name_len;
    if (c) { len -= (size_t)(c + 1 - p); p = c + 1; }
    return n->dav && strlen(local) == len && !memcmp(p,local,len);
}
static bool parse_xml(document_t *d)
{
    const char *s = d->xml, *p = s; int parent = -1; unsigned depth = 0, roots = 0;
    if (!dav_utf8_valid(s)) return false;
    if (!strncmp(p,"\xEF\xBB\xBF",3)) p += 3;
    while (*p) {
        if (*p != '<') { if (parent < 0 && !isspace((unsigned char)*p)) return false; p++; continue; }
        if (!strncmp(p,"<!--",4)) { const char *e=strstr(p+4,"-->"); if(!e)return false; p=e+3;continue; }
        if (!strncmp(p,"<?xml ",6) && !d->count && !roots) {
            const char *e = strstr(p+6,"?>"); if(!e)return false;
            // Only UTF-8 XML is supported. Reject other declared encodings.
            const char *enc = strstr(p,"encoding");
            if(enc && enc<e) {
                const char *a,*v;size_t al,vl;
                if(!attribute(&enc,&a,&al,&v,&vl)||vl!=5||strncasecmp(v,"utf-8",5))return false;
            }
            p=e+2;continue;
        }
        if (!strncmp(p,"<![CDATA[",9) && parent>=0) { const char *e=strstr(p+9,"]]>");if(!e)return false;p=e+3;continue; }
        if (p[1]=='!') return false; // DTD/entities and all external expansion forbidden.
        if (p[1]=='/') {
            p+=2; const char *q=p;while(name_char(*p))p++;
            if(parent<0||p-q!=d->nodes[parent].name_len||memcmp(q,s+d->nodes[parent].name,(size_t)(p-q)))return false;
            spaces(&p);if(*p!='>')return false;
            d->nodes[parent].end=(uint32_t)(q-2-s);parent=d->nodes[parent].parent;depth--;p++;continue;
        }
        p++; const char *q=p;while(name_char(*p))p++;
        if(d->count==XML_NODES||depth==XML_DEPTH){d->resource=true;return false;}
        if(p==q)return false;
        if(parent<0&&++roots!=1)return false;
        unsigned i=d->count++;node_t *n=&d->nodes[i];
        n->name=(uint32_t)(q-s);n->name_len=(uint32_t)(p-q);n->parent=parent;n->attrs=(uint32_t)(p-s);
        spaces(&p);
        while(*p&&*p!='>'&&*p!='/') {
            const char *a,*v;size_t al,vl;
            const char *before=p;
            if(!attribute(&p,&a,&al,&v,&vl))return false;
            const char *previous=s+n->attrs;
            spaces(&previous);
            while(previous<before) {
                const char *old_name,*old_value;size_t old_len,old_vlen;
                if(!attribute(&previous,&old_name,&old_len,&old_value,&old_vlen))return false;
                if(al==old_len&&!memcmp(a,old_name,al))return false;
                spaces(&previous);
            }
            if(*p!='>'&&*p!='/'&&!isspace((unsigned char)*p))return false;
            spaces(&p);
        }
        n->attrs_end=(uint32_t)(p-s);n->dav=namespace_dav(d,i);
        bool empty=*p=='/';if(empty)p++;
        if(*p++!='>')return false;
        n->text=(uint32_t)(p-s);
        if(empty)n->end=n->text;else {parent=(int)i;depth++;}
    }
    return parent<0&&d->count&&is_node(d,0,"multistatus");
}
static bool text(document_t *d, unsigned i, char *out, size_t cap)
{
    node_t *n=&d->nodes[i];const char *p=d->xml+n->text,*end=d->xml+n->end;size_t used=0;
    while(p<end) {
        if(!strncmp(p,"<![CDATA[",9)) {
            const char *e=strstr(p+9,"]]>");if(!e||e>end)return false;
            if(used+(size_t)(e-p-9)>=cap){d->resource=true;return false;}
            memcpy(out+used,p+9,(size_t)(e-p-9));used+=(size_t)(e-p-9);p=e+3;
        } else if(!strncmp(p,"<!--",4)) {
            const char *e=strstr(p+4,"-->");if(!e||e>end)return false;p=e+3;
        } else {
            const char *e=memchr(p,'<',(size_t)(end-p));if(!e)e=end;
            if(e==p)return false;
            if(!decode(p,(size_t)(e-p),out+used,cap-used)){if((size_t)(e-p)>=cap-used)d->resource=true;return false;}
            used+=strlen(out+used);p=e;
        }
    }
    out[used]=0;return dav_utf8_valid(out);
}
static void trim(char *s)
{
    size_t n=strlen(s);while(n&&isspace((unsigned char)s[n-1]))s[--n]=0;
    char *p=s;while(isspace((unsigned char)*p))p++;memmove(s,p,strlen(p)+1);
}
static bool status_ok(const char *s) { return (!strncmp(s,"HTTP/1.1 200",12)||!strncmp(s,"HTTP/1.0 200",12)) && (!s[12]||s[12]==' '); }
static int hex(char c) { return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1; }
static bool uri_decode(const char *s,char *out,size_t cap)
{
    size_t n=0;
    while(*s) {
        unsigned char c=(unsigned char)*s++;
        if(c=='%') {if(!s[0]||!s[1]||hex(s[0])<0||hex(s[1])<0)return false;c=(unsigned char)(hex(s[0])*16+hex(s[1]));s+=2;}
        if((c<32&&c!=9&&c!=10&&c!=13)||c==127||n+1>=cap)return false;
        out[n++]=(char)c;
    }
    out[n]=0;return dav_utf8_valid(out);
}
static bool entry_name(const char *href,const char *base,char *name,size_t cap,bool *self)
{
    if(strncmp(base,"https://",8))return false;
    const char *origin_end=strchr(base+8,'/');if(!origin_end)return false;
    const char *path=href;
    if(!strncmp(href,"https://",8)) {
        const char *end=strchr(href+8,'/');
        if(!end||end-href!=origin_end-base||strncasecmp(href,base,(size_t)(end-href)))return false;
        path=end;
    } else if(strstr(href,"://")||!strncmp(href,"//",2))return false;
    if(strchr(path,'?')||strchr(path,'#')||strchr(origin_end,'?')||strchr(origin_end,'#'))return false;
    char root[1536],full[1536];
    if(!uri_decode(origin_end,root,sizeof(root)))return false;
    size_t r=strlen(root);while(r>1&&root[r-1]=='/')root[--r]=0;
    if(*path!='/') {
        char rel[1024];if(!uri_decode(path,rel,sizeof(rel)))return false;
        if(strlen(root)+strlen(rel)+2>sizeof(full))return false;
        strcpy(full,root);if(r!=1)strcat(full,"/");strcat(full,rel);
    } else if(!uri_decode(path,full,sizeof(full)))return false;
    size_t f=strlen(full);while(f>1&&full[f-1]=='/')full[--f]=0;
    if(!strcmp(root,full)){*self=true;name[0]=0;return true;}
    size_t prefix=r==1?1:r+1;
    if(strncmp(root,full,r)||(r>1&&full[r]!='/')||f<=prefix)return false;
    const char *leaf=full+prefix;
    if(strchr(leaf,'/')||!strcmp(leaf,".")||!strcmp(leaf,"..")||strlen(leaf)>=cap)return false;
    strcpy(name,leaf);*self=false;return true;
}
static dav_error_t parse_response(const char *xml,const char *base,dav_entry_t *entry,bool *is_self)
{
    memset(entry,0,sizeof(*entry));
    if(strlen(xml)>DAV_RESPONSE_LIMIT + DAV_TOKEN_LIMIT + 128)return DAV_ERR_RESOURCE;
    document_t d={.xml=xml,.nodes=calloc(XML_NODES,sizeof(node_t))};
    if(!d.nodes)return DAV_ERR_MEMORY;
    dav_error_t error=DAV_ERR_XML;unsigned responses=0;
    if(!parse_xml(&d)){if(d.resource)error=DAV_ERR_RESOURCE;goto done;}
    for(unsigned i=1;i<d.count;i++) {
        if(d.nodes[i].parent!=0)continue;
        if(!is_node(&d,i,"response"))continue;
        if(++responses>1)goto done;
        dav_entry_t e={0};char href[1536]={0};bool have_href=false,have_type=false;
        for(unsigned j=i+1;j<d.count;j++) {
            if(d.nodes[j].parent!=(int)i)continue;
            if(is_node(&d,j,"href")) {if(have_href||!text(&d,j,href,sizeof(href)))goto done;trim(href);have_href=true;}
            if(is_node(&d,j,"status")) {char v[96];if(!text(&d,j,v,sizeof(v)))goto done;trim(v);if(!status_ok(v))goto done;}
            if(!is_node(&d,j,"propstat"))continue;
            bool good=false,have_status=false;int prop=-1;
            for(unsigned k=j+1;k<d.count;k++)if(d.nodes[k].parent==(int)j) {
                if(is_node(&d,k,"status")) {char v[96];if(have_status||!text(&d,k,v,sizeof(v)))goto done;trim(v);good=status_ok(v);have_status=true;}
                if(is_node(&d,k,"prop")){if(prop>=0)goto done;prop=(int)k;}
            }
            if(!have_status||prop<0)goto done;
            if(!good)continue; // Failed propstats must never supply metadata.
            for(unsigned k=(unsigned)prop+1;k<d.count;k++)if(d.nodes[k].parent==prop) {
                if(is_node(&d,k,"resourcetype")) {
                    if(have_type)goto done;
                    have_type=true;e.kind=DAV_FILE;
                    for(unsigned t=k+1;t<d.count;t++)if(d.nodes[t].parent==(int)k) {
                        if(is_node(&d,t,"collection"))e.kind=DAV_DIRECTORY;else if(e.kind!=DAV_DIRECTORY)e.kind=DAV_OTHER;
                    }
                } else if(is_node(&d,k,"getcontentlength")) {
                    char v[32];if(e.size_known||!text(&d,k,v,sizeof(v)))goto done;trim(v);
                    if(!*v)continue;
                    for(char *p=v;*p;p++)if(!isdigit((unsigned char)*p))goto done;
                    errno=0;char *end;e.size=strtoull(v,&end,10);if(errno||*end)goto done;e.size_known=true;
                } else {
                    char *dest=NULL;size_t cap=0;
                    if(is_node(&d,k,"getetag")){dest=e.etag;cap=sizeof(e.etag);}
                    if(is_node(&d,k,"getlastmodified")){dest=e.modified;cap=sizeof(e.modified);}
                    if(is_node(&d,k,"creationdate")){dest=e.created;cap=sizeof(e.created);}
                    if(is_node(&d,k,"getcontenttype")){dest=e.content_type;cap=sizeof(e.content_type);}
                    if(dest){if(*dest||!text(&d,k,dest,cap))goto done;trim(dest);}
                }
            }
        }
        bool self=false;
        error=DAV_ERR_PROPERTY;
        if(!have_href||!have_type)goto done;
        error=DAV_ERR_PATH;
        if(!entry_name(href,base,e.name,sizeof(e.name),&self))goto done;
        if(self&&e.kind!=DAV_DIRECTORY){error=DAV_ERR_PROPERTY;goto done;}
        *entry=e;*is_self=self;
    }
    error=responses==1?DAV_OK:(d.count>1&&(!d.nodes[1].dav||is_node(&d,1,"responsedescription"))?DAV_OK:DAV_ERR_PROPERTY);
done:
    if(error==DAV_ERR_XML&&d.resource)error=DAV_ERR_RESOURCE;
    free(d.nodes);return error;
}

struct dav_stream {
 char *url;
 dav_emit_t emit;
 void *context;
 dav_error_t error;
 size_t offset,token_len,response_len;
 unsigned depth,first_len;
 bool token_active,capturing,root_seen,root_closed,self_seen;
 char quote,first[3],token[DAV_TOKEN_LIMIT+1],response[DAV_RESPONSE_LIMIT+1];
 char root[DAV_TOKEN_LIMIT+1],prolog[256],names[XML_DEPTH][64];
};
const char *dav_error_name(dav_error_t e)
{
 static const char *names[]={"OK","XML-Syntax/Namespace","XML unvollstaendig","WebDAV-Eigenschaften/Status fehlen",
 "Ungueltiger/ausserhalb liegender Pfad","Speichermangel","Ressourcengrenze (Token/Ressource/Tiefe/Pfad)",
 "HTTP-Verbindung/Status","HTTP-Antwort unvollstaendig","SD-Verzeichnisfehler","APP-Besitz ungueltig","Doppelter Pfad/Selbsteintrag","Eintrags-/Baum-Speicherbudget erschoepft","Index-Speicher fehlt, voll oder Schreib-/Lesefehler","NAS-Quellwechsel: neue Quelle separat bestaetigen","Reservierter SD-Cacheordner kollidiert mit der NAS-Quelle"};
 return (unsigned)e<sizeof(names)/sizeof(names[0])?names[e]:"Unbekannter Fehler";
}
dav_stream_t *dav_stream_new(const char *url,dav_emit_t emit,void *context)
{
 dav_stream_t *p=calloc(1,sizeof(*p));if(!p)return NULL;
 p->url=malloc(strlen(url)+1);if(!p->url){free(p);return NULL;}
 strcpy(p->url,url);p->emit=emit;p->context=context;return p;
}
void dav_stream_free(dav_stream_t *p){if(p){free(p->url);free(p);}}
size_t dav_stream_offset(const dav_stream_t *p){return p?p->offset:0;}
static bool response_append(dav_stream_t *p,const char *s,size_t n)
{
 if(p->response_len+n>DAV_RESPONSE_LIMIT){p->error=DAV_ERR_RESOURCE;return false;}
 memcpy(p->response+p->response_len,s,n);p->response_len+=n;p->response[p->response_len]=0;return true;
}
static void emit_response(dav_stream_t *p)
{
 size_t len=strlen(p->prolog)+strlen(p->root)+p->response_len+strlen(p->names[0])+4;
 char *doc=malloc(len+1);if(!doc){p->error=DAV_ERR_MEMORY;return;}
 snprintf(doc,len+1,"%s%s%s</%s>",p->prolog,p->root,p->response,p->names[0]);
 dav_entry_t entry;bool self=false;
 p->error=parse_response(doc,p->url,&entry,&self);free(doc);
 if(p->error!=DAV_OK)return;
 if(!self&&!entry.name[0])return;
 if(self){if(p->self_seen)p->error=DAV_ERR_DUPLICATE;else p->self_seen=true;}
 else if(!p->emit(p->context,&entry))p->error=DAV_ERR_MEMORY;
}
static void stream_token(dav_stream_t *p)
{
 char *s=p->token;
 if(!dav_utf8_valid(s)){p->error=DAV_ERR_XML;return;}
 if(!strncmp(s,"<!--",4)&&strstr(s+4,"--")!=s+strlen(s)-3){p->error=DAV_ERR_XML;return;}
 if(!strncmp(s,"<!--",4)||!strncmp(s,"<![CDATA[",9)) {
  if(!p->depth&&strncmp(s,"<!--",4))p->error=DAV_ERR_XML;
  return;
 }
 if(!strncmp(s,"<?",2)) {
  if(p->root_seen||*p->prolog||strncmp(s,"<?xml ",6)||strlen(s)>=sizeof(p->prolog)){p->error=DAV_ERR_XML;return;}
  strcpy(p->prolog,s);return;
 }
 if(s[1]=='!'){p->error=DAV_ERR_XML;return;}
 bool closing=s[1]=='/';const char *q=s+(closing?2:1),*end=q;
 while(name_char(*end))end++;
 size_t len=(size_t)(end-q);
 if(!len||len>=sizeof(p->names[0])){p->error=DAV_ERR_RESOURCE;return;}
 if(closing) {
  const char *tail=end;spaces(&tail);
  if(!p->depth||strcmp(tail,">")||strlen(p->names[p->depth-1])!=len||memcmp(q,p->names[p->depth-1],len)){p->error=DAV_ERR_XML;return;}
  p->depth--;
  if(p->capturing&&p->depth==1){emit_response(p);p->capturing=false;p->response_len=0;}
  if(!p->depth)p->root_closed=true;
  return;
 }
 if(p->root_closed||p->depth==XML_DEPTH){p->error=DAV_ERR_RESOURCE;return;}
 bool empty=p->token_len>=2&&s[p->token_len-2]=='/';
 if(!p->depth) {
  if(p->root_seen){p->error=DAV_ERR_XML;return;}
  p->root_seen=true;strcpy(p->root,s);
 } else if(p->depth==1&&!p->capturing) {
  p->capturing=true;p->response_len=0;
  if(!response_append(p,s,p->token_len))return;
 }
 memcpy(p->names[p->depth],q,len);p->names[p->depth][len]=0;p->depth++;
 if(empty) {
  p->depth--;
  if(p->capturing&&p->depth==1){emit_response(p);p->capturing=false;p->response_len=0;}
  if(!p->depth)p->root_closed=true;
 }
}
static void stream_byte(dav_stream_t *p,char c)
{
 if(!c){p->error=DAV_ERR_XML;return;}
 if(p->capturing&&!response_append(p,&c,1))return;
 if(!p->token_active) {
  if(c=='<'){p->token_active=true;p->token_len=0;p->quote=0;}
  else if(!p->capturing&&!isspace((unsigned char)c)){p->error=DAV_ERR_XML;return;}
  else return;
 }
 if(p->token_len==DAV_TOKEN_LIMIT){p->error=DAV_ERR_RESOURCE;return;}
 p->token[p->token_len++]=c;p->token[p->token_len]=0;
 const char *t=p->token;size_t n=p->token_len;bool finished=false;
 if(!strncmp(t,"<!--",4))finished=n>=3&&!memcmp(t+n-3,"-->",3);
 else if(!strncmp(t,"<![CDATA[",9))finished=n>=3&&!memcmp(t+n-3,"]]>",3);
 else if(!strncmp(t,"<?",2))finished=n>=2&&!memcmp(t+n-2,"?>",2);
 else {
  if(p->quote){if(c==p->quote)p->quote=0;}
  else if(c=='\''||c=='"')p->quote=c;
  else if(c=='>')finished=true;
 }
 if(finished){p->token_active=false;stream_token(p);}
}
dav_error_t dav_stream_feed(dav_stream_t *p,const char *data,size_t n)
{
 for(size_t i=0;i<n&&p->error==DAV_OK;i++) {
  p->offset++;
  if(p->first_len<3) {
   p->first[p->first_len++]=data[i];
   if(p->first_len==3&&memcmp(p->first,"\xEF\xBB\xBF",3))
    for(unsigned j=0;j<3&&p->error==DAV_OK;j++)stream_byte(p,p->first[j]);
  } else stream_byte(p,data[i]);
 }
 return p->error;
}
dav_error_t dav_stream_finish(dav_stream_t *p)
{
 if(p->error!=DAV_OK)return p->error;
 if(!p->root_seen||!p->root_closed||p->depth||p->token_active||p->capturing)return p->error=DAV_ERR_XML_INCOMPLETE;
 if(!p->self_seen)return p->error=DAV_ERR_PROPERTY;
 return DAV_OK;
}
