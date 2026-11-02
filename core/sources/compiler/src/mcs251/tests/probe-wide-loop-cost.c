/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include "wide_loop_cost.h"
static char *read_all(const char *name,size_t *length)
{
  FILE *f=fopen(name,"rb");if(!f)return NULL;
  if(fseek(f,0,SEEK_END)) {fclose(f);return NULL;}
  long size=ftell(f);if(size<0 || size>4*1024*1024) {fclose(f);return NULL;}
  rewind(f);char *text=(char *)malloc((size_t)size+1);
  if(!text) {fclose(f);return NULL;}
  *length=fread(text,1,(size_t)size,f);fclose(f);
  if(*length!=(size_t)size) {free(text);return NULL;}
  text[*length]=0;return text;
}
static void print_string(const char *text)
{
  putchar('"');
  for(const unsigned char *p=(const unsigned char *)text;*p;++p) {
    if(*p=='"' || *p=='\\') {putchar('\\');putchar(*p);}
    else if(*p<32 || *p>=127)printf("\\u%04x",(unsigned)*p);
    else putchar(*p);
  }
  putchar('"');
}
static void print_cost(const wide_loop_costs *cost)
{
  printf("{\"unknown\":%u,\"loops\":[",cost->unknown);
  for(unsigned k=0;k<cost->count;++k) {
    printf("%s{\"header\":",k?",":"");print_string(cost->loops[k].header);printf(",\"metrics\":[");
    for(unsigned m=0;m<WLC_METRICS;++m)printf("%s%u",m?",":"",cost->loops[k].metric[m]);
    printf("]}");
  }
  printf("],\"branchCount\":%u,\"branches\":[",cost->branchCount);
  for(unsigned k=0;k<cost->branchCount;++k) {
    const wlc_branch_identity *id=&cost->branches[k];
    printf("%s{\"anchor\":",k?",":"");print_string(id->anchor);
    printf(",\"target\":");print_string(id->target);printf(",\"opcode\":");print_string(id->opcode);
    printf(",\"ordinal\":%u}",id->ordinal);
  }
  printf("],\"paths\":[");
  for(unsigned p=0;p<cost->pathCount;++p) {
    const wlc_path_cost *path=&cost->paths[p];
    printf("%s{\"loop\":%u,\"seen\":%u,\"taken\":%u,\"exited\":%u,\"metrics\":[",p?",":"",path->loop,(unsigned)path->seen,(unsigned)path->taken,path->exited);
    for(unsigned m=0;m<WLC_METRICS;++m)printf("%s%u",m?",":"",path->metric[m]);
    printf("]}");
  }
  printf("]}");
}
int main(int argc,char **argv)
{
  if(argc!=3)return 2;
  size_t aLength,bLength;char *a=read_all(argv[1],&aLength),*b=read_all(argv[2],&bLength);
  if(!a || !b) {free(a);free(b);return 2;}
  wide_loop_costs oldCost=wide_loop_cost_analyze(a,aLength),newCost=wide_loop_cost_analyze(b,bLength);
  printf("{\"parent\":");print_cost(&oldCost);printf(",\"candidate\":");print_cost(&newCost);
  int decision=wide_loop_cost_compare(a,aLength,b,bLength);
  printf(",\"decision\":%d,\"notWorse\":%s}\n",decision,decision==1?"true":"false");
  free(a);free(b);return 0;
}
