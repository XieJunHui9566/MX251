/* SPDX-License-Identifier: GPL-2.0-or-later
 * C ISR 的保存槽是编译器私有帧，不是 C 对象或公开的汇编参数接口。
 * 只处理无调用、无局部帧、无栈/间接访问、无内联汇编的普通 bank0 ISR。
 * 从最终指令和真实符号类型求核心寄存器效果；未知指令/地址完整回退。
 * 不移动或改写任何函数体访存。PSW1 首存末取始终保留。 */
#include "wide_cost.h"
#include "instruction_effects.h"
#include "wide.h"
#include "wide_ordered_contract.h"
enum { LI_LINES=2048, LI_TEXT=512, LI_SAVES=64 };
typedef struct {lineNode *node;char op[24],arg[3][LI_TEXT];unsigned nargs,bytes;bool label,empty;} li_line;
typedef struct {uint32_t gpr;unsigned special;bool flags,memory,bit,unknown;} li_operand;
typedef struct {uint32_t reads,writes;unsigned specialReads,specialWrites;bool flags;} li_effects;

static bool li_parse(lineNode *node,li_line *r)
{
  memset(r,0,sizeof *r);r->node=node;const char *p=node->line;
  if(!p){r->empty=true;return true;}
  while(isspace((unsigned char)*p))++p;
  if(!*p || *p==';'){r->empty=true;return true;}
  size_t len=strcspn(p,";");while(len && isspace((unsigned char)p[len-1]))--len;
  if(len && p[len-1]==':'){r->label=true;return true;}
  /* 旧后端的 bank0 符号别名没有执行效果。 */
  unsigned ar,n;int consumed=0;
  if(sscanf(p,"ar%u = 0x%x%n",&ar,&n,&consumed)==2 && consumed==(int)len && ar<8 && n==ar){r->empty=true;return true;}
  unsigned k=0;while(k<len && !isspace((unsigned char)p[k])) {
    if(k+1>=sizeof r->op)return false;r->op[k]=(char)tolower((unsigned char)p[k]);++k;
  }
  r->op[k]=0;
  while(k<len && isspace((unsigned char)p[k]))++k;
  if(k==len)return true;
  unsigned a=0,j=0;
  for(;k<len;++k) {
    if(isspace((unsigned char)p[k]))continue;
    if(p[k]==','){if(!j || ++a==3)return false;r->arg[a-1][j]=0;j=0;continue;}
    if(j+1>=LI_TEXT)return false;r->arg[a][j++]=p[k];
  }
  if(!j)return false;r->arg[a][j]=0;r->nargs=a+1;return true;
}

/* 只把分配的 DATA 对象及 ISA 固定普通端口视为非核心存储。
 * 其它外设地址可能改变存储映射或执行模式，未取得能力证明时拒绝。 */
static li_operand li_direct(unsigned address,bool bit)
{
  li_operand r={0};r.bit=bit;unsigned mask=bit?1u<<(address&7):255u;
  if(bit)address&=0xf8;
  if(address==0xe0)r.gpr=UINT32_C(1)<<MCS251_BYTE_A;
  else if(address==0xf0)r.gpr=UINT32_C(1)<<MCS251_BYTE_B;
  else if(!bit && (address==0x82 || address==0x83))r.special=MX251_SPECIAL_DPTR;
  else if(!bit && address==0x84)r.special=MX251_SPECIAL_DPX;
  else if(address==0xd0 || address==0xd1 || address==0x81 || address==0xbe)r.unknown=true;
  else if(address==0x80 || address==0x90 || address==0xa0 || address==0xb0)r.memory=true;
  else if(!wide_asm_core_bits(address,mask) && (mcs251OrderedAsmBits[address]&mask)==mask)r.memory=true;
  else r.unknown=true;
  return r;
}

