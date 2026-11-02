/* SPDX-License-Identifier: GPL-2.0-or-later
 * 首版仅 Source 编码的有界直线体；未知编码/分支/调用拒绝。
 * 数值来自完整机器字节，不能从被调函数名称取得固定答案。 */
#ifndef MX251_CALLEE_COST_H
#define MX251_CALLEE_COST_H
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include "summary_sha256.h"
enum{WCS_ENTRIES=32,WCS_BYTES=2048,WCS_METRICS=7,WCS_FILE=262144};
typedef struct{char name[160],signature[256],sha[65];unsigned bytes,metric[WCS_METRICS];}wide_callee_summary;
static unsigned wcs_count;static int wcs_loaded;
static wide_callee_summary wcs_entries[WCS_ENTRIES];
static int wcs_hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
static int wcs_name(const char *s){if(!*s||(!isalpha((unsigned char)*s)&&*s!='_'))return 0;for(++s;*s;++s)if(!isalnum((unsigned char)*s)&&*s!='_')return 0;return 1;}
static int wcs_decode(const unsigned char *b,unsigned size,unsigned metric[WCS_METRICS])
{
 memset(metric,0,sizeof(unsigned)*WCS_METRICS);
 if(!size||size>WCS_BYTES)return 0;
 for(unsigned p=0;p<size;){
  unsigned op=b[p],len=0,mov=0,memory=0,read=0;
  if(op==0xaa){if(p+1!=size)return 0;++metric[0];return 1;}
  if(p+2>size)return 0;
  unsigned arg=b[p+1];
  if(op==0x7c||op==0x7d){len=2;mov=1;} /* MOV R/WR, matching register width. */
  else if(op==0xa5&&((arg>=0xe8&&arg<=0xef)||arg>=0xf8)){len=2;mov=1;}
  else if(op==0x7e&&((arg&15)==0||(arg&15)==1)){len=3;mov=1;}
  else if(op==0x7a&&(arg&15)==1){len=3;mov=1;}
  else if(op==0x69&&(arg&15)==15){len=4;mov=1;memory=read=1;} /* WR,@SPX+signed16. */
  else if(op==0xad){len=2;} /* MUL WR,WR: destination product registers must exist. */
  else if(op==0x2f&&(arg>>4)<8&&(arg&15)<8){len=2;} /* ADD DR,DR; excludes SPX/DPX. */
  else return 0;
  if(p+len>size)return 0;
  /* 不接受能改栈/控制状态的 direct SFR；ABI 寄存器及 bank0别名可解码。 */
  if((op==0x7a||(op==0x7e&&(arg&15)==1))&&b[p+2]>7&&b[p+2]!=0x82&&b[p+2]!=0x83&&b[p+2]!=0xf0)return 0;
  if(op==0xad&&(arg>>4)>14)return 0;
  ++metric[0];metric[1]+=mov;metric[2]+=memory;metric[3]+=read;p+=len;
 }
 return 0; /* Every admitted body ends with exactly one ERET. */
}
static int wcs_load(const char *path,const char *expected)
{
 if(wcs_loaded)return wcs_loaded>0;
 wcs_loaded=-1;wcs_count=0;
 if(!path||!expected||strlen(expected)!=64)return 0;
 FILE *f=fopen(path,"rb");if(!f)return 0;
 unsigned char *data=(unsigned char*)malloc(WCS_FILE+1);if(!data){fclose(f);return 0;}
 size_t n=fread(data,1,WCS_FILE+1,f);int clean=!ferror(f)&&feof(f);fclose(f);
 if(!clean||!n||n>WCS_FILE){free(data);return 0;}data[n]=0;
 char digest[65];wcs_sha256(data,n,digest);
 if(strcmp(digest,expected)||memchr(data,0,n)){free(data);return 0;}
 char *line=(char*)data,*end=strchr(line,'\n');
 if(!end){free(data);return 0;}*end=0;
 if(strcmp(line,"MX251-CALLEE-COST-V1 source-straightline-1")){free(data);return 0;}line=end+1;
 while(*line){
  end=strchr(line,'\n');if(!end)goto invalid;*end=0;
  if(wcs_count==WCS_ENTRIES)goto invalid;
  wide_callee_summary *s=&wcs_entries[wcs_count];char hex[WCS_BYTES*2+1];int used=0;
  if(sscanf(line,"C %159s %255s %64s %u %u,%u,%u,%u,%u,%u,%u %4096s%n",s->name,s->signature,s->sha,&s->bytes,&s->metric[0],&s->metric[1],&s->metric[2],&s->metric[3],&s->metric[4],&s->metric[5],&s->metric[6],hex,&used)!=12||!used||line[used]||!wcs_name(s->name)||!s->bytes||s->bytes>WCS_BYTES||strlen(hex)!=s->bytes*2||strlen(s->sha)!=64)goto invalid;
  char canonical[4800];
  int canonicalLength=snprintf(canonical,sizeof canonical,"C %s %s %s %u %u,%u,%u,%u,%u,%u,%u %s",s->name,s->signature,s->sha,s->bytes,s->metric[0],s->metric[1],s->metric[2],s->metric[3],s->metric[4],s->metric[5],s->metric[6],hex);
  if(canonicalLength<0||(size_t)canonicalLength>=sizeof canonical||strcmp(canonical,line))goto invalid;
  for(unsigned j=0;j<wcs_count;++j)if(!strcmp(s->name,wcs_entries[j].name))goto invalid;
  unsigned char bytes[WCS_BYTES];unsigned metrics[WCS_METRICS];
  for(unsigned j=0;j<s->bytes;++j){int h=wcs_hex(hex[j*2]),l=wcs_hex(hex[j*2+1]);if(h<0||l<0)goto invalid;bytes[j]=(unsigned char)(h*16+l);}
  wcs_sha256(bytes,s->bytes,digest);
  if(strcmp(digest,s->sha)||!wcs_decode(bytes,s->bytes,metrics)||memcmp(metrics,s->metric,sizeof metrics))goto invalid;
  ++wcs_count;line=end+1;
 }
 free(data);wcs_loaded=1;return 1;
invalid:free(data);wcs_count=0;return 0;
}
#endif
