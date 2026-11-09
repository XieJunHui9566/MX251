/* SPDX-License-Identifier: GPL-2.0-or-later
 * Physical machine IR shared by the byte and native-wide generators.
 * Byte-slice liveness, CFG copy equivalence and local destination coalescing
 * operate on the instructions that will actually be assembled. */
#ifndef MX251_MACHINE_IR_H
#define MX251_MACHINE_IR_H
#include "instruction_effects.h"
#include <stdio.h>

enum { MI_REGS=32, MI_OPERANDS=3 };
typedef struct {
  char text[192];
  int reg,width,indirect,memory;
} mi_operand;
typedef struct {
  char *original;
  char opcode[24],label[192];
  mi_operand operand[MI_OPERANDS];
  unsigned operands;
  uint32_t reads,writes,in,out;
  int successors[2],branch,terminal,unknown,pure,copy,removed,changed,call;
  uint32_t equal[MI_REGS];
  short constant[MI_REGS];
} mi_instruction;
typedef int (*mi_resolver)(const char *,void *);
typedef int (*mi_call_resolver)(const char *,uint32_t *,uint32_t *,void *);
typedef struct {
  mi_instruction *instructions;
  size_t count;
  uint32_t exitLive;
  unsigned eliminated,forwarded,coalesced,folded;
} mi_program;

