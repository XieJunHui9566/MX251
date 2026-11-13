/* SPDX-License-Identifier: GPL-2.0-or-later
 * C ISR 的保存槽是编译器私有帧，不是 C 对象或公开的汇编参数接口。
 * 处理无局部帧的普通 bank0 ISR；内联汇编可含已知外设位操作。
 * 复用构建反馈的调用改写集合。
 * 从最终指令和真实符号类型求核心寄存器效果；未知指令/地址完整回退。
 * 不移动或改写任何函数体访存。PSW1 首存末取始终保留。 */
#include "wide_cost.h"
#include "instruction_effects.h"
#include "wide.h"
#include "wide_ordered_contract.h"
extern unsigned mcs251CallDpxWrites(symbol *callee);
extern unsigned mcs251CallBitsWrites(symbol *callee);
enum { LI_LINES=2048, LI_TEXT=512, LI_SAVES=64 };
typedef struct {lineNode *node;char op[24],arg[3][LI_TEXT];unsigned nargs,bytes;bool label,empty;} li_line;
typedef struct {uint32_t gpr;unsigned special;bool flags,memory,bit,unknown,indirect,privateBits;} li_operand;
typedef struct {uint32_t reads,writes;unsigned specialReads,specialWrites,calls;bool flags,bitsWrites;} li_effects;

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

/* This pass leaves peripheral accesses in place. Direct SFR addresses outside
 * the architectural registers and mapping/mode controls do not alias GPRs;
 * they do not need the separate contract used to move ordered inline asm. */
static li_operand li_direct_bits(unsigned address,unsigned mask,bool bit)
{
  li_operand r={0};r.bit=bit;
  if(address==0xe0)r.gpr=UINT32_C(1)<<MCS251_BYTE_A;
  else if(address==0xf0)r.gpr=UINT32_C(1)<<MCS251_BYTE_B;
  else if(!bit && (address==0x82 || address==0x83))r.special=MX251_SPECIAL_DPTR;
  else if(!bit && address==0x84)r.special=MX251_SPECIAL_DPXL;
  else if(address==0xd0 || address==0xd1 || mcs251StackSfrAddress(address))r.unknown=true;
  else if(address==0x80 || address==0x90 || address==0xa0 || address==0xb0)r.memory=true;
  else if(address>=0x80 && wide_asm_private_frame_bits(address,mask))r.memory=true;
  else r.unknown=true;
  return r;
}

static li_operand li_direct(unsigned address,bool bit)
{
  return li_direct_bits(bit?address&0xf8:address,bit?1u<<(address&7):255u,bit);
}

/* 251 byte.bit names the exact SFR byte, including non-classic bit addresses. */
static li_operand li_numeric_bit(const char *text)
{
  li_operand r={0};r.unknown=true;
  if(strlen(text)!=6 || text[0]!='0' || (text[1]!='x' && text[1]!='X') ||
     wide_asm_hex(text[2])<0 || wide_asm_hex(text[3])<0 || text[4]!='.' ||
     text[5]<'0' || text[5]>'7')return r;
  unsigned address=(unsigned)(16*wide_asm_hex(text[2])+wide_asm_hex(text[3]));
  return li_direct_bits(address,1u<<(text[5]-'0'),true);
}

/* The complete inline block must consist of peripheral bit writes. This
 * establishes its effects before final instructions enter the ordinary ISR
 * analysis; labels, calls and other inline instructions retain the old frame. */
static unsigned li_inline_bits(const char *source,bool privateFrame)
{
  unsigned count=0;if(!source)return 0;
  for(const char *p=source;*p;) {
    const char *end=p;while(*end && *end!='\r' && *end!='\n' && (unsigned char)*end!=0x87)++end;
    size_t length=(size_t)(end-p);char text[LI_TEXT];
    if(length>=sizeof text)return 0;
    memcpy(text,p,length);text[length]=0;
    wide_asm_instruction instruction;
    if(wide_asm_decode(text,text+length,&instruction))return 0;
    if(instruction.operation) {
      if(instruction.operation<2)return 0;
      unsigned address=instruction.address,mask=1u<<instruction.bit;
      if(privateFrame ? !wide_asm_private_frame_bits(address,mask) :
                        wide_asm_core_bits(address,mask))return 0;
      ++count;
    }
    p=end;if(*p=='\r')++p;if(*p=='\n' || (unsigned char)*p==0x87)++p;
  }
  return count;
}

