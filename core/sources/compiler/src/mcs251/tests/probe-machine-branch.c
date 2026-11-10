/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "machine_branch.h"
#include <assert.h>

static void boundaries(void) {
  char body[8192];unsigned cases=0;
  for(unsigned distance=0;distance<=160;++distance)for(int back=0;back<2;++back) {
    strcpy(body,"_test:\n");
    if(back)strcat(body,"target:\n");else strcat(body," ejmp target\n");
    for(unsigned i=0;i<distance;++i)strcat(body," nop\n");
    strcat(body,back?" ejmp target\n":"target:\n eret\n");
    char *out=mb_optimize(body,strlen(body),NULL,NULL);
    if(!distance && !back)assert(out && !strstr(out,"jmp"));
    else assert((out && strstr(out,"sjmp"))==(distance<=(back?126u:127u)));
    free(out);++cases;
  }
  for(unsigned distance=120;distance<=132;++distance)for(int back=0;back<2;++back) {
    strcpy(body,"_test:\n");
    if(back) {
      strcat(body,"target:\n");
      for(unsigned i=0;i<distance;++i)strcat(body," nop\n");
    }
    strcat(body," jne skip\n ejmp target\nskip:\n");
    if(!back) {
      for(unsigned i=0;i<distance;++i)strcat(body," nop\n");
      strcat(body,"target:\n");
    }
    strcat(body," eret\n");
    char *out=mb_optimize(body,strlen(body),NULL,NULL);
    assert((out && strstr(out,"\tje\ttarget"))==(distance<=(back?126u:127u)));
    free(out);++cases;
  }
  printf("PASS %u exact signed-relative boundary cases\n",cases);
}
int main(int argc,char **argv) {
  if(argc==1){boundaries();return 0;}
  if(argc!=2 && argc!=3)return 2;
  FILE *f=fopen(argv[1],"rb");if(!f)return 2;
  fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
  char *body=(char *)calloc((size_t)size+1,1);
  if(!body || fread(body,1,(size_t)size,f)!=(size_t)size)return 2;
  fclose(f);
  if(argc==3 && !strcmp(argv[2],"--sizes")) {
    for(char *p=body;*p;) {
      char *end=strchr(p,'\n');if(!end)end=p+strlen(p);
      printf("%u\n",bc_instruction_bytes_mode(p,(size_t)(end-p),NULL,NULL,1));
      p=*end?end+1:end;
    }
  } else {
    char *result=mb_optimize(body,(size_t)size,NULL,NULL);
    fputs(result?result:body,stdout);free(result);
  }
  free(body);return 0;
}