static li_operand li_operand_info(const char *text)
{
  li_operand r={0};mx251_effect_operand reg;
  if(*text=='#' && text[1])return r;
  if(!strcmp(text,"c") || !strcmp(text,"cy")){r.flags=true;return r;}
  if(!strcmp(text,"dpxl")){r.special=MX251_SPECIAL_DPX;return r;}
  if(mx251EffectRegister(text,&reg)){
    r.gpr=reg.gpr;r.special=reg.special;r.unknown=(reg.special&MX251_SPECIAL_SPX)!=0;return r;
  }
  if(strlen(text)==3 && text[0]=='a' && text[1]=='r' && text[2]>='0' && text[2]<='7'){
    r.gpr=UINT32_C(1)<<(text[2]-'0');return r;
  }
  if(*text=='@'){r.unknown=true;return r;}
  memmap *spaces[]={data,bit,sfr,sfrbit};
  for(unsigned i=0;i<sizeof spaces/sizeof spaces[0];++i)if(spaces[i])
    for(const set *node=spaces[i]->syms;node;node=node->next) {
      const symbol *s=node->item;if(!s || !s->rname[0] || getSize(s->type)<=0)continue;
      int offset=wide_cost_symbol_offset(text,s->rname,(unsigned)getSize(s->type));if(offset<0)continue;
      sym_link *spec=getSpec(s->type);
      if(spaces[i]==bit && !SPEC_ABSA(spec) && !s->onStack && !s->_isparm){r.memory=r.bit=true;return r;}
      if(spaces[i]==data && !SPEC_ABSA(spec) && !s->onStack && !s->_isparm){r.memory=true;return r;}
      if(SPEC_ABSA(spec) && (spaces[i]==sfr || spaces[i]==sfrbit))
        return li_direct((unsigned)SPEC_ADDR(spec)+(unsigned)offset,spaces[i]==sfrbit);
      r.unknown=true;return r;
    }
  r.unknown=true;return r;
}

static wide_cost_operand li_cost_symbol(const char *name,const void *unused)
{
  (void)unused;li_operand a=li_operand_info(name);wide_cost_operand r={WC_UNKNOWN,0,0};
  if(!strcmp(name,"psw")){r.kind=WC_DIRECT;return r;}
  if(!a.unknown && (a.memory || a.gpr || a.special))r.kind=a.bit?WC_BIT:WC_DIRECT;
  return r;
}

/* 所有新增分支必须指向当前函数体内的真实标签。 */
static bool li_local_label(const char *name,li_line *lines,unsigned begin,unsigned end)
{
  const char *p=name;while(isdigit((unsigned char)*p))++p;
  if(p==name || strcmp(p,"$"))return false;
  size_t n=strlen(name);
  for(unsigned i=begin;i<end;++i)if(lines[i].label) {
    p=lines[i].node->line;while(isspace((unsigned char)*p))++p;
    if(!strncmp(p,name,n) && p[n]==':')return true;
  }
  return false;
}

