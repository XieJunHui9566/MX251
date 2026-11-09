/* SPDX-License-Identifier: GPL-2.0-or-later
 * Late, translation-unit-local sharing of equal machine function templates.
 * Public entries retain their addresses and ABI2 signatures. Each entry pushes
 * its differing immediates, then jumps to one body. The body owns those slots
 * until return; recursion and calls therefore need no global selector state.
 */
#ifndef MCS251_FUNCTION_SHARE_H
#define MCS251_FUNCTION_SHARE_H
#include "wide_cost.h"
#include "instruction_effects.h"
#include <stdio.h>

enum { FS_INS=2048, FS_LABELS=512, FS_TEXT=256, FS_MEMBERS=16, FS_SLOTS=32 };
typedef struct { char name[FS_TEXT]; unsigned at; } fs_label;
typedef struct {
  char op[16], a[FS_TEXT], b[FS_TEXT];
  int target, depth, delta, slot;
  unsigned branch, width, lower, upper;
} fs_ins;
typedef struct {
  char name[FS_TEXT], signature[FS_TEXT];
  size_t begin,end;
  fs_ins *ins;
  fs_label *labels;
  unsigned ni,nl,abi,eligible,group,member,bytes;
} fs_function;
typedef struct { unsigned width,offset; char value[FS_MEMBERS][FS_TEXT]; } fs_slot;
typedef struct {
  fs_function *fn[FS_MEMBERS]; unsigned count,ns,frame,serial;
  fs_slot slots[FS_SLOTS];
  struct dbuf_s body,entries[FS_MEMBERS];
} fs_group;
typedef int (*fs_operand_barrier)(const char *operand);

static int fs_copy(char *dst,size_t capacity,const char *src,size_t n)
{if(n>=capacity)return 0;memcpy(dst,src,n);dst[n]=0;return 1;}
static int fs_number(const char *text,unsigned *out)
{
  if(*text++!='#' || !isdigit((unsigned char)*text))return 0;
  char *end;unsigned long n=strtoul(text,&end,0);
  if(*end || n>65535)return 0;*out=(unsigned)n;return 1;
}
static const char *fs_inverse(const char *op)
{
  static const char *pairs[]={"jc","jnc","jz","jnz","je","jne","jg","jle","jsl","jsge","jsg","jsle"};
  for(unsigned i=0;i<sizeof pairs/sizeof *pairs;++i)if(!strcmp(op,pairs[i]))return pairs[i^1];
  return NULL;
}
static unsigned fs_width(const char *reg)
{
  mx251_effect_operand o;
  return mx251EffectRegister(reg,&o) && o.gpr?o.width:0;
}
static int fs_stack(const char *s,int *offset)
{
  if(strncmp(s,"@spx",4))return 0;
  s+=4;if(!*s){*offset=0;return 1;}
  if(*s!='-' && *s!='+')return 0;
  int sign=*s++=='-'?-1:1;char *end;
  if(!isdigit((unsigned char)*s))return 0;
  unsigned long n=strtoul(s,&end,0);if(*end || n>32767)return 0;
  *offset=sign*(int)n;return 1;
}
static int fs_label_at(const fs_function *f,const char *name)
{for(unsigned i=0;i<f->nl;++i)if(!strcmp(f->labels[i].name,name))return (int)f->labels[i].at;return -1;}