static uint32_t mi_mask(int reg,int width) {
  return reg<0?0:mcs251RegisterBytes((unsigned)reg,(unsigned)width);
}
static void mi_identity(uint32_t *eq) {
  for(unsigned r=0;r<MI_REGS;++r)eq[r]=UINT32_C(1)<<r;
}
static void mi_kill(uint32_t *eq,uint32_t writes) {
  for(unsigned r=0;r<MI_REGS;++r)
    eq[r]=(writes&(UINT32_C(1)<<r))?UINT32_C(1)<<r:eq[r]&~writes;
}
static int mi_register(const char *text,int *width) {
  if(strlen(text)==3 && text[0]=='a' && text[1]=='r' && text[2]>='0' && text[2]<='7') {
    *width=1;return text[2]-'0';
  }
  mx251_effect_operand operand;
  if(!mx251EffectRegister(text,&operand) || !operand.gpr)return -1;
  *width=(int)operand.width;
  for(int r=0;r<32;++r)if(operand.gpr&(UINT32_C(1)<<r))return r;
  return -1;
}
static void mi_operand_parse(mi_operand *operand) {
  operand->reg=-1;operand->width=0;operand->indirect=operand->text[0]=='@';
  operand->reg=mi_register(operand->text+operand->indirect,&operand->width);
}
static int mi_is(const mi_instruction *n,const char *opcode) {return !strcmp(n->opcode,opcode);}
static int mi_jump(const char *s) {
  return !strcmp(s,"sjmp") || !strcmp(s,"ljmp") || !strcmp(s,"ejmp") || !strcmp(s,"ajmp");
}
static int mi_condition(const char *s) {
  return !strcmp(s,"je") || !strcmp(s,"jne") || !strcmp(s,"jc") || !strcmp(s,"jnc") ||
    !strcmp(s,"jz") || !strcmp(s,"jnz") || !strcmp(s,"jg") || !strcmp(s,"jge") ||
    !strcmp(s,"jl") || !strcmp(s,"jle") || !strcmp(s,"jb") || !strcmp(s,"jnb") ||
    !strcmp(s,"jbc") || !strcmp(s,"cjne") || !strcmp(s,"djnz") ||
    !strcmp(s,"jslt") || !strcmp(s,"jsle") || !strcmp(s,"jsgt") || !strcmp(s,"jsge") ||
    !strcmp(s,"jsl") || !strcmp(s,"jsg");
}
static void mi_effects(mi_instruction *n,const char *body,size_t length) {
  char line[640];size_t used=(size_t)snprintf(line,sizeof line,"%s",n->opcode);
  for(unsigned k=0;k<n->operands;++k)
    used+=(size_t)snprintf(line+used,sizeof line-used,"%s%s",k?",":" ",n->operand[k].text);
  mx251_effects e=mx251InstructionEffects(line,used,body,length);
  n->reads=e.reads;n->writes=e.writes;n->unknown=e.unknown!=0;
  n->pure=n->copy=0;
  if(!*n->opcode || *n->label) {n->reads=n->writes=0;n->unknown=0;return;}
  if(mi_jump(n->opcode)) {n->reads=n->writes=0;n->unknown=0;return;}
  if(mi_condition(n->opcode)) {
    /* Flags are deliberately not killed by this pass. A-changing operations
     * are never dead-write candidates; conditional branches remain intact. */
    if(n->operands==1) {
      n->reads=(mi_is(n,"jz") || mi_is(n,"jnz"))?UINT32_C(1)<<11:0;
      n->writes=0;n->unknown=0;
    }
    if(mi_is(n,"cjne") && n->operands==3 && n->operand[0].reg>=0 &&
       (n->operand[1].reg>=0 || n->operand[1].memory || n->operand[1].text[0]=='#')) {
      n->reads=mi_mask(n->operand[0].reg,n->operand[0].width)|mi_mask(n->operand[1].reg,n->operand[1].width);
      n->writes=0;n->unknown=0;
    }
    if((mi_is(n,"jb") || mi_is(n,"jnb") || mi_is(n,"jbc")) &&
       n->operands==2 && n->operand[0].memory) {n->reads=n->writes=0;n->unknown=0;}
    return;
  }
  mi_operand *d=&n->operand[0],*s=&n->operand[1];
  if(n->operands==2 && (mi_is(n,"mov") || mi_is(n,"movz") || mi_is(n,"movs")) &&
     (d->reg>=0 || d->memory) && (s->reg>=0 || s->memory || s->text[0]=='#') &&
     !d->indirect && !s->indirect) {
    n->reads=mi_mask(s->reg,s->width);n->writes=mi_mask(d->reg,d->width);n->unknown=0;
  }
  if((mi_is(n,"push") || mi_is(n,"pop")) && n->operands==1 && d->reg>=0 && !d->indirect) {
    n->reads=mi_is(n,"push")?mi_mask(d->reg,d->width):0;
    n->writes=mi_is(n,"pop")?mi_mask(d->reg,d->width):0;n->unknown=0;
  }
  if(n->operands==2 && d->reg>=0 && !d->indirect && !s->indirect &&
     (s->reg>=0 || s->memory || s->text[0]=='#') &&
     (mi_is(n,"cmp") || mi_is(n,"add") || mi_is(n,"addc") || mi_is(n,"sub") ||
      mi_is(n,"subb") || mi_is(n,"anl") || mi_is(n,"orl") || mi_is(n,"xrl"))) {
    n->reads=mi_mask(d->reg,d->width)|mi_mask(s->reg,s->width);
    n->writes=mi_is(n,"cmp")?0:mi_mask(d->reg,d->width);n->unknown=0;
    if(mi_is(n,"sub") && d->reg==s->reg && d->width==s->width)n->reads=0;
  }
  if(n->operands==1 && (mi_is(n,"clr") || mi_is(n,"setb")) &&
     (d->reg>=0 || d->memory || !strcmp(d->text,"c"))) {
    n->reads=0;n->writes=mi_mask(d->reg,d->width);n->unknown=0;
  }
  if((mi_is(n,"mov") || mi_is(n,"movz") || mi_is(n,"movs")) && n->operands==2 &&
     d->reg>=0 && !d->indirect && (s->reg>=0 || s->text[0]=='#') && !s->indirect) {
    n->reads=mi_mask(s->reg,s->width);n->writes=mi_mask(d->reg,d->width);n->unknown=0;
    n->pure=!(n->writes&MCS251_AB_BYTES);
    n->copy=mi_is(n,"mov") && s->reg>=0 && s->width==d->width;
  }
}
static int mi_parse(mi_program *p,const char *body,size_t length,uint32_t exitLive,mi_resolver resolve,mi_call_resolver call,void *context) {
  memset(p,0,sizeof *p);p->exitLive=exitLive;
  size_t capacity=1;for(size_t i=0;i<length;++i)capacity+=body[i]=='\n';
  p->instructions=(mi_instruction *)calloc(capacity,sizeof *p->instructions);
  if(!p->instructions)return 0;
  for(size_t begin=0;begin<length;) {
    size_t end=begin;while(end<length && body[end]!='\n')++end;
    mi_instruction *n=&p->instructions[p->count++];size_t bytes=end-begin;
    n->original=(char *)malloc(bytes+2);if(!n->original)return 0;
    memcpy(n->original,body+begin,bytes);n->original[bytes]='\n';n->original[bytes+1]=0;
    const char *s=n->original;while(isspace((unsigned char)*s))++s;
    if(*s && *s!=';') {
      char cleaned[640];size_t size=0;
      while(*s && *s!=';' && *s!='\r' && *s!='\n') {
        if(size+1>=sizeof cleaned)return 0;
        cleaned[size++]=*s++;
      }
      while(size && isspace((unsigned char)cleaned[size-1]))--size;
      cleaned[size]=0;
      if(size && cleaned[size-1]==':') {
        while(size && cleaned[size-1]==':')cleaned[--size]=0;
        if(size>=sizeof n->label)return 0;
        strcpy(n->label,cleaned);
      } else {
        s=cleaned;size_t k=0;
        while(*s && !isspace((unsigned char)*s)) {
          if(k+1>=sizeof n->opcode)return 0;
          n->opcode[k++]=(char)tolower((unsigned char)*s++);
        }
        n->opcode[k]=0;
        while(isspace((unsigned char)*s))++s;
        while(*s) {
          if(n->operands==MI_OPERANDS)return 0;
          mi_operand *o=&n->operand[n->operands++];k=0;
          while(*s && *s!=',') {
            if(!isspace((unsigned char)*s)) {
              if(k+1>=sizeof o->text)return 0;
              o->text[k++]=*s;
            }
            ++s;
          }
          o->text[k]=0;mi_operand_parse(o);
          if(!strcmp(o->text,"psw") || !strcmp(o->text,"psw1"))return 0;
          if(resolve && o->reg<0)o->memory=resolve(o->text,context);
          if(o->memory<0)return 0;
          if(!strcmp(o->text,"dpl") || !strcmp(o->text,"dph") || !strcmp(o->text,"dptr") ||
             !strcmp(o->text,"dpx") || !strcmp(o->text,"spx"))o->memory=1;
          if(*s==',')++s;
        }
      }
    }
    mi_effects(n,body,length);mi_identity(n->equal);
    if(mi_is(n,"ecall") || mi_is(n,"lcall") || mi_is(n,"acall")) {
      n->call=1;
      if(call && n->operands==1 && call(n->operand[0].text,&n->reads,&n->writes,context))n->unknown=0;
    }
    n->successors[0]=n->successors[1]=-1;
    begin=end+1;
  }
  for(size_t i=0;i<p->count;++i) {
    mi_instruction *n=&p->instructions[i];
    /* A machine-local helper has no C return contract. Its return can expose
     * any GPR, even when the enclosing C function returns only one word. */
    if(n->call && n->operands==1)
      for(size_t j=0;j<p->count;++j)
        if(*p->instructions[j].label && !strcmp(n->operand[0].text,p->instructions[j].label))
          p->exitLive=UINT32_MAX;
    n->terminal=mi_is(n,"ret") || mi_is(n,"eret") || mi_is(n,"reti");
    n->branch=mi_jump(n->opcode) || mi_condition(n->opcode);
    if(!n->terminal && !mi_jump(n->opcode) && i+1<p->count)n->successors[0]=(int)i+1;
    if(n->branch) {
      if(!n->operands)return 0;
      const char *target=n->operand[n->operands-1].text;
      /* Numeric PC-relative assembly cannot be relocated by deleting code. */
      if(strchr(target,'+') || strchr(target,'-') || *target=='$' || *target=='#' || *target=='@')return 0;
      int found=-1;
      for(size_t j=0;j<p->count;++j)if(*p->instructions[j].label && !strcmp(target,p->instructions[j].label)) {found=(int)j;break;}
      n->successors[1]=found;
      if(found<0)n->reads=UINT32_MAX;
    }
    if(mi_is(n,"jmp") || (n->opcode[0]=='j' && !n->branch))return 0;
  }
  return 1;
}
static void mi_destroy(mi_program *p) {
  if(p->instructions)for(size_t i=0;i<p->count;++i)free(p->instructions[i].original);
  free(p->instructions);memset(p,0,sizeof *p);
}
static void mi_liveness(mi_program *p) {
  for(size_t i=0;i<p->count;++i)p->instructions[i].in=p->instructions[i].out=0;
  int changed;
  do {
    changed=0;
    for(size_t i=p->count;i-->0;) {
      mi_instruction *n=&p->instructions[i];uint32_t out=0;
      for(int k=0;k<2;++k)if(n->successors[k]>=0)out|=p->instructions[n->successors[k]].in;
      if(n->terminal)out|=p->exitLive;
      if(i+1==p->count && !n->terminal)out|=UINT32_MAX;
      uint32_t in=n->removed?out:n->reads|(out&~n->writes);
      changed|=n->in!=in || n->out!=out;n->in=in;n->out=out;
    }
  } while(changed);
}
static void mi_transfer(const mi_instruction *n,const uint32_t *in,uint32_t *out) {
  memcpy(out,in,MI_REGS*sizeof *out);
  if(n->removed)return;
  if(n->unknown) {mi_identity(out);return;}
  mi_kill(out,n->writes);
  if(n->copy) {
    int dest=n->operand[0].reg,source=n->operand[1].reg,width=n->operand[0].width;
    /* Parallel byte assignment: source snapshots come from the input state. */
    for(int a=0;a<width;++a)for(int r=0;r<MI_REGS;++r) {
      int b=r>=dest && r<dest+width?source+r-dest:r;
      if(in[source+a]&(UINT32_C(1)<<b)) {
        out[dest+a]|=UINT32_C(1)<<r;out[r]|=UINT32_C(1)<<(dest+a);
      }
    }
  }
}
static void mi_equivalences(mi_program *p) {
  /* Intersection at joins is the must-equality lattice. Starting at identity
   * discovers only equalities established on every incoming path. */
  for(size_t i=0;i<p->count;++i)mi_identity(p->instructions[i].equal);
  int changed;
  do {
    changed=0;
    for(size_t i=0;i<p->count;++i) {
      uint32_t in[MI_REGS],next[MI_REGS];int incoming=0;
      mi_identity(in);
      for(size_t j=0;j<p->count;++j)for(int k=0;k<2;++k)
        if(p->instructions[j].successors[k]==(int)i) {
          if(!incoming++ && i)memcpy(in,p->instructions[j].equal,sizeof in);
          else for(int r=0;r<MI_REGS;++r)in[r]&=p->instructions[j].equal[r];
        }
      mi_transfer(&p->instructions[i],in,next);
      if(memcmp(next,p->instructions[i].equal,sizeof next)) {
        memcpy(p->instructions[i].equal,next,sizeof next);changed=1;
      }
    }
  } while(changed);
}
static void mi_input_equal(const mi_program *p,size_t index,uint32_t *eq) {
  int incoming=0;mi_identity(eq);
  for(size_t j=0;j<p->count;++j)for(int k=0;k<2;++k)
    if(p->instructions[j].successors[k]==(int)index) {
      if(!incoming++ && index)memcpy(eq,p->instructions[j].equal,MI_REGS*sizeof *eq);
      else for(int r=0;r<MI_REGS;++r)eq[r]&=p->instructions[j].equal[r];
    }
}
static int mi_same(const uint32_t *eq,int a,int b,int width) {
  for(int k=0;k<width;++k)if(!(eq[a+k]&(UINT32_C(1)<<(b+k))))return 0;
  return 1;
}
static void mi_set_register(mi_operand *o,int reg) {
  snprintf(o->text,sizeof o->text,"%s%sr%d",o->indirect?"@":"",o->width==4?"d":o->width==2?"w":"",reg);
  o->reg=reg;
}
/* Allocated homes precede the emitter's DR16/DR20 scratch registers. This
 * lets a store read its original high GPR instead of retaining a scratch
 * copy solely because that copy has a smaller register number. */