static bool li_body_effect(li_line *l,li_effects *all,li_line *lines,unsigned begin,unsigned end)
{
  if(l->empty || l->label)return true;
  const char *op=l->op;
  if(!strcmp(op,"nop"))return l->nargs==0;
  /* 所有跳转必须来自 C IR 的本函数标签；没有可跨入保存区的汇编。 */
  if(!strcmp(op,"sjmp") || !strcmp(op,"ljmp") || !strcmp(op,"ejmp") ||
     !strcmp(op,"jc") || !strcmp(op,"jnc") || !strcmp(op,"jz") || !strcmp(op,"jnz") ||
     !strcmp(op,"je") || !strcmp(op,"jne") || !strcmp(op,"jg") || !strcmp(op,"jle") ||
     !strcmp(op,"jsl") || !strcmp(op,"jsge") || !strcmp(op,"jsg") || !strcmp(op,"jsle")) {
    if(l->nargs!=1)return false;
    if(!li_local_label(l->arg[0],lines,begin,end))return false;
    if(strcmp(op,"sjmp") && strcmp(op,"ljmp") && strcmp(op,"ejmp"))all->flags=true;
    if(!strcmp(op,"jz") || !strcmp(op,"jnz"))all->reads|=UINT32_C(1)<<MCS251_BYTE_A;
    return true;
  }
  bool bitbranch=!strcmp(op,"jb") || !strcmp(op,"jnb") || !strcmp(op,"jbc");
  bool comparebranch=!strcmp(op,"cjne"), decrementbranch=!strcmp(op,"djnz");
  if(bitbranch || comparebranch || decrementbranch) {
    unsigned label=comparebranch?2u:1u;
    if(l->nargs!=label+1 || !li_local_label(l->arg[label],lines,begin,end))return false;
    li_operand a=li_operand_info(l->arg[0]),b={0};
    if(comparebranch)b=li_operand_info(l->arg[1]);
    if(a.unknown || b.unknown || *l->arg[0]=='#' || (bitbranch && !a.bit))return false;
    all->reads|=a.gpr|b.gpr;all->specialReads|=a.special|b.special;all->flags|=a.flags|b.flags;
    if(decrementbranch || !strcmp(op,"jbc")){all->writes|=a.gpr;all->specialWrites|=a.special;}
    /* CJNE 改写 C；DJNZ 对各模式的 flags 按整体保守保留。 */
    if(comparebranch || decrementbranch || (a.gpr&(UINT32_C(1)<<MCS251_BYTE_A)))all->flags=true;
    return true;
  }
  bool move=!strcmp(op,"mov") || !strcmp(op,"movz") || !strcmp(op,"movs");
  bool compare=!strcmp(op,"cmp");
  bool bitop=!strcmp(op,"clr") || !strcmp(op,"setb") || !strcmp(op,"cpl");
  bool binary=!strcmp(op,"add") || !strcmp(op,"addc") || !strcmp(op,"sub") || !strcmp(op,"subb") ||
    !strcmp(op,"anl") || !strcmp(op,"orl") || !strcmp(op,"xrl");
  bool unary=!strcmp(op,"inc") || !strcmp(op,"dec") || !strcmp(op,"rl") || !strcmp(op,"rr") ||
    !strcmp(op,"rlc") || !strcmp(op,"rrc") || !strcmp(op,"sll") || !strcmp(op,"srl") || !strcmp(op,"sra");
  if((!move && !compare && !bitop && !binary && !unary) ||
     l->nargs!=(move || compare || binary?2u:1u))return false;
  li_operand a=li_operand_info(l->arg[0]),b={0};if(l->nargs==2)b=li_operand_info(l->arg[1]);
  if(a.unknown || b.unknown || *l->arg[0]=='#')return false;
  all->reads|=b.gpr;all->specialReads|=b.special;all->flags|=a.flags || b.flags;
  if(!move){all->reads|=a.gpr;all->specialReads|=a.special;}
  if(!compare){all->writes|=a.gpr;all->specialWrites|=a.special;}
  if(!move && !(bitop && a.bit && a.memory))all->flags=true;
  if(a.gpr&(UINT32_C(1)<<MCS251_BYTE_A))all->flags=true;
  return true;
}

