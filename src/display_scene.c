// Portable renderer shared by the actual LCD task and exact-pixel host previews.
#include "display_scene.h"
#include <stdio.h>
#include <string.h>
static const uint8_t glyphs[36][7]={
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
 {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
 {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
 {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
 {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
 {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
 {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};
typedef struct {int x,y,w,h;uint16_t *pixels;} canvas_t;
static bool intersects(const canvas_t *c,int x,int y,int w,int h){return x<c->x+c->w&&x+w>c->x&&y<c->y+c->h&&y+h>c->y;}
static void fill(canvas_t *c,int x,int y,int w,int h,uint16_t color)
{
 int left=x>c->x?x:c->x,top=y>c->y?y:c->y;
 int right=x+w<c->x+c->w?x+w:c->x+c->w,bottom=y+h<c->y+c->h?y+h:c->y+c->h;
 for(int row=top;row<bottom;row++)for(int col=left;col<right;col++)c->pixels[(row-c->y)*c->w+col-c->x]=color;
}
static void frame(canvas_t *c,int x,int y,int w,int h,uint16_t color)
{
 fill(c,x+2,y,w-4,1,color);fill(c,x+2,y+h-1,w-4,1,color);
 fill(c,x,y+2,1,h-4,color);fill(c,x+w-1,y+2,1,h-4,color);
 fill(c,x+1,y+1,1,1,color);fill(c,x+w-2,y+1,1,1,color);
 fill(c,x+1,y+h-2,1,1,color);fill(c,x+w-2,y+h-2,1,1,color);
}
static uint8_t bits(char ch,unsigned row)
{
 if(ch>='a'&&ch<='z')ch=(char)(ch-'a'+'A');
 unsigned index=ch>='0'&&ch<='9'?(unsigned)(ch-'0'):ch>='A'&&ch<='Z'?(unsigned)(ch-'A'+10):36;
 if(index<36)return glyphs[index][row];
 if(ch=='%'){static const uint8_t g[7]={17,18,2,4,8,9,17};return g[row];}
 if(ch=='.')return row==6?4:0;
 if(ch=='-')return row==3?14:0;
 if(ch==':')return row==2||row==5?4:0;
 return 0;
}
static void text(canvas_t *c,int x,int y,const char *value,unsigned scale,uint16_t color)
{
 for(;*value;value++,x+=6*(int)scale){
  if(!intersects(c,x,y,5*(int)scale,7*(int)scale))continue;
  for(unsigned row=0;row<7;row++)for(unsigned col=0;col<5;col++)if(bits(*value,row)&(1U<<(4-col)))
   fill(c,x+(int)(col*scale),y+(int)(row*scale),(int)scale,(int)scale,color);
 }
}
static void centered(canvas_t *c,int y,const char *value,unsigned scale,uint16_t color)
{
 int width=(int)strlen(value)*6*(int)scale-(int)scale;
 text(c,(SCENE_WIDTH-width)/2,y,value,scale,color);
}
static void symbol(canvas_t *c,unsigned kind,int x,int y,uint16_t color)
{
 // Original 9x9 check circle, sync arrows, warning triangle.
 static const uint16_t icons[3][9]={
  {56,198,257,273,297,325,257,198,56},
  {124,130,7,2,0,128,448,130,124},
  {16,40,40,68,84,130,146,257,511}
 };
 for(unsigned row=0;row<9;row++)for(unsigned col=0;col<9;col++)if(icons[kind][row]&(1U<<(8-col)))fill(c,x+(int)col,y+(int)row,1,1,color);
}
static void status_fields(canvas_t *c,display_state_t state)
{
 static const uint16_t bright[]={0x67F1,0x2E9F,0xFCA6},dim[]={0x02C8,0x0230,0x82A4};
 static const uint16_t active_bg[]={0x0925,0x08CB,0x20C2},inactive_bg[]={0x0021,0x0022,0x0840};
 static const char *labels[]={"BEREIT","SYNC","FEHLER"};
 for(unsigned n=0;n<3;n++){
  bool active=(n==0&&state==DISPLAY_READY)||(n==1&&state==DISPLAY_SYNC)||(n==2&&state==DISPLAY_ERROR);
  int x=3+(int)n*79;uint16_t color=active?bright[n]:dim[n];
  fill(c,x+1,4,74,30,active?active_bg[n]:inactive_bg[n]);frame(c,x,3,76,32,color);
  symbol(c,n,x+33,8,color);
  int width=(int)strlen(labels[n])*6-1;text(c,x+(76-width)/2,23,labels[n],1,color);
 }
}
void display_scene_init(display_scene_t *s)
{
 memset(s,0,sizeof(*s));s->state=DISPLAY_START;
 uint32_t seed=0x17A;
 for(unsigned n=0;n<SCENE_COLUMNS;n++){seed=seed*1664525U+1013904223U;s->heads[n]=(uint8_t)((seed>>16)%SCENE_ROWS);s->phase[n]=(uint8_t)seed;}
}
void display_scene_advance(display_scene_t *s,unsigned column)
{
 if(column>=SCENE_COLUMNS)return;
 s->heads[column]=(uint8_t)((s->heads[column]+1)%SCENE_ROWS);s->phase[column]++;
}
static void matrix(canvas_t *c,const display_scene_t *s)
{
 if(!s->matrix||s->state!=DISPLAY_READY)return;
 static const uint16_t colors[]={0x8FF4,0x058C,0x0348,0x0164};
 for(unsigned column=0;column<SCENE_COLUMNS;column++){
  int x=(int)column*SCENE_CELL;
  if(!intersects(c,x,SCENE_BODY,SCENE_CELL,SCENE_ROWS*SCENE_CELL))continue;
  for(unsigned age=0;age<4;age++){
   unsigned row=(s->heads[column]+SCENE_ROWS-age)%SCENE_ROWS;
   char digit[2]={(char)('0'+((column*13+row*7+s->phase[column])%10)),0};
   text(c,x+1,SCENE_BODY+(int)row*SCENE_CELL,digit,1,colors[age]);
  }
 }
}
bool display_scene_render(const display_scene_t *s,int x,int y,int w,int h,uint16_t *pixels,unsigned capacity)
{
 if(!s||!pixels||x<0||y<0||w<=0||h<=0||x>=SCENE_WIDTH||y>=SCENE_HEIGHT||w>SCENE_WIDTH-x||h>SCENE_HEIGHT-y||
    (unsigned)w>capacity/(unsigned)h)return false;
 canvas_t c={x,y,w,h,pixels};fill(&c,x,y,w,h,0x0000);
 if(y<SCENE_BODY)status_fields(&c,s->state);
 if(y+h<=SCENE_BODY)return true;
 if(s->state==DISPLAY_READY){
  matrix(&c,s);
  // Repaint this opaque plaque even on partial matrix tiles: never text overdraw.
  fill(&c,16,70,208,38,0x0000);fill(&c,17,71,206,36,0x0020);frame(&c,16,70,208,38,0x3E4D);
  centered(&c,82,"BERNINA READY",2,0x8FF5);
 }else if(s->state==DISPLAY_SYNC){
  centered(&c,49,s->input.verifying?"SD PRUEFUNG":"DOWNLOAD",2,0x2E9F);
  int p=display_percent(&s->input);char value[48];
  if(p<0)strcpy(value,"AKTIV");else snprintf(value,sizeof(value),"%d%%",p);
  // Zero-byte batches have a known file count; their completion is file-based.
  if(s->input.file_count&&!s->input.total){
   p=s->input.succeeded<=s->input.file_count?(int)(100ULL*s->input.succeeded/s->input.file_count):-1;
   if(p<0)strcpy(value,"AKTIV");else snprintf(value,sizeof(value),"%d%%",p);
  }
  centered(&c,73,value,2,0xC75F);
  char amount[48];
  if(s->input.total_known||s->input.file_count){
   if(s->input.total<=999999999ULL)snprintf(amount,sizeof(amount),"%llu/%llu B",(unsigned long long)s->input.bytes,(unsigned long long)s->input.total);
   else snprintf(amount,sizeof(amount),"%llu/%llu MiB",(unsigned long long)(s->input.bytes/(1024*1024)),(unsigned long long)(s->input.total/(1024*1024)));
   centered(&c,90,amount,1,0x9492);
  }
  fill(&c,18,100,204,10,0x08CB);frame(&c,18,100,204,10,0x2BB4);
  if(p>=0)fill(&c,20,102,200*p/100,6,0x2E9F);
  if(s->input.file_count){
   uint64_t done=(uint64_t)s->input.succeeded+s->input.failed;
   unsigned remaining=done<s->input.file_count?s->input.file_count-(unsigned)done:0;
   snprintf(value,sizeof(value),"OK %u OFFEN %u",s->input.succeeded,remaining);
  }
  else snprintf(value,sizeof(value),"%llu B",(unsigned long long)s->input.bytes);
  centered(&c,121,value,1,0x9492);
 }else if(s->state==DISPLAY_RETRY){
  centered(&c,52,"NAS WARTET",2,0xDEFB);char retry[40];snprintf(retry,sizeof(retry),"NEUER VERSUCH IN %u S",(unsigned)s->input.retry_seconds);centered(&c,84,retry,1,0xBDF7);centered(&c,112,s->input.usb_confirmed?"USB AKTIV":"SD NICHT AN USB",1,0x9492);
 }else if(s->state==DISPLAY_ERROR){
  symbol(&c,2,115,48,0xFCA6);centered(&c,67,"FEHLER",2,0xFCA6);
  char message[36];const char *error=s->input.error?s->input.error:"ZUSTAND UNKLAR";size_t length=strlen(error);
  if(length>=sizeof(message)){memcpy(message,error,sizeof(message)-4);memcpy(message+sizeof(message)-4,"...",4);}else memcpy(message,error,length+1);
  centered(&c,94,message,1,0xDEBA);centered(&c,118,"DIAGNOSE IM WEB",1,0x7BEF);
 }else if(s->state==DISPLAY_APP){
  if(s->input.comparing){
   centered(&c,49,"VERGLEICH",2,0xDEFB);
   const char *phase=s->input.compare_phase==4?"SD BEREINIGEN":s->input.compare_phase==5?"ORDNER ANLEGEN":s->input.compare_phase==6?"NAS VALIDIEREN":s->input.compare_phase==7?"STAND SPEICHERN":s->input.compare_phase==1?"1/3 NAS LESEN":s->input.compare_phase==2?"2/3 SD LESEN":s->input.compare_phase==3?"3/3 AUSWERTEN":"VERBINDUNG AUFBAUEN";
   centered(&c,73,phase,1,0xBDF7);
   char progress[64];snprintf(progress,sizeof(progress),s->input.compare_phase>=4?"EINTRAEGE %u/%u":"ORDNER %u/%u BEKANNT",s->input.compare_done,s->input.compare_known);centered(&c,87,progress,1,0x9492);
   fill(&c,18,100,204,10,0x08CB);frame(&c,18,100,204,10,0x2BB4);
   unsigned pulse=s->input.compare_pulse%32;unsigned position=pulse<16?pulse:31-pulse;
   fill(&c,20+(int)position*10,102,50,6,0x2E9F);
   snprintf(progress,sizeof(progress),"%u EINTRAEGE ERFASST",s->input.compare_entries);centered(&c,121,progress,1,0x9492);
  }else{
   centered(&c,63,"APP AKTIV",2,0xDEFB);centered(&c,91,"USB GETRENNT",1,0x9492);
  }
  if(!s->input.comparing&&s->input.file_count){char result[32];snprintf(result,sizeof(result),"OK %u FEHLER %u",s->input.succeeded,s->input.failed);centered(&c,115,result,1,0xBDF7);}
 }else{
  centered(&c,65,s->state==DISPLAY_START?"START":"USB WARTEN",2,0xDEFB);
  centered(&c,94,s->state==DISPLAY_START?"USB INITIALISIERUNG":"KEINE USB FREIGABE",1,0x9492);
 }
 return true;
}