static int fs_parse(fs_function *f,const char *source,fs_operand_barrier barrier)
{
  int entry=0,wide=0;unsigned isr=1,address=0,opaque=1,indirect=1,bank=1,naked=1;
  f->ins=Safe_alloc(FS_INS*sizeof *f->ins);f->labels=Safe_alloc(FS_LABELS*sizeof *f->labels);
  for(size_t at=f->begin;at<f->end;) {
    size_t end=at;while(end<f->end && source[end]!='\n')++end;
    const char *p=source+at,*last=source+end;at=end+1;
    while(p<last && isspace((unsigned char)*p))++p;
    while(last>p && isspace((unsigned char)last[-1]))--last;
    char line[1024];if(!fs_copy(line,sizeof line,p,(size_t)(last-p)))return 0;
    char name[FS_TEXT],signature[FS_TEXT];
    if(sscanf(line,"; mx251-function begin schema=1 symbol=%255s isr=%u addressTaken=%u opaque=%u indirect=%u",name,&isr,&address,&opaque,&indirect)==5)continue;
    if(sscanf(line,"; mx251-scalar-definition schema=1 symbol=%255s signature=%255s",name,signature)==2) {
      if(strcmp(name,f->name))return 0;strcpy(f->signature,signature);continue;
    }
    if(sscanf(line,"; mx251-bank-contract schema=1 symbol=%255s abi=%u bank=%u naked=%u",name,&f->abi,&bank,&naked)==4)continue;
    if(sscanf(line,"; mx251-backend-choice function=%255s selected=wide",name)==1 && strstr(line," selected=wide "))wide=1;
    if(!*line || *line==';')continue;
    char *comment=strchr(line,';');if(comment)*comment=0;
    size_t n=strlen(line);while(n && isspace((unsigned char)line[n-1]))line[--n]=0;
    if(n && line[n-1]==':') {
      while(n && line[n-1]==':')line[--n]=0;
      if(f->nl==FS_LABELS || !fs_copy(f->labels[f->nl].name,FS_TEXT,line,n))return 0;
      if(fs_label_at(f,line)>=0)return 0;
      if(!strcmp(line,f->name)){if(entry || f->ni)return 0;entry=1;}
      else if(strncmp(line,"__wm_",5))return 0;
      f->labels[f->nl++].at=f->ni;continue;
    }
    /* Bank aliases precede the entry, and are independent of the body. */
    unsigned r,v;int used=0;
    if(sscanf(line,"ar%u = 0x%x%n",&r,&v,&used)==2 && (size_t)used==n && r<8 && r==v)continue;
    if(!entry || f->ni==FS_INS)return 0;
    fs_ins *in=&f->ins[f->ni++];memset(in,0,sizeof *in);in->target=-1;in->depth=INT_MIN;in->slot=-1;
    size_t k=0;while(k<n && !isspace((unsigned char)line[k]))++k;
    if(!fs_copy(in->op,sizeof in->op,line,k))return 0;
    char args[FS_TEXT*2];size_t j=0;
    for(;k<n;++k)if(!isspace((unsigned char)line[k])){if(j+1>=sizeof args)return 0;args[j++]=line[k];}args[j]=0;
    char *second=strchr(args,',');if(second)*second++=0;
    if(!fs_copy(in->a,FS_TEXT,args,strlen(args)) || (second && (!fs_copy(in->b,FS_TEXT,second,strlen(second)) || strchr(second,','))))return 0;
    if(barrier && (barrier(in->a) || barrier(in->b)))return 0;
    if(fs_inverse(in->op))in->branch=2;
    else if(!strcmp(in->op,"sjmp") || !strcmp(in->op,"ejmp"))in->branch=1;
    else if(in->op[0]=='j' || !strcmp(in->op,"djnz") || !strcmp(in->op,"cjne") || !strcmp(in->op,"ret") || !strcmp(in->op,"reti"))return 0;
    if(in->branch && *in->b)return 0;
    if(!strcmp(in->op,"ecall") && (*in->a=='@' || *in->b))return 0;
    if(!strcmp(in->op,"push") || !strcmp(in->op,"pop") || !strcmp(in->op,"pushw")) {
      unsigned w=!strcmp(in->op,"pushw")?2:fs_width(in->a);
      if(!w || *in->b)return 0;in->delta=!strcmp(in->op,"pop")?-(int)w:(int)w;
    }
    if(!strcmp(in->a,"spx")) {
      unsigned amount;
      if((strcmp(in->op,"add") && strcmp(in->op,"sub") && strcmp(in->op,"inc") && strcmp(in->op,"dec")) || !fs_number(in->b,&amount))return 0;
      in->delta=(!strcmp(in->op,"sub") || !strcmp(in->op,"dec"))?-(int)amount:(int)amount;
    } else if(strstr(in->a,"spx") || strstr(in->b,"spx")) {
      int off;const char *operand=strstr(in->a,"spx")?in->a:in->b;
      if(strcmp(in->op,"mov") || !fs_stack(operand,&off))return 0;
    }
    /* Stack-pointer aliases/escapes and computed transfers need a different frame model. */
    const char *bad[]={"dr60","wr60","wr62","spxl","sph","sp"};
    for(unsigned q=0;q<sizeof bad/sizeof *bad;++q)
      if(!strcmp(in->a,bad[q]) || !strcmp(in->b,bad[q]))return 0;
    unsigned bytes=wide_instruction_bytes(line,n);
    in->lower=bytes?bytes:1;in->upper=bytes?bytes:6;f->bytes+=in->lower;
  }
  if(!entry || !wide || !*f->signature || f->abi!=2 || isr || opaque || indirect || bank || naked || f->ni<32)return 0;
  for(unsigned i=0;i<f->ni;++i) {
    fs_ins *in=&f->ins[i];
    if(in->branch && (in->target=fs_label_at(f,in->a))<0)return 0;
    if(!in->branch && (fs_label_at(f,in->a)>=0 || fs_label_at(f,in->b)>=0))return 0;
  }
  unsigned queue[FS_INS],begin=0,end=1;queue[0]=0;f->ins[0].depth=0;
  while(begin<end) {
    unsigned i=queue[begin++];fs_ins *in=&f->ins[i];int depth=in->depth+in->delta;
    if(depth<0 || depth>30000)return 0;
    if(!strcmp(in->op,"eret")){if(depth || *in->a)return 0;continue;}
    int next[2]={in->branch==1?in->target:(int)i+1,in->branch==2?in->target:-1};
    for(unsigned j=0;j<2;++j)if(next[j]>=0) {
      if(next[j]>=(int)f->ni)return 0;
      fs_ins *to=&f->ins[next[j]];
      if(to->depth!=INT_MIN){if(to->depth!=depth)return 0;}
      else {to->depth=depth;queue[end++]=(unsigned)next[j];}
    }
    const char *ops[]={in->a,in->b};
    for(unsigned j=0;j<2;++j)if(*ops[j]=='@' && !strncmp(ops[j],"@spx",4)) {
      int off;if(!fs_stack(ops[j],&off))return 0;
      unsigned width=fs_width(ops[j^1]);int entryOffset=in->depth+off;
      if(!width || (entryOffset<=0 && entryOffset+(int)width-1>=-2) || entryOffset+(int)width-1>in->depth)return 0;
    }
  }
  return end==f->ni;
}