static void mcs251LeafIsrSaves(lineNode *head,iCode *lic)
{
  const char *enable=getenv("MX251_LEAF_ISR_SAVES");
  if(enable && !strcmp(enable,"0"))return;
  if(!currFunc || !IFFUNC_ISISR(currFunc->type) || IFFUNC_ISNAKED(currFunc->type) ||
     IFFUNC_ISCRITICAL(currFunc->type) || FUNC_REGBANK(currFunc->type) || options.debug ||
     options.useXstack || currFunc->stack || currFunc->xstack || IFFUNC_HASFCALL(currFunc->type))return;
  unsigned functions=0,ends=0;
  for(iCode *ic=lic;ic;ic=ic->next) {
    functions+=ic->op==FUNCTION;ends+=ic->op==ENDFUNCTION;
    if(ic->op==CALL || ic->op==PCALL || ic->op==INLINEASM || ic->op==JUMPTABLE ||
       ic->op==CRITICAL || ic->op==ENDCRITICAL || ic->op==IPUSH || ic->op==IPOP ||
       ic->op==ADDRESS_OF)return;
  }
  if(functions!=1 || ends!=1)return;
  li_line *lines=Safe_alloc(LI_LINES*sizeof *lines);unsigned count=0,begin=0,end=0;
  unsigned pushes[LI_SAVES],pops[LI_SAVES],npush=0,npop=0;int bank=-1;bool ok=true;li_effects effects={0};
  for(lineNode *p=head;p;p=p->next) {
    if(count==LI_LINES || !li_parse(p,&lines[count])){ok=false;break;}
    if(p->line && strstr(p->line,"mx251-isr-body-begin")){if(begin){ok=false;break;}begin=count+1;}
    if(p->line && strstr(p->line,"mx251-isr-body-end")){if(end){ok=false;break;}end=count;}
    ++count;
  }
  if(!begin || !end || begin>end)ok=false;
  for(unsigned i=0;ok && i<count;++i) {
    li_line *l=&lines[i];if(l->empty || l->label)continue;
    if(i>=begin && i<end) {
      if(!li_body_effect(l,&effects,lines,begin,end)){
        dbuf_printf(codeOutBuf,"; mx251-leaf-isr-fallback symbol=%s reason=body-effect instruction=%s\n",currFunc->rname,l->node->line);ok=false;
      }
      continue;
    }
    if(i<begin && !strcmp(l->op,"push") && l->nargs==1 && npush<LI_SAVES)pushes[npush++]=i;
    else if(i>=end && !strcmp(l->op,"pop") && l->nargs==1 && npop<LI_SAVES)pops[npop++]=i;
    else if(i<begin && l->nargs==2 && !strcmp(l->arg[0],"psw") && bank<0 &&
      ((!strcmp(l->op,"anl") && !strcmp(l->arg[1],"#0xe7")) || (!strcmp(l->op,"mov") && !strcmp(l->arg[1],"#0x00"))))bank=(int)i;
    else if(i>=end && !strcmp(l->op,"reti") && !l->nargs){}
    else ok=false;
  }
  if(npush!=npop || !npush)ok=false;
  bool remove[LI_SAVES]={false};unsigned saved=0,bytes=0;
  for(unsigned i=0;ok && i<npush;++i) {
    li_line *a=&lines[pushes[i]],*b=&lines[pops[npop-1-i]];const char *name=a->arg[0];
    if(strcmp(name,b->arg[0])){ok=false;break;}
    bool dead=false;unsigned width=1;
    if(!strcmp(name,"acc"))dead=!(effects.writes&(UINT32_C(1)<<MCS251_BYTE_A));
    else if(!strcmp(name,"b"))dead=!(effects.writes&(UINT32_C(1)<<MCS251_BYTE_B));
    else if(!strcmp(name,"dpl") || !strcmp(name,"dph"))dead=!(effects.specialWrites&MX251_SPECIAL_DPTR);
    else if(!strcmp(name,"dpxl"))dead=!(effects.specialWrites&MX251_SPECIAL_DPX);
    else if(!strcmp(name,"dr16") || !strcmp(name,"dr20") || !strcmp(name,"dr24") || !strcmp(name,"dr28")){
      unsigned r=(unsigned)atoi(name+2);width=4;dead=!(effects.writes&mcs251RegisterBytes(r,4));
    } else if(!strcmp(name,"psw"))dead=bank>=0 && !effects.flags && !((effects.reads|effects.writes)&0xffu);
    if(dead) {
      unsigned ac=wide_instruction_bytes_context(a->node->line,strlen(a->node->line),li_cost_symbol,NULL);
      unsigned bc=wide_instruction_bytes_context(b->node->line,strlen(b->node->line),li_cost_symbol,NULL);
      if(!ac || !bc){ok=false;break;}remove[i]=true;saved+=width;bytes+=ac+bc;
      if(!strcmp(name,"psw"))bytes+=3;
    }
  }
  if(ok && saved) {
    for(unsigned i=0;i<npush;++i)if(remove[i]) {
      bool psw=!strcmp(lines[pushes[i]].arg[0],"psw");
      lines[pushes[i]].node->line=Safe_strdup("; mx251 leaf ISR: unused private save removed");
      lines[pops[npop-1-i]].node->line=Safe_strdup("; mx251 leaf ISR: paired restore removed");
      if(psw)lines[bank].node->line=Safe_strdup("; mx251 leaf ISR: unused bank selection removed");
    }
    dbuf_printf(codeOutBuf,"; mx251-leaf-isr-saves schema=1 symbol=%s removed-bytes=%u stack-saved=%u core-reads=%08lx core-writes=%08lx special-reads=%u special-writes=%u flags=%u psw1=retained frame=private compiler-proof=leaf-direct\n",
      currFunc->rname,bytes,saved,(unsigned long)effects.reads,(unsigned long)effects.writes,effects.specialReads,effects.specialWrites,effects.flags);
  } else dbuf_printf(codeOutBuf,"; mx251-leaf-isr-fallback symbol=%s reason=shape-or-cost-or-no-saving known=%u begin=%u end=%u pushes=%u pops=%u\n",currFunc->rname,ok,begin,end,npush,npop);
  Safe_free(lines);
}
