/* SPDX-License-Identifier: GPL-2.0-or-later
 * Relax branches after physical-register optimization. Only the intervening
 * instructions need known Source-mode encodings; unknown spans stay intact. */
#ifndef MX251_MACHINE_BRANCH_H
#define MX251_MACHINE_BRANCH_H
#include "machine_ir.h"
#include "backend_encoding.h"

static const char *mb_inverse(const char *op) {
  static const char *pairs[][2]={{"je","jne"},{"jc","jnc"},{"jz","jnz"},
    {"jg","jle"},{"jsg","jsle"},{"jsge","jsl"}};
  for(unsigned i=0;i<sizeof pairs/sizeof pairs[0];++i)
    for(unsigned j=0;j<2;++j)if(!strcmp(op,pairs[i][j]))return pairs[i][1-j];
  return NULL;
}
static size_t mb_render(const mi_instruction *n,char *text,size_t size) {
  size_t used=(size_t)snprintf(text,size,"\t%s\t",n->opcode);
  for(unsigned k=0;k<n->operands;++k)
    used+=(size_t)snprintf(text+used,size-used,"%s%s",k?",":"",n->operand[k].text);
  used+=(size_t)snprintf(text+used,size-used,"\n");return used;
}
static int mb_range(const mi_program *p,const unsigned *sizes,int from,int to,int omit) {
  if(to<0)return 0;
  unsigned bytes=to<=from?2:0;
  int start=to<=from?to:from+1,end=to<=from?from:to;
  for(int i=start;i<end;++i) {
    const mi_instruction *n=&p->instructions[i];
    if(i==omit || n->removed || !*n->opcode)continue;
    if(!sizes[i])return 0;
    bytes+=sizes[i];if(bytes>(to<=from?128u:127u))return 0;
  }
  return bytes<=(to<=from?128u:127u);
}
static char *mb_optimize(const char *body,size_t length,wide_cost_resolver resolver,const void *context) {
  mi_program p;
  if(!mi_parse(&p,body,length,UINT32_MAX,NULL,NULL,NULL)){mi_destroy(&p);return NULL;}
  /* Numeric labels can be reused across ASxxxx global-label scopes. This
   * function-local pass only rewrites unambiguous label names. */
  for(size_t i=0;i<p.count;++i)if(*p.instructions[i].label)
    for(size_t j=i+1;j<p.count;++j)if(!strcmp(p.instructions[i].label,p.instructions[j].label)) {
      mi_destroy(&p);return NULL;
    }
  unsigned *sizes=(unsigned *)calloc(p.count,sizeof *sizes);
  if(!sizes){mi_destroy(&p);return NULL;}
  for(size_t i=0;i<p.count;++i) {
    mi_instruction *n=&p.instructions[i];
    if(*n->opcode)sizes[i]=bc_instruction_bytes_mode(n->original,strlen(n->original),resolver,context,1);
  }
  unsigned shortened=0,combined=0,deleted=0;int changed;
  do {
    changed=0;
    for(int i=0;i<(int)p.count;++i) {
      mi_instruction *n=&p.instructions[i];
      if(n->removed)continue;
      int to=n->successors[1];
      if((mi_is(n,"ejmp") || mi_is(n,"sjmp")) && to>=0) {
        int next=i+1;
        while(next<to && (p.instructions[next].removed || !*p.instructions[next].opcode))++next;
        if(next==to){n->removed=1;sizes[i]=0;++deleted;++changed;continue;}
        if(mi_is(n,"ejmp") && mb_range(&p,sizes,i,to,-1)) {
          strcpy(n->opcode,"sjmp");n->changed=1;sizes[i]=2;++shortened;++changed;
        }
      }
      const char *inverse=mb_inverse(n->opcode);
      if(!inverse || n->operands!=1 || to<0)continue;
      int jump=i+1;
      while(jump<(int)p.count && (p.instructions[jump].removed ||
            (!*p.instructions[jump].opcode && !*p.instructions[jump].label)))++jump;
      if(jump>=(int)p.count)continue;
      mi_instruction *j=&p.instructions[jump];
      if((!mi_is(j,"ejmp") && !mi_is(j,"sjmp")) || j->successors[1]<0 || to<=jump)continue;
      int after=jump+1;
      while(after<to && (p.instructions[after].removed || !*p.instructions[after].opcode))++after;
      if(after!=to || !mb_range(&p,sizes,i,j->successors[1],jump))continue;
      strcpy(n->opcode,inverse);strcpy(n->operand[0].text,j->operand[0].text);
      n->successors[1]=j->successors[1];n->changed=1;sizes[i]=2;
      j->removed=1;sizes[jump]=0;++combined;++changed;
    }
  } while(changed);
  char *output=NULL;
  if(shortened || combined || deleted) {
    size_t capacity=length+p.count*640+160,used=0;output=(char *)malloc(capacity);
    if(output) {
      for(size_t i=0;i<p.count;++i) {
        mi_instruction *n=&p.instructions[i];if(n->removed)continue;
        if(n->changed)used+=mb_render(n,output+used,capacity-used);
        else used+=(size_t)snprintf(output+used,capacity-used,"%s",n->original);
      }
      snprintf(output+used,capacity-used,"; mx251-final-branches shortened=%u combined=%u deleted=%u\n",shortened,combined,deleted);
    }
  }
  free(sizes);mi_destroy(&p);return output;
}
#endif
