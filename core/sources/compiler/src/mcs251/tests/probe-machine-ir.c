/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "machine_ir.h"
static int memory(const char *name,void *context) {
  (void)context;
  return !strcmp(name,"_data")?1:0;
}
static int call(const char *name,uint32_t *reads,uint32_t *writes,void *context) {
  (void)context;if(strcmp(name,"_known"))return 0;
  *reads=mi_mask(4,2);*writes=mi_mask(0,2)|mi_mask(4,2);return 1;
}
int main(int argc,char **argv) {
  if(argc!=2)return 2;
  FILE *f=fopen(argv[1],"rb");if(!f)return 2;
  fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
  char *body=(char *)calloc((size_t)size+1,1);
  if(!body || fread(body,1,(size_t)size,f)!=(size_t)size)return 2;
  fclose(f);unsigned counts[3];
  char *result=mi_optimize(body,(size_t)size,UINT32_MAX,memory,call,NULL,counts);
  fputs(result?result:body,stdout);free(result);free(body);return 0;
}