static int mi_register_rank(int reg) {
  return reg<16?reg:reg>=24?reg-8:reg+8;
}
static int mi_forward(mi_program *p,const char *body,size_t length) {
  int changes=0;mi_equivalences(p);
  for(size_t i=0;i<p->count;++i) {
    mi_instruction *n=&p->instructions[i];uint32_t eq[MI_REGS];
    if(n->removed || n->unknown || n->branch || n->terminal || !*n->opcode)continue;
    mi_input_equal(p,i,eq);
    if(n->copy && mi_same(eq,n->operand[0].reg,n->operand[1].reg,n->operand[0].width)) {
      n->removed=1;++p->eliminated;++changes;continue;
    }
    /* Preserve opcode/operand encoding classes; only native GPR sources are
     * substituted. Byte A/B and special-register encodings are left intact. */
    if(n->operands!=2 || !(mi_is(n,"mov") || mi_is(n,"movz") || mi_is(n,"movs") ||
       mi_is(n,"cmp") || mi_is(n,"add") || mi_is(n,"sub") || mi_is(n,"anl") || mi_is(n,"orl") || mi_is(n,"xrl")))continue;
    mi_operand *o=&n->operand[1];
    if(o->reg<0 || o->indirect || (mi_mask(o->reg,o->width)&MCS251_AB_BYTES))continue;
    int best=o->reg;
    for(int r=0;r<MI_REGS;r+=o->width) {
      if(!mcs251RegisterEncodable(r,o->width) || (mi_mask(r,o->width)&MCS251_AB_BYTES) ||
         mi_register_rank(r)>=mi_register_rank(best) || !mi_same(eq,r,o->reg,o->width))continue;
      best=r;
    }
    if(best!=o->reg) {
      mi_set_register(o,best);n->changed=1;mi_effects(n,body,length);++p->forwarded;++changes;
    }
  }
  return changes;
}
static int mi_coalesce(mi_program *p,const char *body,size_t length) {
  int changes=0;mi_liveness(p);
  for(size_t end=0;end<p->count;++end) {
    mi_instruction *copy=&p->instructions[end];
    if(copy->removed || !copy->copy)continue;
    int source=copy->operand[1].reg,dest=copy->operand[0].reg,width=copy->operand[0].width;
    uint32_t sm=mi_mask(source,width),dm=mi_mask(dest,width);
    if(width<2 || source==dest || ((sm|dm)&MCS251_AB_BYTES) || (sm&dm) || (copy->out&sm))continue;
    size_t start=end;int found=0;
    while(start) {
      mi_instruction *n=&p->instructions[--start];
      if(n->removed)continue;
      if(n->unknown || n->call || n->branch || n->terminal || *n->label || ((n->reads|n->writes)&dm))break;
      /* MUL/DIV also write an implicit half of the enclosing register.
       * Renaming one explicit operand cannot rename a partial result. */
      if((mi_is(n,"mul") || mi_is(n,"div")) && ((n->reads|n->writes)&sm))break;
      int valid=1;
      for(unsigned k=0;k<n->operands;++k) {
        mi_operand *o=&n->operand[k];uint32_t mask=mi_mask(o->reg,o->width);
        if(!(mask&sm))continue;
        if((mask&sm)!=mask || !mcs251RegisterEncodable(dest+o->reg-source,o->width)) {valid=0;break;}
      }
      if(!valid)break;
      if(n->writes&sm) {
        if((n->writes&sm)==sm && !(n->reads&sm)) {
          found=!(n->in&dm);break;
        }
      }
    }
    if(!found)continue;
    for(size_t j=start;j<end;++j) {
      mi_instruction *n=&p->instructions[j];if(n->removed)continue;
      for(unsigned k=0;k<n->operands;++k) {
        mi_operand *o=&n->operand[k];
        if(mi_mask(o->reg,o->width)&sm) {mi_set_register(o,dest+o->reg-source);n->changed=1;}
      }
      mi_effects(n,body,length);
    }
    copy->removed=1;++p->coalesced;++changes;mi_liveness(p);
  }
  return changes;
}