/* Only source-immediate operands vary. Registers, memory transactions, external
 * calls, stack layout and every control-flow edge remain identical. */
static unsigned fs_parameter_width(const fs_ins *in)
{
  if(*in->b!='#')return 0;
  unsigned width=fs_width(in->a),number;
  if(!width)return 0;
  if(!strcmp(in->op,"mov") && width<=2)return width;
  if(strcmp(in->op,"add") && strcmp(in->op,"sub") && strcmp(in->op,"cmp") &&
     strcmp(in->op,"anl") && strcmp(in->op,"orl") && strcmp(in->op,"xrl"))return 0;
  if(!fs_number(in->b,&number) || (width==1 && number>255) || (width==4 && number>32767))return 0;
  return width;
}
static int fs_match(const fs_function *a,const fs_function *b)
{
  if(a->ni!=b->ni || strcmp(a->signature,b->signature))return 0;
  for(unsigned i=0;i<a->ni;++i) {
    const fs_ins *x=&a->ins[i],*y=&b->ins[i];
    if(strcmp(x->op,y->op) || x->target!=y->target || x->depth!=y->depth || x->delta!=y->delta)return 0;
    if(x->branch)continue;
    if(strcmp(x->a,y->a))return 0;
    if(strcmp(x->b,y->b) && (!fs_parameter_width(x) || fs_parameter_width(x)!=fs_parameter_width(y)))return 0;
  }
  return 1;
}
static int fs_slots(fs_group *g)
{
  fs_function *f=g->fn[0];
  for(unsigned i=0;i<f->ni;++i) {
    int differs=0;for(unsigned m=1;m<g->count;++m)differs|=strcmp(f->ins[i].b,g->fn[m]->ins[i].b)!=0;
    if(!differs)continue;
    unsigned width=fs_parameter_width(&f->ins[i]),s;
    if(!width)return 0;
    for(s=0;s<g->ns;++s) {
      if(g->slots[s].width!=width)continue;
      unsigned m=0;while(m<g->count && !strcmp(g->slots[s].value[m],g->fn[m]->ins[i].b))++m;
      if(m==g->count)break;
    }
    if(s==g->ns) {
      if(g->ns==FS_SLOTS)return 0;
      fs_slot *slot=&g->slots[g->ns++];slot->width=width;slot->offset=g->frame+1;
      g->frame+=width==4?4:2;
      for(unsigned m=0;m<g->count;++m)strcpy(slot->value[m],g->fn[m]->ins[i].b);
    }
    f->ins[i].slot=(int)s;
  }
  return 1;
}
static void fs_load(struct dbuf_s *out,const char *reg,unsigned width,int offset)
{
  if(width==4) {
    unsigned r=(unsigned)atoi(reg+2);
    dbuf_printf(out,"\tmov\twr%u,@spx%+d\n\tmov\twr%u,@spx%+d\n",r,offset,r+2,offset+2);
  } else dbuf_printf(out,"\tmov\t%s,@spx%+d\n",reg,offset);
}
static void fs_emit_instruction(fs_group *g,unsigned i,struct dbuf_s *out)
{
  fs_ins *in=&g->fn[0]->ins[i];
  if(!strcmp(in->op,"eret")) {
    if(g->frame)dbuf_printf(out,"\tsub\tspx,#%u\n",g->frame);
    dbuf_append_str(out,"\teret\n");return;
  }
  if(in->slot>=0) {
    fs_slot *slot=&g->slots[in->slot];unsigned width=slot->width;
    int off=(int)slot->offset-(int)g->frame-in->depth+(width==1);
    if(!strcmp(in->op,"mov")){fs_load(out,in->a,width,off);return;}
    /* Preserve the temporary, including across flags consumed by a following
     * branch. R14/R15 and extended WR/DR push/pop do not modify PSW flags. */
    char scratch[16];unsigned r=width==1?15:20;
    mx251_effect_operand dst,tmp;mx251EffectRegister(in->a,&dst);
    for(;;) {
      SNPRINTF(scratch,sizeof scratch,"%sr%u",width==1?"":width==2?"w":"d",r);
      mx251EffectRegister(scratch,&tmp);if(!(dst.gpr&tmp.gpr))break;
      r=width==1?14:24;
    }
    dbuf_printf(out,"\tpush\t%s\n",scratch);
    fs_load(out,scratch,width,off-(int)width);
    dbuf_printf(out,"\t%s\t%s,%s\n\tpop\t%s\n",in->op,in->a,scratch,scratch);return;
  }
  char a[FS_TEXT],b[FS_TEXT];strcpy(a,in->a);strcpy(b,in->b);char *operands[]={a,b};
  for(unsigned k=0;k<2;++k) {
    int off;
    if(fs_stack(operands[k],&off) && in->depth+off<=-3)
      SNPRINTF(operands[k],FS_TEXT,"@spx%+d",off-(int)g->frame);
  }
  dbuf_printf(out,"\t%s\t%s%s%s\n",in->op,a,*b?",":"",b);
}
static unsigned fs_upper(const struct dbuf_s *text)
{
  const char *p=dbuf_get_buf(text);size_t length=dbuf_get_length(text);unsigned total=0;
  for(size_t at=0;at<length;) {
    size_t end=at;while(end<length && p[end]!='\n')++end;
    unsigned bytes=wide_instruction_bytes(p+at,end-at);total+=bytes?bytes:6;at=end+1;
  }
  return total;
}
static unsigned fs_emit(fs_group *g)
{
  unsigned total=0;fs_function *f=g->fn[0];
  for(unsigned m=0;m<g->count;++m) {
    struct dbuf_s *out=&g->entries[m];dbuf_init(out,256);
    int saveA=0;
    for(unsigned s=0;s<g->ns;++s){unsigned number;if(g->slots[s].width==1 && !fs_number(g->slots[s].value[m],&number))saveA=1;}
    if(saveA)dbuf_append_str(out,"\tmov\twr22,wr10\n");
    for(unsigned s=0;s<g->ns;++s) {
      fs_slot *slot=&g->slots[s];const char *value=slot->value[m];unsigned number;
      if(slot->width==4)dbuf_append_str(out,"\tpushw\t#0\n");
      if(fs_number(value,&number))dbuf_printf(out,"\tpushw\t%s\n",value);
      else {
        if(slot->width==1)dbuf_printf(out,"\tmov\ta,%s\n\tmovz\twr20,a\n",value);
        else dbuf_printf(out,"\tmov\twr20,%s\n",value);
        dbuf_append_str(out,"\tpush\twr20\n");
      }
    }
    if(saveA)dbuf_append_str(out,"\tmov\twr10,wr22\n");
    dbuf_printf(out,"\tejmp\t__mfs_%u_0\n",g->serial);total+=fs_upper(out);
  }
  struct dbuf_s *chunks=Safe_alloc(f->ni*sizeof *chunks);
  unsigned *offsets=Safe_alloc((f->ni+1)*sizeof *offsets),*sizes=Safe_alloc(f->ni*sizeof *sizes);
  unsigned char *shorts=Safe_alloc(f->ni),*labels=Safe_alloc(f->ni);
  memset(shorts,0,f->ni);memset(labels,0,f->ni);labels[0]=1;
  for(unsigned i=0;i<f->ni;++i)if(f->ins[i].branch)labels[f->ins[i].target]=1;
  for(unsigned i=0;i<f->ni;++i) {
    dbuf_init(&chunks[i],64);
    if(f->ins[i].branch)sizes[i]=f->ins[i].branch==2?6:4;
    else {fs_emit_instruction(g,i,&chunks[i]);sizes[i]=fs_upper(&chunks[i]);}
  }
  for(int changed=1;changed;) {
    changed=0;offsets[0]=0;for(unsigned i=0;i<f->ni;++i)offsets[i+1]=offsets[i]+sizes[i];
    for(unsigned i=0;i<f->ni;++i)if(f->ins[i].branch && !shorts[i]) {
      int target=f->ins[i].target;
      int distance=target>(int)i?(int)(offsets[target]-offsets[i+1]):(int)offsets[target]-(int)offsets[i]-2;
      if(distance>=-128 && distance<=127){shorts[i]=1;sizes[i]=2;changed=1;}
    }
  }
  dbuf_init(&g->body,4096);
  for(unsigned i=0;i<f->ni;++i) {
    fs_ins *in=&f->ins[i];if(labels[i])dbuf_printf(&g->body,"__mfs_%u_%u:\n",g->serial,i);
    if(!in->branch)dbuf_append(&g->body,dbuf_get_buf(&chunks[i]),dbuf_get_length(&chunks[i]));
    else if(in->branch==1 || shorts[i])
      dbuf_printf(&g->body,"\t%s\t__mfs_%u_%d\n",in->branch==1?(shorts[i]?"sjmp":"ejmp"):in->op,g->serial,in->target);
    else dbuf_printf(&g->body,"\t%s\t__mfs_skip_%u_%u\n\tejmp\t__mfs_%u_%d\n__mfs_skip_%u_%u:\n",fs_inverse(in->op),g->serial,i,g->serial,in->target,g->serial,i);
    total+=sizes[i];dbuf_destroy(&chunks[i]);
  }
  Safe_free(labels);Safe_free(shorts);Safe_free(sizes);Safe_free(offsets);Safe_free(chunks);return total;
}
static void fs_destroy_group(fs_group *g)
{dbuf_destroy(&g->body);for(unsigned m=0;m<g->count;++m)dbuf_destroy(&g->entries[m]);}

