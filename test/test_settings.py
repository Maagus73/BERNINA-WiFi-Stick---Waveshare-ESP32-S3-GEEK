#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
code=r'''
#include "app_settings.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
int main(void){setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1);tzset();struct tm d={.tm_year=126,.tm_mon=2,.tm_mday=29,.tm_hour=12,.tm_isdst=-1};time_t day_start,day_end;assert(app_day_range(mktime(&d),&day_start,&day_end)&&day_end-day_start==82800);d=(struct tm){.tm_year=126,.tm_mon=9,.tm_mday=25,.tm_hour=12,.tm_isdst=-1};assert(app_day_range(mktime(&d),&day_start,&day_end)&&day_end-day_start==90000);assert(!app_day_range(0,&day_start,&day_end));uint32_t n;assert(app_interval_convert(10,false,&n)&&n==600);assert(app_interval_convert(2,true,&n)&&n==7200);assert(app_interval_convert(10080,false,&n)&&n==604800);assert(app_interval_convert(168,true,&n));assert(!app_interval_convert(169,true,&n)&&!app_interval_convert(10081,false,&n)&&!app_interval_convert(0,false,&n)&&!app_interval_convert(0xffffffff,true,&n));char a[]={ 'a',(char)0xc3,0};app_utf8_tail(a);assert(!strcmp(a,"a"));char b[]={ 'a',(char)0xc3,(char)0xa4,0};app_utf8_tail(b);assert(strlen(b)==3);char c[]={(char)0xf0,(char)0x9f,(char)0x98,0};app_utf8_tail(c);assert(!*c);}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'main.c').write_text(code);subprocess.run(['clang','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'src'),str(p/'main.c'),str(root/'src/app_settings.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
print('PASS interval limits, exact minute/hour conversions, overflow rejection and UTF-8 log truncation')