static int mi_literal(const char *s,uint32_t *value) {
  if(*s++!='#' || !*s)return 0;
  char *end;unsigned long v=strtoul(s,&end,0);
  if(*end)return 0;
  *value=(uint32_t)v;return 1;
}
static int mi_constant_value(const short *state,const mi_operand *operand,uint32_t *value) {
  if(mi_literal(operand->text,value))return 1;
  if(operand->reg<0 || operand->indirect)return 0;
  *value=0;
  for(int k=0;k<operand->width;++k) {
    if(state[operand->reg+k]<0)return 0;
    *value=(*value<<8)|(unsigned)state[operand->reg+k];
  }
  return 1;
}
static void mi_constant_input(const mi_program *p,size_t index,short *state) {
  int incoming=0;for(int r=0;r<MI_REGS;++r)state[r]=-1;
  for(size_t j=0;j<p->count;++j)for(int k=0;k<2;++k)
    if(p->instructions[j].successors[k]==(int)index) {
      if(!incoming++ && index)memcpy(state,p->instructions[j].constant,MI_REGS*sizeof *state);
      else for(int r=0;r<MI_REGS;++r)if(state[r]!=p->instructions[j].constant[r])state[r]=-1;
    }
}
static void mi_constant_transfer(const mi_instruction *n,const short *in,short *out) {
  memcpy(out,in,MI_REGS*sizeof *out);if(n->removed)return;
  for(int r=0;r<MI_REGS;++r)if(n->unknown || (n->writes&(UINT32_C(1)<<r)))out[r]=-1;
  if(n->unknown || !n->writes || !n->operands)return;
  const mi_operand *d=&n->operand[0],*s=&n->operand[1];
  if(d->reg<0 || d->indirect)return;
  uint32_t a=0,b=0,value=0;int known=0;
  int ka=mi_constant_value(in,d,&a),kb=n->operands==2 && mi_constant_value(in,s,&b);
  if(mi_is(n,"mov") || mi_is(n,"movz") || mi_is(n,"movs")) {
    if(kb) {
      value=b;known=1;
      if(mi_is(n,"movs") && s->width && s->width<4 && (b&(UINT32_C(1)<<(8*s->width-1))))
        value|=~((UINT32_C(1)<<(8*s->width))-1);
    } else if(s->reg>=0 && !s->indirect) {
      for(int k=0;k<d->width;++k) {
        int source=s->width-d->width+k;
        if(source>=0)out[d->reg+k]=in[s->reg+source];
        else if(mi_is(n,"movz"))out[d->reg+k]=0;
      }
    }
  } else if(mi_is(n,"clr")) {value=0;known=1;}
  else if(mi_is(n,"sub") && d->reg==s->reg && d->width==s->width) {value=0;known=1;}
  else if(ka && kb) {
    known=1;
    if(mi_is(n,"add") || mi_is(n,"inc"))value=a+b;
    else if(mi_is(n,"sub") || mi_is(n,"dec"))value=a-b;
    else if(mi_is(n,"anl"))value=a&b;
    else if(mi_is(n,"orl"))value=a|b;
    else if(mi_is(n,"xrl"))value=a^b;
    else known=0;
  }
  if(known)for(int k=0;k<d->width;++k)out[d->reg+k]=(value>>(8*(d->width-1-k)))&255;
}
static int mi_nz_dead(const mi_program *p,size_t start) {
  unsigned char *seen=(unsigned char *)calloc(p->count,1);
  int *queue=(int *)malloc(p->count*sizeof *queue);size_t head=0,tail=0;int dead=1;
  if(!seen || !queue) {free(seen);free(queue);return 0;}
  for(int k=0;k<2;++k) {
    int next=p->instructions[start].successors[k];
    if(next>=0 && !seen[next]) {seen[next]=1;queue[tail++]=next;}
  }
  while(head<tail && dead) {
    const mi_instruction *n=&p->instructions[queue[head++]];
    if(!n->removed) {
      if(n->unknown || n->terminal || mi_is(n,"je") || mi_is(n,"jne") ||
         mi_is(n,"jg") || mi_is(n,"jge") || mi_is(n,"jl") || mi_is(n,"jle") ||
         mi_is(n,"jslt") || mi_is(n,"jsle") || mi_is(n,"jsgt") || mi_is(n,"jsge") ||
         mi_is(n,"jsl") || mi_is(n,"jsg")) {dead=0;break;}
      if(mi_is(n,"cmp") || mi_is(n,"cjne") ||
         ((mi_is(n,"add") || mi_is(n,"sub") || mi_is(n,"addc") || mi_is(n,"subb") ||
           mi_is(n,"anl") || mi_is(n,"orl") || mi_is(n,"xrl") || mi_is(n,"inc") || mi_is(n,"dec")) &&
          n->operand[0].reg>=0) || (mi_is(n,"clr") && n->operand[0].reg==11))continue;
    }
    for(int k=0;k<2;++k) {
      int next=n->successors[k];
      if(next<0) {if(k==1 && n->branch)dead=0;continue;}
      if(!seen[next]) {seen[next]=1;queue[tail++]=next;}
    }
  }
  free(seen);free(queue);return dead;
}
static int mi_constants(mi_program *p,const char *body,size_t length) {
  for(size_t i=0;i<p->count;++i)for(int r=0;r<MI_REGS;++r)p->instructions[i].constant[r]=-1;
  int changed;
  do {
    changed=0;
    for(size_t i=0;i<p->count;++i) {
      short in[MI_REGS],out[MI_REGS];mi_constant_input(p,i,in);
      mi_constant_transfer(&p->instructions[i],in,out);
      if(memcmp(out,p->instructions[i].constant,sizeof out)) {
        memcpy(p->instructions[i].constant,out,sizeof out);changed=1;
      }
    }
  } while(changed);
  int changes=0;
  for(size_t i=0;i<p->count;++i) {
    mi_instruction *n=&p->instructions[i];short in[MI_REGS];uint32_t a,b;
    if(n->removed)continue;
    mi_constant_input(p,i,in);
    if(n->pure && mi_is(n,"mov") && mi_constant_value(in,&n->operand[0],&a) &&
       mi_constant_value(in,&n->operand[1],&b) && a==b) {
      n->removed=1;++p->eliminated;++changes;continue;
    }
    /* 251 CJNE writes CY, N and Z. Preserve CY explicitly and fold only
     * after checking that every following path overwrites N/Z before use. */
    if(mi_is(n,"cjne") && n->operands==3 && n->operand[0].reg>=0 &&
       mi_constant_value(in,&n->operand[0],&a) && mi_constant_value(in,&n->operand[1],&b) && a==b && mi_nz_dead(p,i)) {
      strcpy(n->opcode,"clr");n->operands=1;memset(n->operand,0,sizeof n->operand);
      strcpy(n->operand[0].text,"c");mi_operand_parse(&n->operand[0]);
      n->branch=0;n->successors[1]=-1;n->changed=1;
      mi_effects(n,body,length);++p->folded;++changes;
    }
  }
  return changes;
}
static char *mi_optimize(const char *body,size_t length,uint32_t exitLive,mi_resolver resolve,mi_call_resolver call,void *context,unsigned *counts) {
  mi_program p;
  if(!mi_parse(&p,body,length,exitLive,resolve,call,context)) {mi_destroy(&p);return NULL;}
  int changed;
  do {
    changed=mi_constants(&p,body,length);
    changed+=mi_forward(&p,body,length);
    changed+=mi_coalesce(&p,body,length);
    mi_liveness(&p);
    for(size_t i=0;i<p.count;++i) {
      mi_instruction *n=&p.instructions[i];
      if(!n->removed && n->pure && !(n->out&n->writes)) {
        n->removed=1;++p.eliminated;++changed;
      }
    }
  } while(changed);
  size_t capacity=length+p.count*640+160;char *output=(char *)malloc(capacity);size_t used=0;
  if(output) {
    for(size_t i=0;i<p.count;++i) {
      mi_instruction *n=&p.instructions[i];
      if(n->removed)continue;
      if(!n->changed)used+=(size_t)snprintf(output+used,capacity-used,"%s",n->original);
      else {
        used+=(size_t)snprintf(output+used,capacity-used,"\t%s\t",n->opcode);
        for(unsigned k=0;k<n->operands;++k)used+=(size_t)snprintf(output+used,capacity-used,"%s%s",k?",":"",n->operand[k].text);
        output[used++]='\n';output[used]=0;
      }
      if(n->call && !n->unknown && n->writes!=UINT32_MAX)
        used+=(size_t)snprintf(output+used,capacity-used,"; mx251-machine-call callee=%s writes=%08lx\n",n->operand[0].text,(unsigned long)n->writes);
    }
    used+=(size_t)snprintf(output+used,capacity-used,"; mx251-machine-ir eliminated=%u forwarded=%u coalesced=%u folded=%u\n",p.eliminated,p.forwarded,p.coalesced,p.folded);
  }
  if(counts) {counts[0]=p.eliminated;counts[1]=p.forwarded;counts[2]=p.coalesced;}
  mi_destroy(&p);return output;
}
#endif