/* genInline consumes its text buffer, so classify it before code emission. */
static int mcs251IsrInlineBits(iCode *lic)
{
  int count=0;
  if(!currFunc || !IFFUNC_ISISR(currFunc->type))return -1;
  for(iCode *ic=lic;ic;ic=ic->next)if(ic->op==INLINEASM) {
    unsigned n=li_inline_bits(IC_INLINE(ic),true);if(!n)return -1;count+=(int)n;
  }
  return count;
}

static li_operand li_operand_info(const char *text)
{
  li_operand r={0};mx251_effect_operand reg;
  if(text[0]=='0' && (text[1]=='x' || text[1]=='X') && strchr(text,'.'))return li_numeric_bit(text);
  if(*text=='#' && text[1])return r;
  if(!strcmp(text,"bits") || (text[0]=='b' && text[1]>='0' && text[1]<='7' && !text[2])) {
    r.privateBits=r.memory=true;r.bit=strcmp(text,"bits")!=0;return r;
  }
  if(!strcmp(text,"c") || !strcmp(text,"cy")){r.flags=true;return r;}
  if(!strncmp(text,"acc.",4) && text[4]>='0' && text[4]<='7' && !text[5]){
    r.gpr=UINT32_C(1)<<MCS251_BYTE_A;r.bit=true;return r;
  }
  if(!strcmp(text,"dpxl")){r.special=MX251_SPECIAL_DPXL;return r;}
  if(mx251EffectRegister(text,&reg)){
    r.gpr=reg.gpr;r.special=reg.special;r.unknown=(reg.special&MX251_SPECIAL_SPX)!=0;return r;
  }
  if(strlen(text)==3 && text[0]=='a' && text[1]=='r' && text[2]>='0' && text[2]<='7'){
    r.gpr=UINT32_C(1)<<(text[2]-'0');return r;
  }
  if(*text=='@'){
    if(mx251EffectOperand(text,&reg)){
      r.gpr=reg.gpr;r.special=reg.special;r.memory=r.indirect=true;return r;
    }
    r.unknown=true;return r;
  }
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

/* A C object address remains an object address through pointer copies and
 * indexing. Integer-derived pointers and absolute objects need the actual
 * machine address instead. Out-of-object C stores are not defined accesses. */
static bool li_object_pointer(operand *p,unsigned depth)
{
  if(!p || !IS_SYMOP(p) || depth>16)return false;
  symbol *s=OP_SYMBOL(p);iCode *def=NULL;
  sym_link *type=operandType(p);
  if(!IS_PTR(type) || getSize(type)!=3 ||
     (DCL_TYPE(type)!=FPOINTER && DCL_TYPE(type)!=GPOINTER))return false;
  if(s->remat)def=s->rematiCode;
  else if(IS_ITEMP(p) && bitVectnBitsOn(OP_DEFS(p))==1)
    def=hTabItemWithKey(iCodehTab,bitVectFirstBit(OP_DEFS(p)));
  if(!def)return false;
  if(def->op==ADDRESS_OF && IS_TRUE_SYMOP(IC_LEFT(def))){
    symbol *object=OP_SYMBOL(IC_LEFT(def));sym_link *spec=getSpec(object->type);
    return !SPEC_ABSA(spec) && !object->onStack &&
      SPEC_SCLS(spec)!=S_SFR && SPEC_SCLS(spec)!=S_SBIT;
  }
  if(def->op==CAST || (def->op=='=' && !POINTER_SET(def)))
    return li_object_pointer(IC_RIGHT(def),depth+1);
  if(def->op=='+' || def->op=='-')return li_object_pointer(IC_LEFT(def),depth+1);
  return false;
}

static bool li_indirect_store(li_line *l,li_line *lines,unsigned begin,unsigned at)
{
  mx251_effect_operand address;
  if(!mx251EffectOperand(l->arg[0],&address))return false;
  if(address.stack)return true;
  if(address.special!=MX251_SPECIAL_DPX && (!address.gpr || address.width!=4))return false;
  iCode *ic=l->node->ic;
  if(ic && POINTER_SET(ic) && li_object_pointer(IC_RESULT(ic),0))return true;
  if(strcmp(l->arg[0],"@dpx"))return false;
  /* Match immediate DPX setup along a single-entry instruction sequence.
   * A conditional exit preserves DPX on its fallthrough path; an incoming
   * label, call or address write ends the search. */
  unsigned low=0,high=0;bool haveLow=false,haveHigh=false;
  for(unsigned i=at;i>begin;){
    li_line *p=&lines[--i];if(p->empty)continue;if(p->label)break;
    if(p->nargs==2 && !strcmp(p->op,"mov") && p->arg[1][0]=='#'){
      char *end;unsigned long value=strtoul(p->arg[1]+1,&end,0);
      if(end!=p->arg[1]+1 && !*end){
        if(!strcmp(p->arg[0],"dptr") && !haveLow && value<=65535){low=(unsigned)value;haveLow=true;continue;}
        if(!strcmp(p->arg[0],"dpxl") && !haveHigh && value<=255){high=(unsigned)value;haveHigh=true;continue;}
      }
    }
    if(haveLow && haveHigh)break;
    if(!strcmp(p->op,"jb") || !strcmp(p->op,"jnb") ||
       !strcmp(p->op,"jc") || !strcmp(p->op,"jnc") ||
       !strcmp(p->op,"jz") || !strcmp(p->op,"jnz"))continue;
    /* Only simple operations on GPRs/direct objects can be crossed. */
    li_operand dest=li_operand_info(p->arg[0]);
    /* The body is checked in execution-text order. Earlier indirect stores
     * have already been proved to target C objects or non-core addresses,
     * so they cannot change DPXL retained by literal-address reuse. */
    if(!strcmp(p->op,"mov") && dest.indirect && !dest.unknown)continue;
    /* A newer low-half assignment hides earlier DPTR writes without
     * hiding the high byte that is still being searched for. */
    if(haveLow && dest.special==MX251_SPECIAL_DPTR &&
       (!strcmp(p->op,"mov") || !strcmp(p->op,"inc") || !strcmp(p->op,"dec")))continue;
    if(dest.unknown || dest.indirect || dest.special ||
       (strcmp(p->op,"mov") && strcmp(p->op,"clr") &&
        strcmp(p->op,"anl") && strcmp(p->op,"orl") && strcmp(p->op,"xrl") &&
        strcmp(p->op,"add") && strcmp(p->op,"addc") && strcmp(p->op,"sub") &&
        strcmp(p->op,"subb") && strcmp(p->op,"inc") && strcmp(p->op,"dec")))break;
  }
  unsigned long value=((unsigned long)high<<16)|low;
  mx251_effect_operand source;unsigned width=1;
  if(mx251EffectRegister(l->arg[1],&source))width=source.width;
  return haveLow && haveHigh && value>=256 && value+width<=0x1000000ul;
}

static bool li_body_effect(li_line *l,li_effects *all,li_line *lines,unsigned begin,unsigned end)
{
  if(l->empty || l->label)return true;
  const char *op=l->op;
  if(!strcmp(op,"nop"))return l->nargs==0;
  if(!strcmp(op,"mul") && l->nargs==1 && !strcmp(l->arg[0],"ab")) {
    all->reads|=MCS251_AB_BYTES;all->writes|=MCS251_AB_BYTES;all->flags=true;return true;
  }
  if(!strcmp(op,"jmp") && l->nargs==1 && !strcmp(l->arg[0],"@a+dptr")) {
    /* Only the emitter's adjacent, fully local jump table can dispatch into
     * the private body. Check metadata, entry count and every target. */
    unsigned at=(unsigned)(l-lines)+1,entries=0;char owner[LI_TEXT],label[LI_TEXT];bool metadata=false;
    while(at<end && lines[at].empty) {
      const char *text=lines[at].node->line,*mark=text?strstr(text,"mx251-jump-table schema=1 "):NULL;
      if(mark && sscanf(mark,"mx251-jump-table schema=1 owner=%511s label=%511s entries=%u stride=4 dispatch=a-dptr",owner,label,&entries)==3 &&
         !strcmp(owner,currFunc->rname) && entries>0 && entries<=256)metadata=true;
      ++at;
    }
    if(!metadata || at>=end || !lines[at].label || !li_local_label(label,lines,at,at+1))return false;
    for(unsigned k=0;k<entries;++k) {
      do {++at;}while(at<end && lines[at].empty);
      if(at>=end || strcmp(lines[at].op,"ejmp") || lines[at].nargs!=1 ||
         !li_local_label(lines[at].arg[0],lines,begin,end))return false;
    }
    all->reads|=UINT32_C(1)<<MCS251_BYTE_A;all->specialReads|=MX251_SPECIAL_DPX;return true;
  }
  if(!strcmp(op,"ecall") || !strcmp(op,"lcall") || !strcmp(op,"acall")){
    iCode *ic=l->node->ic;unsigned long writes;
    if(l->nargs!=1 || !ic || ic->op!=CALL || !IS_SYMOP(IC_LEFT(ic)))return false;
    symbol *callee=OP_SYMBOL(IC_LEFT(ic));
    if(strcmp(l->arg[0],callee->rname) || !mcs251CallClobberWrites(callee,&writes))return false;
    all->writes|=(uint32_t)writes;all->reads=UINT32_MAX;
    all->specialWrites|=mcs251CallDpxWrites(callee);
    all->bitsWrites|=mcs251CallBitsWrites(callee)!=0;
    all->flags=true;++all->calls;return true;
  }
  /* Reuse the emitter's effects for word arguments and relative stack
   * cleanup. These change the argument stack, not the private saved values;
   * arbitrary SPX assignments and inline stack access are still excluded. */
  if(!strcmp(op,"pushw") ||
     ((!strcmp(op,"inc") || !strcmp(op,"dec")) && !strcmp(l->arg[0],"spx"))) {
    mx251_effects e=mx251InstructionEffects(l->node->line,strlen(l->node->line),NULL,0);
    if(e.unknown || e.writes || e.specialWrites!=MX251_SPECIAL_SPX)return false;
    all->reads|=e.reads;all->specialReads|=e.specialReads;all->specialWrites|=e.specialWrites;
    all->flags|=(e.flagsReads|e.flagsWrites)!=0;return true;
  }
  if(!strcmp(op,"push") || !strcmp(op,"pop")){
    if(l->nargs!=1)return false;li_operand a=li_operand_info(l->arg[0]);
    if(a.unknown || a.indirect || a.flags || l->arg[0][0]=='#')return false;
    if(!strcmp(op,"push")){all->reads|=a.gpr;all->specialReads|=a.special;}
    else {all->writes|=a.gpr;all->specialWrites|=a.special;all->flags|=(a.gpr&MCS251_AB_BYTES)!=0;all->bitsWrites|=a.privateBits;}
    return true;
  }
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
    if(decrementbranch || !strcmp(op,"jbc")){all->writes|=a.gpr;all->specialWrites|=a.special;all->bitsWrites|=a.privateBits;}
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
  if(a.indirect && (!move || !li_indirect_store(l,lines,begin,(unsigned)(l-lines))))return false;
  all->reads|=b.gpr;all->specialReads|=b.special;all->flags|=a.flags || b.flags;
  if(!move || a.indirect){all->reads|=a.gpr;all->specialReads|=a.special;}
  if(!compare && !a.indirect){all->writes|=a.gpr;all->specialWrites|=a.special;all->bitsWrites|=a.privateBits;}
  if(!move && !(bitop && a.bit && a.memory))all->flags=true;
  if(a.gpr&(UINT32_C(1)<<MCS251_BYTE_A))all->flags=true;
  return true;
}

/* With no local frame, save slots have no address-dependent consumers. Build
 * both ends together, switch bank before register saves, and restore PSW last.
 * Group only already-preserved bytes; DPX's padding byte is private scratch. */
static void li_group_saves(li_line *lines,unsigned *pushes,unsigned *pops,
                           unsigned count,const bool *removed,int bank,bool dropBank)
{
  uint32_t gpr=0;unsigned before=0,oldStack=0,dpx=0;
  char names[LI_SAVES][32];unsigned nn=0;
  for(unsigned i=0;i<count;++i)if(!removed[i]) {
    li_line *a=&lines[pushes[i]],*b=&lines[pops[count-1-i]];
    unsigned ac=wide_instruction_bytes_context(a->node->line,strlen(a->node->line),li_cost_symbol,NULL);
    unsigned bc=wide_instruction_bytes_context(b->node->line,strlen(b->node->line),li_cost_symbol,NULL);
    /* Private bit-bank and PSW1 operands are direct byte saves. */
    if(!ac || !bc)return;
    before+=ac+bc;
    li_operand operand=li_operand_info(a->arg[0]);mx251_effect_operand reg;
    unsigned width=mx251EffectRegister(a->arg[0],&reg)?reg.width:1;
    if(!strcmp(a->arg[0],"dpx"))width=4;
    oldStack+=width;
    if(operand.gpr){if(gpr&operand.gpr)return;gpr|=operand.gpr;}
    else if(!strcmp(a->arg[0],"dpl"))dpx|=1;
    else if(!strcmp(a->arg[0],"dph"))dpx|=2;
    else if(!strcmp(a->arg[0],"dpxl"))dpx|=4;
    else {if(strlen(a->arg[0])>=sizeof names[0])return;strcpy(names[nn++],a->arg[0]);}
  }
  if((gpr&255) && (bank<0 || dropBank))return;
  unsigned prefix=nn,newStack=oldStack;
  if(dpx==7){strcpy(names[nn++],"dpx");++newStack;}
  else if(dpx==3)strcpy(names[nn++],"wr58");
  else {
    if(dpx&1)strcpy(names[nn++],"dpl");
    if(dpx&2)strcpy(names[nn++],"dph");
    if(dpx&4)strcpy(names[nn++],"dpxl");
  }
  for(unsigned reg=0;reg<32;) {
    if(!(gpr&(UINT32_C(1)<<reg))){++reg;continue;}
    unsigned width=1;
    if(!(reg&3) && (gpr&(UINT32_C(15)<<reg))==(UINT32_C(15)<<reg))width=4;
    else if(!(reg&1) && (gpr&(UINT32_C(3)<<reg))==(UINT32_C(3)<<reg))width=2;
    if(width==1 && reg>=16)return;
    SNPRINTF(names[nn++],sizeof names[0],"%sr%u",width==4?"d":width==2?"w":"",reg);
    reg+=width;
  }
  unsigned after=4*nn;if(after>=before || !nn)return;
  struct dbuf_s entry,exit;dbuf_init(&entry,256);dbuf_init(&exit,256);
  for(unsigned i=0;i<nn;++i) {
    if(i==prefix && bank>=0 && !dropBank)dbuf_printf(&entry,"%s\n",lines[bank].node->line);
    dbuf_printf(&entry,"\tpush\t%s\n",names[i]);
    dbuf_printf(&exit,"\tpop\t%s\n",names[nn-1-i]);
  }
  /* A body with no GPR/DPX saves may still retain its bank selection. */
  if(prefix==nn && bank>=0 && !dropBank)dbuf_printf(&entry,"%s\n",lines[bank].node->line);
  for(unsigned i=0;i<count;++i) {
    lines[pushes[i]].node->line=Safe_strdup(i?"; mx251 grouped save":dbuf_c_str(&entry));
    lines[pops[i]].node->line=Safe_strdup(i?"; mx251 grouped restore":dbuf_c_str(&exit));
  }
  if(bank>=0)lines[bank].node->line=Safe_strdup("; mx251 bank selection belongs to grouped entry");
  dbuf_printf(codeOutBuf,"; mx251-isr-save-groups schema=1 symbol=%s saved-bytes=%u stack-before=%u stack-after=%u frame=private\n",
    currFunc->rname,before-after,oldStack,newStack);
  dbuf_destroy(&entry);dbuf_destroy(&exit);
}

static void mcs251LeafIsrSaves(lineNode *head,iCode *lic,int inlineBits)
{
  const char *enable=getenv("MX251_LEAF_ISR_SAVES");
  if(enable && !strcmp(enable,"0"))return;
  if(!currFunc || !IFFUNC_ISISR(currFunc->type) || IFFUNC_ISNAKED(currFunc->type) ||
     IFFUNC_ISCRITICAL(currFunc->type) || FUNC_REGBANK(currFunc->type) || options.debug ||
     options.useXstack || currFunc->stack || currFunc->xstack || inlineBits<0)return;
  unsigned functions=0,ends=0;
  for(iCode *ic=lic;ic;ic=ic->next) {
    functions+=ic->op==FUNCTION;ends+=ic->op==ENDFUNCTION;
    if(ic->op==PCALL ||
       ic->op==CRITICAL || ic->op==ENDCRITICAL)return;
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
  bool remove[LI_SAVES]={false};unsigned saved=0,bytes=0,omittedDpx=0;uint32_t omitted=0;
  bool omittedBits=false;
  bool dropBank=bank>=0 && !((effects.reads|effects.writes)&0xffu);
  for(unsigned i=0;ok && i<npush;++i) {
    li_line *a=&lines[pushes[i]],*b=&lines[pops[npop-1-i]];const char *name=a->arg[0];
    if(strcmp(name,b->arg[0])){ok=false;break;}
    bool dead=false;unsigned width=1;
    if(!strcmp(name,"acc"))dead=!(effects.writes&(UINT32_C(1)<<MCS251_BYTE_A));
    else if(!strcmp(name,"b"))dead=!(effects.writes&(UINT32_C(1)<<MCS251_BYTE_B));
    else if(!strcmp(name,"dpx")){width=4;dead=!(effects.specialWrites&MX251_SPECIAL_DPX);}
    else if(!strcmp(name,"dpl") || !strcmp(name,"dph"))dead=!(effects.specialWrites&MX251_SPECIAL_DPTR);
    else if(!strcmp(name,"dpxl"))dead=!(effects.specialWrites&MX251_SPECIAL_DPXL);
    else if(!strcmp(name,"bits"))dead=!effects.bitsWrites;
    else if(!strcmp(name,"psw"))dead=bank>=0 && !effects.flags && !((effects.reads|effects.writes)&0xffu);
    else {
      mx251_effect_operand reg;
      if(mx251EffectRegister(name,&reg) && reg.gpr){width=reg.width;dead=!(effects.writes&reg.gpr);}
      else if(strlen(name)==3 && name[0]=='a' && name[1]=='r' && name[2]>='0' && name[2]<='7')
        dead=!(effects.writes&(UINT32_C(1)<<(name[2]-'0')));
    }
    if(dead) {
      unsigned ac=wide_instruction_bytes_context(a->node->line,strlen(a->node->line),li_cost_symbol,NULL);
      unsigned bc=wide_instruction_bytes_context(b->node->line,strlen(b->node->line),li_cost_symbol,NULL);
      if(!ac || !bc){ok=false;break;}remove[i]=true;saved+=width;bytes+=ac+bc;
      omitted|=li_operand_info(name).gpr;
      omittedDpx|=li_operand_info(name).special&MX251_SPECIAL_DPX;
      omittedBits|=li_operand_info(name).privateBits;
    }
  }
  if(dropBank)bytes+=3;
  if(ok && (saved || dropBank)) {
    for(unsigned i=begin;effects.calls && i<end;++i)
      if(!strcmp(lines[i].op,"ecall") || !strcmp(lines[i].op,"lcall") || !strcmp(lines[i].op,"acall")) {
        dbuf_printf(codeOutBuf,"; mx251-isr-call callee=%s omitted=%08lx\n",lines[i].arg[0],(unsigned long)omitted);
        if(omittedDpx)dbuf_printf(codeOutBuf,"; mx251-isr-dpx-call callee=%s omitted=%u\n",lines[i].arg[0],omittedDpx);
        if(omittedBits)dbuf_printf(codeOutBuf,"; mx251-isr-bits-call callee=%s omitted=1\n",lines[i].arg[0]);
      }
    for(unsigned i=0;i<npush;++i)if(remove[i]) {
      lines[pushes[i]].node->line=Safe_strdup("; mx251 leaf ISR: unused private save removed");
      lines[pops[npop-1-i]].node->line=Safe_strdup("; mx251 leaf ISR: paired restore removed");
    }
    if(dropBank)lines[bank].node->line=Safe_strdup("; mx251 leaf ISR: unused bank selection removed");
    dbuf_printf(codeOutBuf,"; mx251-leaf-isr-saves schema=2 symbol=%s removed-bytes=%u stack-saved=%u core-reads=%08lx core-writes=%08lx special-reads=%u special-writes=%u flags=%u calls=%u psw1=retained frame=private inline-bits=%u compiler-proof=body-effects\n",
      currFunc->rname,bytes,saved,(unsigned long)effects.reads,(unsigned long)effects.writes,effects.specialReads,effects.specialWrites,effects.flags,effects.calls,(unsigned)inlineBits);
  } else dbuf_printf(codeOutBuf,"; mx251-leaf-isr-fallback symbol=%s reason=shape-or-cost-or-no-saving known=%u begin=%u end=%u pushes=%u pops=%u\n",currFunc->rname,ok,begin,end,npush,npop);
  if(ok)li_group_saves(lines,pushes,pops,npush,remove,bank,dropBank);
  Safe_free(lines);
}