static void mcs251ShareFunctions(struct dbuf_s *buffer,fs_operand_barrier barrier)
{
  const char *source=dbuf_c_str(buffer);size_t length=dbuf_get_length(buffer);
  fs_function *functions=NULL;unsigned count=0,capacity=0;fs_function *active=NULL;
  for(size_t at=0;at<length;) {
    size_t end=at;while(end<length && source[end]!='\n')++end;
    char line[512],name[FS_TEXT];
    if(fs_copy(line,sizeof line,source+at,end-at)) {
      if(sscanf(line,"; mx251-function begin schema=1 symbol=%255s",name)==1) {
        if(count==capacity){capacity+=32;functions=Safe_realloc(functions,capacity*sizeof *functions);}
        active=&functions[count++];memset(active,0,sizeof *active);strcpy(active->name,name);active->begin=at;
      } else if(active && sscanf(line,"; mx251-function end schema=1 symbol=%255s",name)==1) {
        active->end=end<length?end+1:end;
        if(!strcmp(active->name,name))active->eligible=fs_parse(active,source,barrier);
        active=NULL;
      }
    }
    at=end+1;
  }
  fs_group **groups=Safe_alloc((count?count:1)*sizeof *groups);unsigned ng=0;
  for(unsigned i=0;i<count;++i)if(functions[i].eligible && !functions[i].group) {
    fs_group *g=Safe_alloc(sizeof *g);memset(g,0,sizeof *g);g->fn[g->count++]=&functions[i];g->serial=ng;
    for(unsigned j=i+1;j<count && g->count<FS_MEMBERS;++j)
      if(functions[j].eligible && !functions[j].group && fs_match(&functions[i],&functions[j]))g->fn[g->count++]=&functions[j];
    if(g->count<2 || !fs_slots(g)){Safe_free(g);continue;}
    unsigned original=0;for(unsigned m=0;m<g->count;++m)original+=g->fn[m]->bytes;
    unsigned shared=fs_emit(g);
    if(shared+128>original){fs_destroy_group(g);for(unsigned k=0;k<functions[i].ni;++k)functions[i].ins[k].slot=-1;Safe_free(g);continue;}
    groups[ng++]=g;
    for(unsigned m=0;m<g->count;++m){g->fn[m]->group=ng;g->fn[m]->member=m;}
  }
  if(ng) {
    struct dbuf_s out;dbuf_init(&out,length);size_t copied=0;
    for(unsigned i=0;i<count;++i)if(functions[i].group) {
      fs_function *f=&functions[i];fs_group *g=groups[f->group-1];unsigned m=f->member;
      dbuf_append(&out,source+copied,f->begin-copied);
      /* Preserve ownership, signature, bank contract and call facts. Old costs
       * and register-effects comments describe the replaced body, so omit them. */
      for(size_t at=f->begin;at<f->end;) {
        size_t end=at;while(end<f->end && source[end]!='\n')++end;
        const char *p=source+at;
        if(!strncmp(p,"; mx251-function begin ",23) || !strncmp(p,"; mx251-scalar-definition ",26) ||
           !strncmp(p,"; mx251-bank-contract ",22) || !strncmp(p,"; mx251-scalar-call ",20))dbuf_append(&out,p,end-at+1);
        at=end+1;
      }
      dbuf_printf(&out,"; mx251-function-sharing schema=1 group=%u members=%u constants=%u frame=%u\n%s:\n",g->serial,g->count,g->ns,g->frame,f->name);
      dbuf_append(&out,dbuf_get_buf(&g->entries[m]),dbuf_get_length(&g->entries[m]));
      if(!m)dbuf_append(&out,dbuf_get_buf(&g->body),dbuf_get_length(&g->body));
      dbuf_printf(&out,"; mx251-function end schema=1 symbol=%s\n",f->name);copied=f->end;
    }
    dbuf_append(&out,source+copied,length-copied);dbuf_destroy(buffer);*buffer=out;
  }
  for(unsigned i=0;i<ng;++i){fs_destroy_group(groups[i]);Safe_free(groups[i]);}Safe_free(groups);
  for(unsigned i=0;i<count;++i){Safe_free(functions[i].ins);Safe_free(functions[i].labels);}Safe_free(functions);
}
#endif
