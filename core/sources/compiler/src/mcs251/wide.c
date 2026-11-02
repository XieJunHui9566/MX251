#include "register_facts.h"
#include "instruction_effects.h"
#include "extended_constants.h"
/* SPDX-License-Identifier: GPL-2.0-or-later
 * 251 原生宽度后端：整数运算、浮点值传递及寄存器 ABI 的直接调用。
 * 宽值、寄存器重叠、控制流与溢出槽在发射前统一决定。 */
#include "common.h"
#include "wide.h"
#include "SDCCdflow.h"
#include "internal_return.h"
#include "constant_arg.h"
#include "dbuf_string.h"
#include <stdint.h>
#include <stdarg.h>

extern struct dbuf_s *codeOutBuf;
extern bool mcs251InlineClosedLeafText(struct dbuf_s *output,symbol *owner,ebbIndex *ebbi);
#include "wide_leaf_effects.c.inc"
#ifdef MX251_WIDE_AUDIT
#include "wide_audit.h"
#else
#define WA_FAIL(stage) 0
#endif
/* 存储上界与本轮导入限额分开：默认仍按 128/512 回退，独立比较容量收益。
 * 候选仍整体复制，数组内嵌避免分配试算之间共享可变位集或节点。
 * 对象位掩码、参数 ABI、跳转边池、帧和搜索预算不随这两个上界扩大。 */
enum { WV_MAX = 512, WN_MAX = 2048, WO_MAX = 32, WA_MAX = 8, WJ_MAX = 2048, WASM_MAX = 16, WB_WORDS = (WV_MAX + 63) / 64, WM_LOAD=0x10000, WM_STORE, WM_ADDRESS, WM_UMULW, WM_NOP, WM_PARAM_LOAD, WM_PARAM_STORE, WM_PARAM_ADDRESS, WM_OBJECT_LOAD, WM_OBJECT_STORE, WM_SYMBOL_LOAD, WM_SYMBOL_STORE, WM_BIT_LOAD, WM_BIT_STORE, WM_BIT_CAST, WM_RAW32_COPY, WM_ORDERED_ASM, WM_VOLATILE_BIT_READ, WM_UMUL32N, WM_ROTATE_LEFT, WM_UDIVW, WM_UMODW };
typedef struct { unsigned char operation,address,bit; } wide_asm_instruction;
typedef struct { uint64_t w[WB_WORDS]; } wide_bits;
typedef struct {
  symbol *sym;
  int width, reg, slot, pin, preference, weight, reads;
  bool sign, pointer, codePointer, floating, symbolSnapshot;
  wide_bits edges;
} wide_value;
typedef struct { int value, width; uint32_t literal; bool sign, floating, codePointer; } wide_operand;
/* 可取址对象有独占的完整存储期，不与虚拟寄存器的溢出槽合并。
 * 不复用旧后端尚未重新布局的 symbol.stack，也不缓存对象内容。 */
typedef struct { symbol *sym; int slot, size; bool privateUnion; } wide_object;
typedef struct {
  unsigned op;
  iCode *origin;
  wide_operand left, right, addressIndex;
  symbol *callee;
  symbol *addressSymbol;
  int addressOffset, addressObject;
  unsigned addressOp;
  bool byteAccess, codeAccess, pointerAccess;
  bool argumentReadReverse; /* ABI2 SEND reads least significant byte first. */
  bool rotateCandidate; /* proposal only: original MIR/allocation remain frozen */
  bool volatileDataWideAccess; /* explicit object-access contract; byteAccess remains true */
  bool nativeIndexed;
  unsigned indexedExtent,indexedOrdinal;
  bool privateUnionWordAccess; /* transaction width only; byteAccess still guards aliases */
  unsigned args, stackArgs, stackBytes;
  wide_operand stackOperands[WA_MAX-1];
  unsigned char stackWidths[WA_MAX-1];
  bool ordinaryCall;
  unsigned rangeMultiplyMask; /* 本次 CALL 前的完整 u32 实参范围，非类型猜测 */
  uint32_t callSaveOmit;
  signed char multiplyDigits[32];
  int multiplyTop;
  int result, label, target, succ[2], width;
  int jumpStart, jumpCount;
  bool sign, floating, trueBranch;
  unsigned orderedAsmCount;
  wide_asm_instruction orderedAsm[WASM_MAX];
  unsigned orderedReadCount;
  symbol *orderedReadSymbols[2];
  wide_bits use, def, in, out;
} wide_node;
/* 文本位置用于替换，字节上界用于证明距离；二者不能混用。 */
typedef struct { int label;size_t text;unsigned upper,barrier; } wide_label_site;
typedef struct {
  int target;const char *condition;size_t begin,end;
  unsigned upperBegin,upperEnd,barrier,weight;
} wide_branch_site;
typedef struct {
  wide_label_site labels[WN_MAX*4];wide_branch_site branches[WN_MAX];
  unsigned labelCount,branchCount,emitUpper,emitBarrier;
  bool relaxDisabled;
  wide_value values[WV_MAX]; wide_node nodes[WN_MAX];
  wide_object objects[WO_MAX];
  /* 独立的有界边池保留表项次序和重复目标；整体复制分配候选时没有悬空指针。 */
  int jumpLabels[WJ_MAX], jumpSuccessors[WJ_MAX], nj;
  int nv, nn, no, frame, serial, received, stackDepth, foldedCasts, reusedParameterLoads;
  unsigned emitWeight, emitCost, emitInstructions;
  bool scratchAddressValid;
  uint32_t scratchAddress;
  int scratchNode,scratchNextNode;
  bool body, sampledQualified, formalAddress, ordinaryEntry, allocationInvalid;
  bool nativeReceiveFree;
  bool nativeIndexedTrial;
  unsigned domainCount,domainValues[4];
  bool applyCallEffects,callEffectsInvalid;
  int valueLimit,nodeLimit,rejectionLine;
  const char *rejectionReason;
  iCode *rejectionOrigin;
  bool orderedAsmEnabled;
  unsigned char orderedAsmBits[256];
  struct isolated_valinfo_analysis *rangeAnalysis;
  ebbIndex *sourceBlocks; /* 仅借用当前函数 CFG 做纯地址支配证明，不修改 IR。 */
  struct dbuf_s *diagnostics, *candidateOutput;
  struct dbuf_s output;
} wide_function;

#include "wide_capacity.c.inc"
#include "wide_ordered_asm.c.inc"

/* wassert 记录编译器错误但不退出当前表达式；错误后仍须阻止越界访问。 */
static bool wb_has (wide_bits b, int n) { if(n>=WV_MAX) {wassert(0);return false;}return n >= 0 && (b.w[n / 64] & (UINT64_C(1) << (n % 64))); }
static void wb_set (wide_bits *b, int n) { if(n>=WV_MAX) {wassert(0);return;}if (n >= 0) b->w[n / 64] |= UINT64_C(1) << (n % 64); }
static bool wb_empty (wide_bits b) { for (int i=0;i<WB_WORDS;++i) if(b.w[i])return false;return true; }
static bool wb_equal (wide_bits a, wide_bits b) { return !memcmp (&a, &b, sizeof a); }
static wide_bits wb_or (wide_bits a, wide_bits b) { for (int i=0;i<WB_WORDS;++i) a.w[i]|=b.w[i]; return a; }
static wide_bits wb_minus (wide_bits a, wide_bits b) { for (int i=0;i<WB_WORDS;++i) a.w[i]&=~b.w[i]; return a; }

/* 与原后端 __gptrget/put 的平坦寻址一致；高补位仍统一清零。
   近指针/pdata 转换需原后端处理，不能直接当作平坦地址。 */
static bool wide_code_pointer(sym_link *t) {return IS_PTR(t) && DCL_TYPE(t)==CPOINTER && getSize(t)==3 && !IS_FUNC(t->next);}
static bool wide_pointer(sym_link *t) {return IS_PTR(t) && (DCL_TYPE(t)==FPOINTER || DCL_TYPE(t)==GPOINTER || DCL_TYPE(t)==CPOINTER) && getSize(t)==3 && !IS_FUNC(t->next);}
static int wide_width(sym_link *t) {return wide_pointer(t)?4:getSize(t);}
static bool
wide_value_type (sym_link *t, bool sampled)
{
  int size = getSize (t);
  if(isAtomic(t) || (isVolatile(t) && !sampled))return false;
  if(wide_pointer(t))return true;
  if(IS_FLOAT(t))return size==4;
  return IS_SPEC (t) && IS_INTEGRAL (t) &&
    (!IS_BITINT (t) || SPEC_BITINTWIDTH (t) == 8*size) &&
    (size == 1 || size == 2 || size == 4);
}
/* CODE 只扩展函数内部的取址/读取，公开 ABI 仍只接纳原有平坦数据指针。 */
static bool wide_type(sym_link *t) {return wide_value_type(t,false) && !wide_code_pointer(t) && !IS_BIT(t) && !IS_BOOL(t);}

/* 普通 ABI2 已定义三字节 CODE 指针运输；MIR 补零为四字节不改变
 * DPL/DPH/B 或物理栈槽。ABI4 仍保持原有类型合同与原后端诊断。
 * 读取空间由 codePointer/codeAccess 传递，不能退化为 XDATA。 */
static bool
wide_abi_type(sym_link *t,sym_link *functionType)
{
  return wide_type(t) || (FUNC_SDCCCALL(functionType)==2 &&
    wide_code_pointer(t) && wide_value_type(t,false));
}


/* ABI4 前两个参数在 DR4/DR12；其余标量按声明宽度、逆序压栈。
 * 平坦指针在 MIR 中为四字节，栈上仍严格占三字节，不改变公开 ABI。 */
static unsigned wide_register_count(sym_link *type) {return FUNC_SDCCCALL(type)==2?1:2;}

/* 编译器拥有的 16/32 位整数支持调用可复用普通 ABI2。
 * 身份来自前端 operation/type 表，绝不按函数拼写识别。仅承认完整
 * 同宽、同符号、二参数的原型；不改运算实现、不省略活寄存器保存。
 * 8 位混合提升、64 位、移位、浮点及未知 cdef 仍保留旧后端。 */
static bool
wide_integer_runtime_contract(symbol *callee,unsigned *operation,unsigned *width,unsigned *signedness)
{
  if(!callee || !callee->cdef || !IS_FUNC(callee->type) || FUNC_SDCCCALL(callee->type)!=2)return false;
  sym_link *t=callee->type,*r=t->next;value *a=FUNC_ARGS(t);
  if(!a || !a->next || a->next->next || !IS_SPEC(r) || !IS_INTEGRAL(r) || IS_BITINT(r))return false;
  for(unsigned op=0;op<3;++op)for(unsigned index=1;index<=2;++index)for(unsigned sign=0;sign<2;++sign) {
    if(callee!=muldiv[op][index][sign])continue;
    unsigned bytes=1u<<index;bool expectedUnsigned=op!=0 && sign==0;
    if(getSize(r)!=bytes || !!IS_UNSIGNED(r)!=expectedUnsigned || isVolatile(r) || isAtomic(r))return false;
    for(value *v=a;v;v=v->next) {
      sym_link *p=v->type;
      if(!IS_SPEC(p) || !IS_INTEGRAL(p) || IS_BITINT(p) || getSize(p)!=bytes ||
         !!IS_UNSIGNED(p)!=expectedUnsigned || isVolatile(p) || isAtomic(p))return false;
    }
    *operation=op;*width=bytes;*signedness=!expectedUnsigned;return true;
  }
  return false;
}

static bool
wide_stack_signature(sym_link *type)
{
  unsigned index=0;
  if(IFFUNC_ISSMALLC(type) || IFFUNC_ISDYNAMICC(type))return false;
  for(value *arg=FUNC_ARGS(type);arg;arg=arg->next,++index)
    if(index==WA_MAX || (index>=wide_register_count(type) && !wide_abi_type(arg->type,type)))return false;
  return true;
}

static bool
wide_operand_from (wide_function *f, operand *op, wide_operand *v)
{
  sym_link *t;
  v->value = -1;
  /* 前端联合体读操作会把 volatile 限定带到结果临时值。
   * 非取址 ITEMP 表示已经取样的值，其寄存器使用不再次访问内存；
   * 经类型提升带上该限定的字面量同样不表示一次内存访问。
   * 实际读写仍只能经 WM_LOAD/WM_STORE 导入，保留限定和字节次序；
   * 真实变量、取址临时值与 atomic 不能借此放宽，原 iCode 类型不修改。 */
  bool sampled=op && (IS_OP_LITERAL(op) ||
    (IS_ITEMP(op) && !IS_OP_GLOBAL(op) && !OP_SYMBOL(op)->addrtaken));
  if (!op || !wide_value_type(t=operandType(op),sampled) ||
      (!sampled && isOperandVolatile(op,FALSE))) return false;
  if(sampled && isVolatile(t))f->sampledQualified=true;
  v->width = wide_width (t); v->floating=IS_FLOAT(t);v->codePointer=wide_code_pointer(t);
  v->sign = !wide_pointer(t) && !v->floating && !SPEC_USIGN (getSpec (t));
  if (IS_OP_LITERAL (op)) {
    /* 使用编译器已有的 binary32 常量序列化，绝不把浮点数值转成整数。 */
    if(v->floating) {
      v->literal=0;
      for(int i=0;i<4;++i)v->literal|=(uint32_t)byteOfVal(OP_VALUE(op),i)<<(8*i);
    } else v->literal = (uint32_t)(int64_t)operandLitValue (op);
    if (v->width < 4) v->literal &= (UINT32_C(1) << (8*v->width)) - 1;
    if(wide_pointer(t))v->literal&=0xffffff;
    return true;
  }
  /* 初期只接收前端的非逃逸临时值；不把真实内存变量误当虚拟寄存器。 */
  if (!IS_ITEMP (op) || IS_OP_GLOBAL (op) || OP_SYMBOL (op)->addrtaken) return false;
  for (int i=0;i<f->nv;++i) if (!f->values[i].symbolSnapshot && f->values[i].sym == OP_SYMBOL (op)) {
    if(f->values[i].floating!=v->floating || f->values[i].width!=v->width || f->values[i].codePointer!=v->codePointer)return false;
    v->value=i;return true;
  }
  if (!wide_capacity_room(f,1,0,__LINE__)) return false;
  v->value=f->nv++;
  f->values[v->value]=(wide_value){.sym=OP_SYMBOL(op),.width=v->width,.sign=v->sign,.floating=v->floating,.pointer=wide_pointer(t),.codePointer=v->codePointer,.reg=-1,.slot=-1,.pin=-1,.preference=-1};
  return true;
}

static bool
wide_bit_symbol(operand *op)
{
  if(!op || !IS_TRUE_SYMOP(op) || !IS_BIT(operandType(op)))return false;
  symbol *s=OP_SYMBOL(op);
  return !s->onStack && !s->_isparm && !isAtomic(operandType(op)) &&
    IN_BITSPACE(SPEC_OCLS(s->etype));
}

static bool
wide_register_parameter(symbol *s)
{
  /* 普通 ABI2 的寄存器首参也可能直接保留为真实栈符号，例如显式
   * __reentrant 而未启用全局 stack-auto，或指向 volatile 的指针。
   * 为它复用独占物理 home 与逐次快照，不能把真实变量注册成临时值。
   * 参数本体的 volatile/atomic 限定仍由 wide_type 拒绝；所指对象的
   * 限定继续由内存节点处理。显式试验开关也允许 ABI4 的两个真实
   * 寄存器形参；按 FUNC_ARGS 的符号身份确认位置，不猜测名称或槽。 */
  if(!s->_isparm || !s->onStack ||
     (!s->addrtaken && FUNC_SDCCCALL(currFunc->type)!=2 &&
      !(FUNC_SDCCCALL(currFunc->type)==4 && mcs251WideOrdinaryEnabled)) ||
     !wide_abi_type(s->type,currFunc->type))return false;
  int index=0;
  for(value *arg=FUNC_ARGS(currFunc->type);arg && index<wide_register_count(currFunc->type);arg=arg->next,++index)
    if(arg->sym==s)return true;
  return false;
}

static bool
wide_parameter_slot(operand *op,sym_link **type,int *offset)
{
  if(!op || !IS_TRUE_SYMOP(op))return false;
  symbol *s=OP_SYMBOL(op);sym_link *t=operandType(op);
  if(!s->_isparm || !s->onStack ||
     isOperandVolatile(op,TRUE) || !wide_abi_type(t,currFunc->type))return false;
  int at=-2,index=0;
  /* ECALL 返回地址占三字节；偏移相对于入口 SPX，独立于旧后端帧重排。
   * 取址对象复用原物理栈槽；volatile/atomic 形参仍回退。 */
  for(value *arg=FUNC_ARGS(currFunc->type);arg;arg=arg->next,++index) {
    if(index<wide_register_count(currFunc->type))continue;
    at-=getSize(arg->type);
    if(arg->sym!=s)continue;
    if(!wide_abi_type(arg->type,currFunc->type) || getSize(t)!=getSize(arg->type) ||
       wide_code_pointer(t)!=wide_code_pointer(arg->type) ||
       IS_FLOAT(t)!=IS_FLOAT(arg->type) || wide_pointer(t)!=wide_pointer(arg->type))return false;
    *type=arg->type;*offset=at;return true;
  }
  return false;
}

static bool wide_stack_object(wide_function *f,symbol *s,int *object);

/* 显式实现契约仅改变普通分配 DATA 的完整标量访问粒度；绝不取消
 * volatile 屏障，不缓存、合并或重排访问。非原子，不证明 ISR 可撕裂
 * 采样与旧逐字节实现相同。外部/绝对地址/取址/部分对象全部回退。
 * 最终链接必须另外证明对象留在正常 DATA RAM 内且不覆盖寄存器组。
 */
static bool
wide_volatile_data_object(symbol *s,sym_link *t)
{
  if(!mcs251VolatileDataWideEnabled || !s || !t || !s->type || !s->etype ||
     !IS_SPEC(s->type) || !IS_SPEC(t) || s->onStack || s->_isparm ||
     s->addrtaken || s->undefined || s->isitmp || s->iaccess ||
     SPEC_ABSA(s->etype) || SPEC_EXTR(s->etype) ||
     SPEC_OCLS(s->etype)!=data || !isinSet(data->syms,s) || getAddrspace(s->type) ||
     !isVolatile(s->type) || !isVolatile(t) ||
     IS_BITVAR(s->type) || IS_BOOL(s->type) ||
     !wide_value_type(s->type,true) || !wide_value_type(t,true) ||
     getSize(s->type)!=getSize(t) || (getSize(t)!=2 && getSize(t)!=4) ||
     IS_FLOAT(s->type)!=IS_FLOAT(t))return false;
  return true;
}


static bool
wide_symbol_snapshot(wide_function *f,wide_node *n,operand *op,bool store,wide_operand *v)
{
  if(!op || !IS_TRUE_SYMOP(op))return false;
  symbol *s=OP_SYMBOL(op);sym_link *t=operandType(op);memmap *m=SPEC_OCLS(s->etype);
  /* 位读取在原位置取样为规范的 0/1 字节。位写入另行处理，必须检查
   * 完整右值非零，不能先截成一个字节（例如 0x100 仍必须写入 1）。 */
  if(!store && wide_bit_symbol(op)) {
    if(!wide_capacity_room(f,1,1,__LINE__))return false;
    int value=f->nv++;
    f->values[value]=(wide_value){.sym=s,.width=1,.symbolSnapshot=true,
      .reg=-1,.slot=-1,.pin=-1,.preference=-1};
    *v=(wide_operand){.value=value,.width=1};
    wide_node *memory=&f->nodes[f->nn++];
    *memory=(wide_node){.op=WM_BIT_LOAD,.origin=n->origin,.result=value,.width=1,
      .addressSymbol=s,.addressObject=-1,.label=-1,.target=-1,.succ={-1,-1},
      .left={.value=-1},.right={.value=-1},.addressIndex={.value=-1},.byteAccess=true};
    wb_set(&memory->def,value);return true;
  }
  if(s->onStack && (!s->_isparm || wide_register_parameter(s)) &&
     ((IS_SPEC(s->type) && IS_SPEC(t)) ||
      (wide_pointer(s->type) && wide_pointer(t) && wide_code_pointer(t)==wide_code_pointer(s->type) &&
       (!wide_code_pointer(t) || (f->ordinaryEntry && s->_isparm)))) &&
     !IS_BITVAR(t) && !IS_BOOL(t) && wide_value_type(t,true) &&
     getSize(t)==getSize(s->type)) {
    int object,width=wide_width(t);
    if(!wide_capacity_room(f,1,1,__LINE__) || !wide_stack_object(f,s,&object))return false;
    int value=f->nv++;bool floating=IS_FLOAT(t),pointer=wide_pointer(t);
    bool sign=!floating && !pointer && !SPEC_USIGN(getSpec(t));
    /* 直接符号访问和 &local 必须复用同一个对象槽。每次读取重新取样，
     * 不跨别名写入、调用或分支缓存；volatile 保持逐字节访问次序。 */
    f->values[value]=(wide_value){.sym=s,.width=width,.sign=sign,.floating=floating,.pointer=pointer,.codePointer=wide_code_pointer(t),
      .symbolSnapshot=true,.reg=-1,.slot=-1,.pin=-1,.preference=-1};
    *v=(wide_operand){.value=value,.width=width,.sign=sign,.floating=floating,.codePointer=wide_code_pointer(t)};
    wide_node *memory=&f->nodes[f->nn++];
    *memory=(wide_node){.op=store?WM_OBJECT_STORE:WM_OBJECT_LOAD,.origin=n->origin,
      .result=store?-1:value,.width=width,.sign=sign,.floating=floating,
      .addressSymbol=s,.addressObject=object,.pointerAccess=pointer,.label=-1,.target=-1,.succ={-1,-1},
      .left={.value=-1},.right={.value=-1},.addressIndex={.value=-1},
      .byteAccess=isVolatile(t) || isOperandVolatile(op,TRUE)};
    if(store){memory->right=*v;wb_set(&memory->use,value);}
    else wb_set(&memory->def,value);
    return true;
  }
  sym_link *parameterType;int parameterOffset;
  if(wide_parameter_slot(op,&parameterType,&parameterOffset)) {
    if(!wide_capacity_room(f,1,1,__LINE__))return false;
    int value=f->nv++,width=wide_width(parameterType);
    bool floating=IS_FLOAT(parameterType),pointer=wide_pointer(parameterType);
    bool sign=!floating && !pointer && !SPEC_USIGN(getSpec(parameterType));
    /* 每次读写均生成独立内存节点；写回不改变调用方的副本布局，
     * 三字节指针的 MIR 填充字节不能落到邻近参数。 */
    f->values[value]=(wide_value){.sym=s,.width=width,.sign=sign,.pointer=pointer,.codePointer=wide_code_pointer(parameterType),
      .floating=floating,.symbolSnapshot=true,.reg=-1,.slot=-1,.pin=-1,.preference=-1};
    *v=(wide_operand){.value=value,.width=width,.sign=sign,.floating=floating,.codePointer=wide_code_pointer(t)};
    wide_node *memory=&f->nodes[f->nn++];
    *memory=(wide_node){.op=store?WM_PARAM_STORE:WM_PARAM_LOAD,.origin=n->origin,
      .result=store?-1:value,.width=width,.sign=sign,.floating=floating,
      .addressOffset=parameterOffset,.addressObject=-1,.label=-1,.target=-1,.succ={-1,-1},
      .left={.value=-1,.width=getSize(parameterType),.sign=sign},
      .right={.value=-1},.addressIndex={.value=-1}};
    if(store){memory->right=*v;wb_set(&memory->use,value);}
    else wb_set(&memory->def,value);
    return true;
  }
  int width=getSize(t);
  /* 真实对象每次都读写内存，不能按 symbol 合并为长期寄存器值。
   * 先只接纳直接 RAM 标量及单字节 SFR；位空间、分页、CODE、远对象、
   * 指针、形参/栈对象及 atomic 均保持原来的整函数回退。 */
  if(s->onStack || s->_isparm || !IS_SPEC(t) || IS_BITVAR(t) || IS_BOOL(t) ||
     !wide_value_type(t,true) || !IN_DIRSPACE(m) || IN_BITSPACE(m) ||
     IN_CODESPACE(m) || IN_FARSPACE(m) || IN_PAGEDSPACE(m) ||
     (m==sfr && width!=1) || (IN_REGSP(m) && m!=sfr) ||
     !wide_capacity_room(f,1,1,__LINE__))return false;
  int value=f->nv++;
  bool floating=IS_FLOAT(t),sign=!floating && !SPEC_USIGN(getSpec(t));
  f->values[value]=(wide_value){.sym=s,.width=width,.sign=sign,.floating=floating,
    .symbolSnapshot=true,.reg=-1,.slot=-1,.pin=-1,.preference=-1};
  *v=(wide_operand){.value=value,.width=width,.sign=sign,.floating=floating,.codePointer=wide_code_pointer(t)};
  wide_node *memory=&f->nodes[f->nn++];
  *memory=(wide_node){.op=store?WM_SYMBOL_STORE:WM_SYMBOL_LOAD,.origin=n->origin,
    .result=store?-1:value,.width=width,.sign=sign,.floating=floating,
    .addressSymbol=s,.addressObject=-1,.label=-1,.target=-1,.succ={-1,-1},
    .left={.value=-1},.right={.value=-1},.addressIndex={.value=-1},
    .byteAccess=isVolatile(t) || isOperandVolatile(op,TRUE) || m==sfr,
    .volatileDataWideAccess=wide_volatile_data_object(s,t)};
  if(store){memory->right=*v;wb_set(&memory->use,value);}
  else wb_set(&memory->def,value);
  return true;
}

static bool
wide_result (wide_function *f, wide_node *n, operand *op)
{
  wide_operand v;
  if(op && IS_TRUE_SYMOP(op)) {
    if(!wide_symbol_snapshot(f,n,op,true,&v))return false;
  } else if (!wide_operand_from(f,op,&v) || v.value < 0) return false;
  n->result=v.value;wb_set(&n->def,v.value);
  n->width=v.width;n->sign=v.sign;n->floating=v.floating;
  return true;
}

static bool
wide_use (wide_function *f, wide_node *n, operand *op, wide_operand *v)
{
  if(op && IS_TRUE_SYMOP(op)) {
    /* SEND/IPUSH 延迟到 CALL 的原策略只允许纯值。不能将真实对象读取
     * 偷移到调用点；前端已显式取样为 ITEMP 的参数仍按原路径处理。 */
    if(n->op==CALL || !wide_symbol_snapshot(f,n,op,false,v))return false;
  } else if (!wide_operand_from(f,op,v)) return false;
  if (v->value >= 0) {wb_set(&n->use,v->value);++f->values[v->value].reads;f->values[v->value].weight+=1+4*n->origin->depth;}
  return true;
}

static bool
wide_memory(wide_function *f,wide_node *n,iCode *ic,bool store)
{
  operand *p=store?IC_RESULT(ic):IC_LEFT(ic),*value=store?IC_RIGHT(ic):IC_RESULT(ic);
  sym_link *pt=operandType(p),*vt=operandType(value);
  /* 不把只读 CODE 存储退化成 XDATA 写，也不猜测运行期的存储空间。 */
  if(store && wide_code_pointer(pt))return false;
  /* 指针对象物理占三字节，MIR 值占四字节；单独标记访问，
   * 不能用宽寄存器大小读写内存。CODE 指针值及函数/近指针保留回退。 */
  bool pointer=wide_pointer(pt) && wide_pointer(pt->next) && wide_pointer(vt) &&
    !wide_code_pointer(pt) && !wide_code_pointer(pt->next) && !wide_code_pointer(vt);
  if(!wide_pointer(pt) || isAtomic(pt->next) || isAtomic(vt))return false;
  if(!pointer && (!IS_SPEC(pt->next) || (!IS_INTEGRAL(pt->next) && !IS_FLOAT(pt->next)) ||
     IS_BITVAR(pt->next) || !IS_SPEC(vt) || (!IS_INTEGRAL(vt) && !IS_FLOAT(vt)) ||
     IS_FLOAT(pt->next)!=IS_FLOAT(vt) || getSize(pt->next)!=getSize(vt) ||
     (getSize(vt)!=1 && getSize(vt)!=2 && getSize(vt)!=4) ||
     (IS_BITINT(pt->next) && SPEC_BITINTWIDTH(pt->next)!=8*getSize(vt))))return false;
  if(!store && IC_RIGHT(ic) && (!IS_OP_LITERAL(IC_RIGHT(ic)) || operandLitValue(IC_RIGHT(ic))!=0))return false;
  if(!wide_use(f,n,p,&n->left))return false;
  n->op=store?WM_STORE:WM_LOAD;n->width=wide_width(vt);n->codeAccess=wide_code_pointer(pt);n->pointerAccess=pointer;
  /* 易失访问保持原后端的字节宽度、地址递增顺序；普通对象才使用字读写。 */
  n->byteAccess=isVolatile(pt->next) || isOperandVolatile(value,TRUE);
  if(store) {if(!wide_use(f,n,value,&n->right))return false;}
  else if(!wide_result(f,n,value))return false;
  return true;
}


/* 只折叠同一基本块内由真实 DATA 对象和常量偏移形成的近地址。
 * 不接纳运行期近指针、近指针 ABI 或地址空间转换；实际字节读写仍保留。 */
static bool wide_near_type(sym_link *t)
{return IS_PTR(t) && DCL_TYPE(t)==POINTER && getSize(t)==1 && !IS_FUNC(t->next);}

/* 外部 DATA 对象须有完整的无重叠标量布局，真实 volatile/atomic、
 * union、嵌套、数组、位域一律回退；不把声明名称当作硬件或 SDK 能力。
 * 复用冻结来源证明，实际地址仍由普通 direct 符号重定位决定。 */
static bool
wide_extern_data_layout(sym_link *type)
{
  if(!type || isVolatile(type) || isAtomic(type))return false;
  if(!IS_STRUCT(type))return IS_SPEC(type) && IS_INTEGRAL(type) && !IS_BITVAR(type) &&
    (getSize(type)==1 || getSize(type)==2 || getSize(type)==4);
  structdef *layout=SPEC_STRUCT(type);
  if(!layout || layout->type!=STRUCT || !layout->fields || !layout->size ||
     layout->b_flexArrayMember || layout->size!=(unsigned)getSize(type))return false;
  unsigned end=0;
  for(symbol *field=layout->fields;field;field=field->next) {
    sym_link *ft=field->type;
    if(!ft || !IS_SPEC(ft) || !IS_INTEGRAL(ft) || IS_STRUCT(ft) || IS_BITVAR(ft) ||
       field->bitVar || isVolatile(ft) || isAtomic(ft))return false;
    int bytes=getSize(ft);
    if((bytes!=1 && bytes!=2 && bytes!=4) || field->offset<end ||
       field->offset>layout->size || (unsigned)bytes>layout->size-field->offset)return false;
    end=field->offset+bytes;
  }
  return true;
}

/* 零偏移成员可在 IR 中仍保留聚合指针。仅恢复普通 STRUCT 的
 * 第一标量成员同型视图；不扩展运行期指针、union、限定符或类型重解释。 */
static bool
wide_data_zero_member(sym_link *aggregate,sym_link *value,bool store)
{
  if(!aggregate || !value || !IS_STRUCT(aggregate) ||
     !wide_extern_data_layout(aggregate) || !IS_SPEC(value) ||
     !IS_INTEGRAL(value) || IS_BOOL(value) || IS_BITVAR(value) ||
     isVolatile(value) || isAtomic(value) || (store && isConst(aggregate)))return false;
  symbol *field=SPEC_STRUCT(aggregate)->fields;
  sym_link *type=field->type;
  int width=getSize(value);
  if(field->offset || IS_BOOL(type) || (store && isConst(type)) ||
     (width!=1 && width!=2 && width!=4) || getSize(type)!=width ||
     SPEC_NOUN(type)!=SPEC_NOUN(value) || SPEC_USIGN(type)!=SPEC_USIGN(value) ||
     SPEC_LONG(type)!=SPEC_LONG(value) || SPEC_LONGLONG(type)!=SPEC_LONGLONG(value) ||
     (IS_BITINT(value) && (SPEC_BITINTWIDTH(value)!=8*width ||
                          SPEC_BITINTWIDTH(type)!=SPEC_BITINTWIDTH(value))))return false;
  return true;
}

/* 跨块只接受全函数唯一定义且定义块严格支配使用块的临时地址。
 * 不能靠文本先后、单一前驱或函数/SDK 名称猜测来源；多定义、汇合、
 * 不可达和缺失支配信息全部回退。解析后仍只允许原来的纯 DATA 地址链，
 * 不跨块缓存对象内容，也不移动 volatile/内存读取。 */
static iCode *
wide_dominating_near_definition(wide_function *f,operand *p,iCode *begin,eBBlock **source)
{
  if(!f->sourceBlocks)return NULL;
  eBBlock *use=NULL,*owner=NULL;iCode *definition=NULL;
  for(int b=0;b<f->sourceBlocks->count;++b) {
    eBBlock *bb=f->sourceBlocks->bbOrder[b];
    if(bb->sch==begin)use=bb;
    for(iCode *d=bb->sch;d;d=d==bb->ech?NULL:d->next) {
      if(!POINTER_SET(d) && IC_RESULT(d) && IS_SYMOP(IC_RESULT(d)) &&
         OP_SYMBOL(IC_RESULT(d))==OP_SYMBOL(p)) {
        if(definition)return NULL;
        definition=d;owner=bb;
      }
    }
  }
  if(!definition || !use || !owner || owner==use || owner->noPath || use->noPath ||
     !use->domVect || owner->bbnum<0 || owner->bbnum>=use->domVect->size ||
     !bitVectBitValue(use->domVect,owner->bbnum))return NULL;
  *source=owner;return definition;
}

static bool
wide_near_location(wide_function *f,operand *p,iCode *before,iCode *begin,symbol **object,int *offset,unsigned depth)
{
  /* 块首操作没有块内前驱；传 NULL 后只能走严格支配来源，不能线性越过块边界。 */
  if(depth>16 || !p || !IS_ITEMP(p) || !wide_near_type(operandType(p)))return false;
  for(iCode *d=before;d;d=d==begin?NULL:d->prev) {
    if(!POINTER_SET(d) && IC_RESULT(d) && IS_SYMOP(IC_RESULT(d)) &&
       OP_SYMBOL(IC_RESULT(d))==OP_SYMBOL(p)) {
      if(d->op==ADDRESS_OF && IS_TRUE_SYMOP(IC_LEFT(d)) && IC_RIGHT(d) && IS_OP_LITERAL(IC_RIGHT(d))) {
        symbol *o=OP_SYMBOL(IC_LEFT(d));
        if(o->onStack || o->_isparm || SPEC_OCLS(o->etype)!=data ||
           SPEC_ABSA(o->etype) || isAtomic(o->type) || getSize(o->type)<=0 || getSize(o->type)>128)return false;
        if(IS_EXTERN(o->etype) && !wide_extern_data_layout(o->type))return false;
        double at=operandLitValue(IC_RIGHT(d));
        if(at<0 || at>=getSize(o->type) || at!=(int)at)return false;
        *object=o;*offset=(int)at;return true;
      }
      if(d->op==CAST && IC_RIGHT(d) &&
         wide_near_type(operandType(IC_RIGHT(d))) && wide_near_type(operandType(IC_RESULT(d))) &&
         wide_data_zero_member(operandType(IC_RIGHT(d))->next,operandType(IC_RESULT(d))->next,false) &&
         wide_data_zero_member(operandType(IC_RIGHT(d))->next,operandType(IC_RESULT(d))->next,true))
        return wide_near_location(f,IC_RIGHT(d),d==begin?NULL:d->prev,begin,object,offset,depth+1);
      if(d->op=='+' && IC_RIGHT(d) && IS_OP_LITERAL(IC_RIGHT(d)) &&
         wide_near_location(f,IC_LEFT(d),d==begin?NULL:d->prev,begin,object,offset,depth+1)) {
        double at=*offset+operandLitValue(IC_RIGHT(d));
        if(at<0 || at>=getSize((*object)->type) || at!=(int)at)return false;
        *offset=(int)at;return true;
      }
      return false;
    }
  }
  eBBlock *source=NULL;
  iCode *definition=wide_dominating_near_definition(f,p,begin,&source);
  if(!definition)return false;
  if(!wide_near_location(f,p,definition,source->sch,object,offset,depth+1))return false;
  dbuf_printf(f->diagnostics,"; wide dominating DATA origin definition=%d block=%d offset=%d\n",definition->key,source->bbnum,*offset);
  return true;
}

static bool
wide_near_field(wide_function *f,wide_node *n,iCode *ic,iCode *begin)
{
  symbol *object=NULL;int offset=0;
  bool store=POINTER_SET(ic),load=ic->op==GET_VALUE_AT_ADDRESS;
  if(store || load) {
    operand *address=store?IC_RESULT(ic):IC_LEFT(ic),*value=store?IC_RIGHT(ic):IC_RESULT(ic);
    if(!address || !value || !wide_near_type(operandType(address)) ||
       !wide_near_location(f,address,ic==begin?NULL:ic->prev,begin,&object,&offset,0))return false;
    sym_link *pt=operandType(address),*t=operandType(value);
    bool memberView=wide_data_zero_member(pt->next,t,store);
    if(!IS_SPEC(t) || !IS_INTEGRAL(t) || IS_BITVAR(t) || isAtomic(t) ||
       (!memberView && (!IS_SPEC(pt->next) || !IS_INTEGRAL(pt->next) ||
        (getSize(pt->next)!=1 && getSize(pt->next)!=2 && getSize(pt->next)!=4) ||
        IS_BITVAR(pt->next) || isAtomic(pt->next) || getSize(t)!=getSize(pt->next))) ||
       offset+getSize(t)>getSize(object->type))return false;
    if(load && IC_RIGHT(ic) && (!IS_OP_LITERAL(IC_RIGHT(ic)) || operandLitValue(IC_RIGHT(ic))!=0))return false;
    if(store) {if(!wide_use(f,n,value,&n->right))return false;}
    else if(!wide_result(f,n,value))return false;
    n->op=store?WM_SYMBOL_STORE:WM_SYMBOL_LOAD;n->width=getSize(t);
    n->addressSymbol=object;n->addressOffset=offset;
    if(IS_EXTERN(object->etype))dbuf_printf(f->diagnostics,"; wide proven extern DATA field icode=%d offset=%d width=%d\n",ic->key,offset,n->width);
    n->byteAccess=isVolatile(object->type) || isVolatile(pt->next) || isOperandVolatile(value,TRUE);
    f->body=true;
    dbuf_printf(f->diagnostics,"; wide near DATA field icode=%d offset=%d width=%d transaction=bytes\n",ic->key,offset,n->width);
    return true;
  }
  /* 仅将同块已证明的 DATA 符号地址物化为线性 generic 指针。
   * 不读取或转换运行期 near 指针；CODE、栈、absolute 和
   * 超界偏移仍由来源证明拒绝。复用现有三字节符号重定位与普通 ABI。 */
  if(ic->op==CAST && IC_RESULT(ic) && IC_RIGHT(ic) &&
     wide_pointer(operandType(IC_RESULT(ic))) &&
     DCL_TYPE(operandType(IC_RESULT(ic)))==GPOINTER &&
     wide_near_location(f,IC_RIGHT(ic),ic==begin?NULL:ic->prev,begin,&object,&offset,0)) {
    if(!wide_result(f,n,IC_RESULT(ic)))return false;
    n->op=WM_ADDRESS;n->addressSymbol=object;n->addressOffset=offset;
    f->body=true;
    dbuf_printf(f->diagnostics,"; wide near DATA address icode=%d offset=%d destination=generic physical=3\n",ic->key,offset);
    return true;
  }
  if((ic->op==ADDRESS_OF || ic->op=='+' || ic->op==CAST) && IC_RESULT(ic) &&
     wide_near_location(f,IC_RESULT(ic),ic,begin,&object,&offset,0)) {
    /* 纯地址无独立副作用；任何未覆盖的使用仍由正常导入器拒绝整函数。 */
    n->op=WM_NOP;return true;
  }
  return false;
}

static bool
wide_stack_object (wide_function *f, symbol *s, int *object)
{
  int size=getSize(s->type);
  /* 寄存器真实符号用本地槽接收；栈形参继续使用调用方物理槽，不复制。
   * 静态/零长/过大对象保持整函数回退。 */
  if(!s->onStack || (s->_isparm && !wide_register_parameter(s)) || !s->level || IS_STATIC(s->etype) ||
     size<=0 || size>512)return false;
  for(int i=0;i<f->no;++i)if(f->objects[i].sym==s) {*object=i;return true;}
  if(f->no==WO_MAX)return wide_reject(f,"object-capacity",__LINE__);
  if(size>512-f->frame)return wide_reject(f,"object-frame-capacity",__LINE__);
  *object=f->no++;
  f->objects[*object]=(wide_object){.sym=s,.slot=f->frame,.size=size};
  f->frame+=size;
  return true;
}

static bool
wide_parameter_load(wide_function *f,wide_node *n,iCode *ic)
{
  sym_link *t;int offset;
  if(!wide_parameter_slot(IC_RIGHT(ic),&t,&offset) ||
     (ic->op==CAST && (IS_BOOL(operandType(IC_RESULT(ic))) || IS_BIT(operandType(IC_RESULT(ic))))) ||
     !wide_result(f,n,IC_RESULT(ic)) || n->floating!=IS_FLOAT(t) ||
     (ic->op=='=' && n->width!=wide_width(t)) ||
     (wide_pointer(t) && wide_pointer(operandType(IC_RESULT(ic))) &&
      wide_code_pointer(t)!=f->values[n->result].codePointer))return false;
  n->op=WM_PARAM_LOAD;n->addressOffset=offset;
  n->left.width=getSize(t);n->left.sign=!wide_pointer(t) && !n->floating && !SPEC_USIGN(getSpec(t));return true;
}

static bool
wide_pointer_offset(iCode *ic,wide_operand *v)
{
  operand *op=IC_RIGHT(ic);
  if((ic->op!='+' && ic->op!='-') || !wide_pointer(operandType(IC_RESULT(ic))) ||
     !wide_pointer(operandType(IC_LEFT(ic))) || !IS_OP_LITERAL(op) ||
     !IS_INTEGRAL(operandType(op)) || getSize(operandType(op))!=8)return false;
  uint64_t bits=operandLitValueUll(op);
  bool negative=!SPEC_USIGN(getSpec(operandType(op))) && (bits&UINT64_C(0x8000000000000000));
  /* 前端结构字段偏移使用 long long。仅精确可表示的常量进入地址运算，
   * 不截断一般 64 位值，不经 double 转换，也不开放 64 位整数算术。 */
  if(negative ? bits<UINT64_C(0xffffffff80000000) : bits>UINT64_C(0xffffffff))return false;
  *v=(wide_operand){.value=-1,.width=4,.literal=(uint32_t)bits,.sign=negative};return true;
}

static bool
wide_range_u32(operand *o)
{
  if(!o)return false;
  sym_link *t=operandType(o);
  return IS_SPEC(t) && IS_INTEGRAL(t) && IS_UNSIGNED(t) && getSize(t)==4 &&
    !IS_BITINT(t) && !isVolatile(t) && !isAtomic(t) && !isOperandVolatile(o,TRUE);
}

static void
wide_capture_multiply_range(wide_function *f,wide_node *n,iCode *ic,operand *a,operand *b)
{
  if(!mcs251RangeNarrowMultiplyEnabled || !n->ordinaryCall || n->args!=2 ||
     n->stackArgs!=1 || n->stackBytes!=4 || n->callee!=muldiv[0][2][0] ||
     !n->callee->cdef || !wide_range_u32(a) || !wide_range_u32(b) ||
     !wide_range_u32(IC_RESULT(ic)))return;
  /* __mullong 是编译器拥有的 signed long 原型，unsigned C 乘法也复用。
   * 限定其原始精确 ABI；源表达式的实际实参和结果另须全部是 u32。
   * 低 32 位模乘不把 formal signed 原型误作 signed C 的优化授权。 */
  sym_link *t=n->callee->type;value *args=FUNC_ARGS(t);
  if(!IS_SPEC(t->next) || !IS_INTEGRAL(t->next) || IS_UNSIGNED(t->next) || getSize(t->next)!=4 ||
     !args || !args->next || args->next->next ||
     !IS_SPEC(args->type) || !IS_INTEGRAL(args->type) || IS_UNSIGNED(args->type) || getSize(args->type)!=4 ||
     !IS_SPEC(args->next->type) || !IS_INTEGRAL(args->next->type) || IS_UNSIGNED(args->next->type) || getSize(args->next->type)!=4)return;
  operand *inputs[2]={a,b};
  for(unsigned k=0;k<2;++k) {
    struct valinfo v=getIsolatedOperandValinfo(f->rangeAnalysis,ic,inputs[k]);
    bool bounded=!v.nothing && !v.anything && v.min>=0 && v.max>=v.min && v.max<=65535;
    if(bounded)n->rangeMultiplyMask|=1u<<k;
    dbuf_printf(f->diagnostics,"; wide range-multiply fact icode=%d argument=%u nothing=%u anything=%u min=%lld max=%lld known-mask=%llx known-bits=%llx narrow=%u\n",
      ic->key,k+1,v.nothing,v.anything,v.min,v.max,v.knownbitsmask,v.knownbits,bounded);
  }
}

static bool
wide_import (wide_function *f, ebbIndex *ebbi)
{
  f->sourceBlocks=ebbi;
  operand *pending[2]={NULL,NULL};
  unsigned pendingMask=0,pushedCount=0;
  operand *pushed[WA_MAX-1]={NULL};
  wide_operand pendingSample[2],pushedSample[WA_MAX-1];
  bool pendingSampled[2]={false,false},pushedSampled[WA_MAX-1]={false};
  for (int b=0;b<ebbi->count;++b) {
    eBBlock *bb=ebbi->bbOrder[b];
    if (!bb->sch) continue;
    if (bb->noPath && optimize.label4 && bb->entryLabel != entryLabel && bb->entryLabel != returnLabel) continue;
    for (iCode *ic=bb->sch;ic;ic=ic==bb->ech?NULL:ic->next) {
      /* 可选 pattern 的不匹配不是拒绝；仅对当前最终失败 iCode 给出原因。 */
      f->rejectionReason=NULL;f->rejectionLine=0;f->rejectionOrigin=ic;
      if (!wide_capacity_room(f,0,1,__LINE__)) return wide_import_reject(f,ic,__LINE__);
      int first=f->nn;
      wide_node *n=&f->nodes[f->nn++];
      n->op=ic->op;n->origin=ic;n->result=-1;n->left.value=n->right.value=n->addressIndex.value=-1;
      n->target=n->label=-1;n->succ[0]=n->succ[1]=-1;n->addressObject=-1;
      /* 参数的寄存器搬运在 CALL 并行完成；真实对象读取必须保留在
       * 原 SEND/IPUSH 的位置，不能合并重复对象或推迟 volatile 取样。 */
      if((pendingMask || pushedCount) && ic->op!=SEND && ic->op!=IPUSH && ic->op!=CALL) return wide_import_reject(f,ic,__LINE__);
      if(wide_near_field(f,n,ic,bb->sch))goto wide_import_order_samples;
      switch (ic->op) {
      case FUNCTION: case ENDFUNCTION: break;
      case INLINEASM:
        if(!wide_ordered_asm_import(f,n,ic))return wide_import_reject(f,ic,__LINE__);
        f->body=true;break;
      case DUMMY_READ_VOLATILE: {
        /* 赋值表达式的弃用值也可能要求一次真实 volatile 位读取。
         * 与 genDummyRead 一致按 RIGHT、LEFT 次序执行，不合并相同位。
         * 这是 C IR 语义能力，不借 ASM 位掩码授予任意地址访问。 */
        if(!f->orderedAsmEnabled)return wide_reject(f,"ordered-volatile-bit-read-disabled",__LINE__);
        operand *reads[2]={IC_RIGHT(ic),IC_LEFT(ic)};
        for(unsigned j=0;j<2;++j) {
          operand *read=reads[j];if(!read)continue;
          if(!wide_bit_symbol(read) || !isOperandVolatile(read,TRUE))
            return wide_reject(f,"unsupported-volatile-dummy-read-operand",__LINE__);
          n->orderedReadSymbols[n->orderedReadCount++]=OP_SYMBOL(read);
        }
        if(!n->orderedReadCount)return wide_reject(f,"empty-volatile-dummy-read",__LINE__);
        n->op=WM_VOLATILE_BIT_READ;n->byteAccess=true;f->body=true;
        dbuf_printf(f->diagnostics,"; wide volatile-bit-read admitted icode=%d reads=%u order=right-left carry-clobber=scratch\n",ic->key,n->orderedReadCount);
        break;
      }
      case LABEL: n->label=IC_LABEL(ic)->key;break;
      case GOTO: n->target=IC_LABEL(ic)->key;break;
      case RECEIVE:
        if (f->body || ic->argreg<1 || ic->argreg>wide_register_count(currFunc->type) ||
            !wide_result(f,n,IC_RESULT(ic))) return wide_import_reject(f,ic,__LINE__);
        /* 普通首参在 DPL/DPH/B/A，发射时先快照再写已分配宽值。
         * 不能像 ABI4 一样把 RECEIVE 固定到 DR4/DR12。 */
        if(!f->ordinaryEntry)f->values[n->result].pin=(ic->argreg==1?8:16)-n->width;
        ++f->received;
        break;
      case RETURN:
        if(IC_LEFT(ic)) {
          if (!wide_use(f,n,IC_LEFT(ic),&n->left)) return wide_import_reject(f,ic,__LINE__);
          if(n->left.floating!=IS_FLOAT(currFunc->type->next) || n->left.width!=wide_width(currFunc->type->next))return wide_import_reject(f,ic,__LINE__);
          n->width=n->left.width;
        } else if(!IS_VOID(currFunc->type->next)) return wide_import_reject(f,ic,__LINE__);
        if(n->left.value>=0) f->values[n->left.value].preference=8-n->width;
        f->body=true;break;
      case IPUSH: {
        /* 连续实参仍由 CALL 统一压入物理栈。真实对象在此刻生成独立
         * 内存节点，后续只运输其快照；现有内存类型/字节访问门槛不变。 */
        if(!ic->parmPush || pushedCount==WA_MAX-1)return wide_import_reject(f,ic,__LINE__);
        unsigned slot=pushedCount++;operand *input=IC_LEFT(ic);pushed[slot]=input;
        pushedSampled[slot]=input && IS_TRUE_SYMOP(input);
        if(pushedSampled[slot]) {
          if(!wide_symbol_snapshot(f,n,input,false,&pushedSample[slot]))return wide_import_reject(f,ic,__LINE__);
          wide_node *memory=&f->nodes[f->nn-1];
          if(memory->op!=WM_SYMBOL_LOAD && memory->op!=WM_BIT_LOAD)
            return wide_reject(f,"argument-snapshot-memory-contract",__LINE__);
          memory->byteAccess=true;memory->volatileDataWideAccess=false;
          dbuf_printf(f->diagnostics,"; wide argument snapshot icode=%d kind=stack slot=%u value=%d width=%d\n",ic->key,slot,pushedSample[slot].value,pushedSample[slot].width);
        }
        n->op=WM_NOP;f->body=true;break;
      }
      case SEND: {
        if(ic->argreg<1 || ic->argreg>2 || (pendingMask&(1u<<(ic->argreg-1)))) return wide_import_reject(f,ic,__LINE__);
        unsigned slot=ic->argreg-1;operand *input=IC_LEFT(ic);pending[slot]=input;
        pendingSampled[slot]=input && IS_TRUE_SYMOP(input);
        if(pendingSampled[slot]) {
          if(!wide_symbol_snapshot(f,n,input,false,&pendingSample[slot]))return wide_import_reject(f,ic,__LINE__);
          wide_node *memory=&f->nodes[f->nn-1];
          if(memory->op!=WM_SYMBOL_LOAD && memory->op!=WM_BIT_LOAD)
            return wide_reject(f,"argument-snapshot-memory-contract",__LINE__);
          memory->byteAccess=true;memory->volatileDataWideAccess=false;
          memory->argumentReadReverse=memory->width>1;
          dbuf_printf(f->diagnostics,"; wide argument snapshot icode=%d kind=register slot=%u value=%d width=%d\n",ic->key,slot,pendingSample[slot].value,pendingSample[slot].width);
          n->op=WM_NOP;
        }
        pendingMask|=1u<<slot;f->body=true;break;
      }
      case CALL: {
        /* 普通入口复用同一调用保存与物理栈编组；CALL 的 ABI、可重入、
         * 类型与 cdef 门槛仍逐项验证。跨普通入口的尾跳转仍禁用，
         * 确保返回值转换和本帧释放由当前函数完成。 */
        sym_link *t=operandType(IC_LEFT(ic));
        if(!IS_SYMOP(IC_LEFT(ic)) || !IS_FUNC(t) || (FUNC_SDCCCALL(t)!=4 && FUNC_SDCCCALL(t)!=2) ||
           !IFFUNC_ISREENT(t) || IFFUNC_ISISR(t) || IFFUNC_ISNAKED(t) ||
           IFFUNC_ISCRITICAL(t) || IFFUNC_CALLEESAVES(t) || IFFUNC_ISBANKEDCALL(t) ||
           FUNC_REGBANK(t) || IFFUNC_HASVARARGS(t) || IFFUNC_ISNORETURN(t) ||
           (!wide_abi_type(t->next,t) && !IS_VOID(t->next)) || !wide_stack_signature(t)) return wide_import_reject(f,ic,__LINE__);
        /* 普通可重入 ABI 仅第一个标量在 DPL/DPH/B/A，其余按原宽度压栈。
         * 非可重入静态形参、间接调用及其他调用约定仍整体回退。 */
        n->ordinaryCall=FUNC_SDCCCALL(t)==2;
        /* 新快照的物理取样顺序以普通 ABI2 的 SEND/IPUSH 为合同。
         * ABI4 真实对象实参没有该顺序证明，保持此前整函数回退。 */
        if(!n->ordinaryCall) {
          for(unsigned j=0;j<2;++j)if(pendingSampled[j])return wide_reject(f,"argument-snapshot-abi4-contract",__LINE__);
          for(unsigned j=0;j<pushedCount;++j)if(pushedSampled[j])return wide_reject(f,"argument-snapshot-abi4-contract",__LINE__);
        }
        unsigned runtimeOperation=0,runtimeWidth=0,runtimeSigned=0;
        bool compilerIntegerRuntime=wide_integer_runtime_contract(OP_SYMBOL(IC_LEFT(ic)),&runtimeOperation,&runtimeWidth,&runtimeSigned);
        if(n->ordinaryCall && OP_SYMBOL(IC_LEFT(ic))->cdef && !compilerIntegerRuntime)
          return wide_import_reject(f,ic,__LINE__);
        if(compilerIntegerRuntime)
          dbuf_printf(f->diagnostics,"; wide compiler-runtime-call icode=%d operation=%u bytes=%u signed=%u abi=2 preserve=full-live-registers\n",ic->key,runtimeOperation,runtimeWidth,runtimeSigned);
        unsigned registers=n->ordinaryCall?1:2,count=0,bytes=0;
        for(value *arg=FUNC_ARGS(t);arg;arg=arg->next) {
          if(count==WA_MAX || !wide_abi_type(arg->type,t)) return wide_import_reject(f,ic,__LINE__);
          wide_operand *o,*sample=NULL;
          operand *input;
          if(count<registers) {o=count?&n->right:&n->left;input=pending[count];if(pendingSampled[count])sample=&pendingSample[count];}
          else {
            if(count-registers>=pushedCount)return wide_import_reject(f,ic,__LINE__);
            unsigned slot=pushedCount-1-(count-registers);
            o=&n->stackOperands[slot];input=pushed[slot];if(pushedSampled[slot])sample=&pushedSample[slot];
            n->stackWidths[pushedCount-1-(count-registers)]=getSize(arg->type);
            bytes+=getSize(arg->type);
          }
          if(!input)return wide_import_reject(f,ic,__LINE__);
          if(sample) {
            *o=*sample;
            if(o->value<0)return wide_import_reject(f,ic,__LINE__);
            wb_set(&n->use,o->value);++f->values[o->value].reads;
            f->values[o->value].weight+=1+4*n->origin->depth;
          } else if(!wide_use(f,n,input,o))return wide_import_reject(f,ic,__LINE__);
          if(o->width!=wide_width(arg->type) || o->floating!=IS_FLOAT(arg->type) || o->codePointer!=wide_code_pointer(arg->type)) {
            dbuf_printf(f->diagnostics,"; wide call argument deferred icode=%d arg=%u width=%d expected=%d float=%d/%d code=%d/%d literal=%lu value=%d\n",ic->key,count,o->width,wide_width(arg->type),o->floating,IS_FLOAT(arg->type),o->codePointer,wide_code_pointer(arg->type),(unsigned long)o->literal,o->value);
            return wide_import_reject(f,ic,__LINE__);
          }
          ++count;
        }
        if(pendingMask!=((1u<<(count<registers?count:registers))-1) ||
           pushedCount!=(count>registers?count-registers:0) || bytes!=ic->parmBytes)return wide_import_reject(f,ic,__LINE__);
        n->stackArgs=pushedCount;n->stackBytes=bytes;
        n->args=count;n->callee=OP_SYMBOL(IC_LEFT(ic));
        if(IC_RESULT(ic) && !IS_VOID(t->next) &&
           (!wide_result(f,n,IC_RESULT(ic)) || n->width!=wide_width(t->next) || n->floating!=IS_FLOAT(t->next))) return wide_import_reject(f,ic,__LINE__);
        if(n->ordinaryCall && count==2 && pushedCount==1)
          wide_capture_multiply_range(f,n,ic,pending[0],pushed[0]);
        pending[0]=pending[1]=NULL;pendingMask=pushedCount=0;f->body=true;break;
      }
      case JUMPTABLE: {
        int count=elementsInSet(IC_JTLABELS(ic));
        /* 前端先做原宽度的上下界比较和最小值归一化，随后索引为 0..255。
         * 不删除这些检查；超出边池容量时整函数回退，不截断表项。 */
        if(count<=0 || count>256)return wide_import_reject(f,ic,__LINE__);
        if(count>WJ_MAX-f->nj)return wide_reject(f,"jump-edge-capacity",__LINE__);
        if(!wide_use(f,n,IC_JTCOND(ic),&n->left) || n->left.floating ||
           wide_pointer(operandType(IC_JTCOND(ic))))return wide_import_reject(f,ic,__LINE__);
        n->jumpStart=f->nj;n->jumpCount=count;
        /* 只读遍历；setFirst/NextItem 会改动共享集合的 curr 游标。 */
        for(const set *entry=IC_JTLABELS(ic);entry;entry=entry->next)
          f->jumpLabels[f->nj++]=((const symbol *)entry->item)->key;
        if(f->nj!=n->jumpStart+count)return wide_import_reject(f,ic,__LINE__);
        f->body=true;break;
      }
      case IFX:
        if (!wide_use(f,n,IC_COND(ic),&n->left) || (!IC_TRUE(ic) && !IC_FALSE(ic))) return wide_import_reject(f,ic,__LINE__);
        /* -0.0f 不能用整数非零判断；未降为支持调用的浮点语义整体回退。 */
        if(n->left.floating)return wide_import_reject(f,ic,__LINE__);
        n->target=(IC_TRUE(ic)?IC_TRUE(ic):IC_FALSE(ic))->key;n->trueBranch=IC_TRUE(ic)!=NULL;
        n->width=n->left.width;f->body=true;break;
      case '=': case CAST:
        if(wide_parameter_load(f,n,ic)) {f->body=true;break;}
        if(POINTER_SET(ic)) {if(!wide_memory(f,n,ic,true))return wide_import_reject(f,ic,__LINE__);f->body=true;break;}
        if(wide_bit_symbol(IC_RESULT(ic))) {
          if(!wide_use(f,n,IC_RIGHT(ic),&n->right) || n->right.floating ||
             !IS_INTEGRAL(operandType(IC_RIGHT(ic))))return wide_import_reject(f,ic,__LINE__);
          n->op=WM_BIT_STORE;n->addressSymbol=OP_SYMBOL(IC_RESULT(ic));
          f->body=true;break;
        }
        if(ic->op==CAST && IS_BIT(operandType(IC_RESULT(ic)))) {
          /* 位临时值必须规范化为 0/1；不能先截为字节，否则 0x100 会变成假。
           * 真实位对象的访问仍由独立节点完成，不缓存或合并 volatile 访问。 */
          if(!IS_ITEMP(IC_RESULT(ic)) || !wide_result(f,n,IC_RESULT(ic)) || n->width!=1 ||
             !wide_use(f,n,IC_RIGHT(ic),&n->right) || n->right.floating ||
             !IS_INTEGRAL(operandType(IC_RIGHT(ic))))return wide_import_reject(f,ic,__LINE__);
          n->op=WM_BIT_CAST;f->body=true;break;
        }
        if (ic->op==CAST && IS_BOOL(operandType(IC_RESULT(ic)))) return wide_import_reject(f,ic,__LINE__);
        if (POINTER_SET(ic) || !wide_result(f,n,IC_RESULT(ic)) || !wide_use(f,n,IC_RIGHT(ic),&n->right)) return wide_import_reject(f,ic,__LINE__);
        if(n->floating!=n->right.floating)return wide_import_reject(f,ic,__LINE__);
        /* CODE 与数据指针的直接转换保留旧路径；整数到明确类型指针的
         * 转换按原有 24 位截断规则规范化，不能丢失指针的读取空间。 */
        if(wide_pointer(operandType(IC_RESULT(ic))) && wide_pointer(operandType(IC_RIGHT(ic))) &&
           f->values[n->result].codePointer!=n->right.codePointer)return wide_import_reject(f,ic,__LINE__);
        if (ic->op=='=' && n->width!=n->right.width) return wide_import_reject(f,ic,__LINE__);
        f->body=true;break;
      case GET_VALUE_AT_ADDRESS:
        if(!wide_memory(f,n,ic,false))return wide_import_reject(f,ic,__LINE__);f->body=true;break;
      case ADDRESS_OF: {
        if(!wide_pointer(operandType(IC_RESULT(ic))) || !wide_result(f,n,IC_RESULT(ic)) ||
           !IS_TRUE_SYMOP(IC_LEFT(ic)) || !IC_RIGHT(ic) || !IS_OP_LITERAL(IC_RIGHT(ic)))return wide_import_reject(f,ic,__LINE__);
        symbol *s=OP_SYMBOL(IC_LEFT(ic));memmap *m=SPEC_OCLS(s->etype);
        bool code=wide_code_pointer(operandType(IC_RESULT(ic)));
        if(s->onStack) {
          if(code)return wide_import_reject(f,ic,__LINE__);
          if(operandLitValue(IC_RIGHT(ic))<0 || operandLitValue(IC_RIGHT(ic))>getSize(s->type))return wide_import_reject(f,ic,__LINE__);
          if(s->_isparm) {
            sym_link *parameterType;int offset;
            f->formalAddress=true;
            if(wide_parameter_slot(IC_LEFT(ic),&parameterType,&offset)) {
              n->op=WM_PARAM_ADDRESS;n->addressSymbol=s;
              n->addressOffset=offset+(int)operandLitValue(IC_RIGHT(ic));f->body=true;break;
            }
          }
          if(!wide_stack_object(f,s,&n->addressObject))return wide_import_reject(f,ic,__LINE__);
        } else if(IN_CODESPACE(m)) {if(!code)return wide_import_reject(f,ic,__LINE__);}
        else if(code || !IN_FARSPACE(m) || m->paged)return wide_import_reject(f,ic,__LINE__);
        n->op=WM_ADDRESS;n->addressSymbol=s;n->addressOffset=(int)operandLitValue(IC_RIGHT(ic));f->body=true;break;
      }
      case '!':
        if (!wide_result(f,n,IC_RESULT(ic)) || !wide_use(f,n,IC_LEFT(ic),&n->left)) return wide_import_reject(f,ic,__LINE__);
        if(n->floating || n->left.floating)return wide_import_reject(f,ic,__LINE__);
        f->body=true;break;
      case UNARYMINUS:
        /* 同宽有符号整数在 C 定义域内等于 0-x；复用现有 SUB，
         * 不插入饱和/越界语义，也不改变前端整型提升和截断。
         * 最小负数保持目标已有二补码位模式，但不把 C 有符号溢出
         * 声称为新增的定义域。指针、浮点和宽度/符号不匹配继续回退。
         * 窄无符号历史成本负例仍由原研究开关单独控制。 */
        if (!wide_result(f,n,IC_RESULT(ic)) || !wide_use(f,n,IC_LEFT(ic),&n->right) ||
            n->floating || n->right.floating || n->sign!=n->right.sign ||
            n->width!=n->right.width ||
            (n->width!=1 && n->width!=2 && n->width!=4) ||
            wide_pointer(operandType(IC_RESULT(ic))) || wide_pointer(operandType(IC_LEFT(ic))) ||
            (!n->sign && n->width!=4 &&
             (!getenv("MX251_NARROW_UNSIGNED_NEGATE") || strcmp(getenv("MX251_NARROW_UNSIGNED_NEGATE"),"1"))))
          return wide_import_reject(f,ic,__LINE__);
        if(n->sign)dbuf_printf(f->diagnostics,"; wide signed integer negate icode=%d width=%d domain=defined-signed-integer\n",ic->key,n->width);
        n->op='-';n->left=(wide_operand){.value=-1,.width=n->width,.literal=0,.sign=n->sign};
        f->body=true;break;
      case GETABIT:
        /* 前端已把单比特掩码转成值选位；只接收范围内的常量位置。
         * 输出为规范化字节 0/1，真实对象的 volatile 快照仍由 wide_use 产生。
         */
        if (!wide_result(f,n,IC_RESULT(ic)) || !wide_use(f,n,IC_LEFT(ic),&n->left) ||
            !wide_use(f,n,IC_RIGHT(ic),&n->right) || n->width!=1 || n->sign ||
            n->floating || n->left.floating || n->right.floating ||
            n->right.value>=0 || n->right.literal>=8u*n->left.width ||
            wide_pointer(operandType(IC_LEFT(ic)))) return wide_import_reject(f,ic,__LINE__);
        f->body=true;break;
      case GETBYTE:
        /* 只选择值中的一个完整字节，不改变产生该值的内存读取。
         * 偏移以最低有效位为零；带符号结果及指针继续交给原后端。
         */
        if (!wide_result(f,n,IC_RESULT(ic)) || !wide_use(f,n,IC_LEFT(ic),&n->left) ||
            !wide_use(f,n,IC_RIGHT(ic),&n->right) || n->width!=1 || n->sign ||
            n->floating || n->left.floating || n->right.floating ||
            n->right.value>=0 || n->right.literal%8 ||
            n->right.literal>(unsigned)(8*(n->left.width-1)) ||
            wide_pointer(operandType(IC_LEFT(ic)))) return wide_import_reject(f,ic,__LINE__);
        f->body=true;break;
      case GETWORD:
        /* 位偏移按最低有效位计；只接受完整落在输入中的常量字节边界。 */
        if (!wide_result(f,n,IC_RESULT(ic)) || !wide_use(f,n,IC_LEFT(ic),&n->left) ||
            !wide_use(f,n,IC_RIGHT(ic),&n->right) || n->width!=2 || n->sign ||
            n->floating || n->left.floating || n->right.floating ||
            n->left.width<2 || n->right.value>=0 || n->right.literal%8 ||
            n->right.literal>(unsigned)(8*(n->left.width-2)) ||
            wide_pointer(operandType(IC_LEFT(ic)))) return wide_import_reject(f,ic,__LINE__);
        f->body=true;break;
      case '/': case '%': {
        /* 无符号同宽整数除以正二次幂可精确改写为右移或低位掩码。
         * 使用 IR 已取样值；不移动 volatile 访问、不修改运行库算法。
         * 有符号/浮点/指针、零除和不匹配宽度保留原有回退。 */
        if(!wide_result(f,n,IC_RESULT(ic)) || !wide_use(f,n,IC_LEFT(ic),&n->left) ||
           !wide_use(f,n,IC_RIGHT(ic),&n->right) ||
           n->floating || n->left.floating || n->right.floating ||
           wide_pointer(operandType(IC_RESULT(ic))) || wide_pointer(operandType(IC_LEFT(ic))) ||
           wide_pointer(operandType(IC_RIGHT(ic))))return wide_import_reject(f,ic,__LINE__);
        uint32_t divisor=n->right.literal;
        bool positive=n->right.value<0 && divisor &&
          (!n->right.sign || !(divisor&(UINT32_C(1)<<(8*n->right.width-1))));
        if(!n->sign && !n->left.sign && n->width==n->left.width && positive &&
           !(divisor&(divisor-1))) {
          unsigned shift=0;for(uint32_t d=divisor;d>1;d>>=1)++shift;
          if(shift<8u*n->width) {
            n->op=ic->op=='/'?RIGHT_OP:BITWISEAND;
            n->right=(wide_operand){.value=-1,.width=n->width,.literal=ic->op=='/'?shift:divisor-1,.sign=false};
            dbuf_printf(f->diagnostics,"; wide unsigned power2 icode=%d width=%d shift=%u result=%s\n",ic->key,n->width,shift,ic->op=='/'?"quotient":"remainder");
            f->body=true;break;
          }
        }
        /* 原研究开关的原生 16 位 DIV 准入保持独立，不随二次幂改写开放。 */
        if(!getenv("MX251_WIDE_UNSIGNED_DIVISION") || strcmp(getenv("MX251_WIDE_UNSIGNED_DIVISION"),"1") ||
           n->width!=2 || n->left.width!=2 || n->right.width!=2 ||
           n->sign || n->left.sign || n->right.sign ||
           (n->right.value<0 && !divisor)) {
          dbuf_printf(f->diagnostics,"; wide division deferred result=%d/%d left=%d/%d right=%d/%d literal=%lu value=%d\n",n->width,n->sign,n->left.width,n->left.sign,n->right.width,n->right.sign,(unsigned long)divisor,n->right.value);
          return wide_import_reject(f,ic,__LINE__);
        }
        n->op=ic->op=='/'?WM_UDIVW:WM_UMODW;f->body=true;break;
      }
      case '*':
        /* 无符号窄因子先零扩展到 WR，MUL 保留完整 32 位积。
         * 新增的 8x8->8 按模 256 取低字节，与前端的截断语义一致；
         * 其他截断宽度、有符号、指针及宽因子仍回退，不扩大未经验证的路径。 */
        if (!wide_result(f,n,IC_RESULT(ic)) || !wide_use(f,n,IC_LEFT(ic),&n->left) ||
            !wide_use(f,n,IC_RIGHT(ic),&n->right) || n->sign || n->left.sign || n->right.sign ||
            n->floating || n->left.floating || n->right.floating ||
            (n->width!=1 && n->width!=2 && n->width!=4) || n->left.width>2 || n->right.width>2 ||
            (n->left.width+n->right.width>n->width &&
             !(n->width==1 && n->left.width==1 && n->right.width==1)) ||
            wide_pointer(operandType(IC_RESULT(ic))) || wide_pointer(operandType(IC_LEFT(ic))) ||
            wide_pointer(operandType(IC_RIGHT(ic)))) return wide_import_reject(f,ic,__LINE__);
        n->op=WM_UMULW;f->body=true;break;
      case '+': case '-': case '^': case '|': case BITWISEAND:
      case LEFT_OP: case RIGHT_OP:
      case '<': case '>': case LE_OP: case GE_OP: case EQ_OP: case NE_OP:
        if (!wide_result(f,n,IC_RESULT(ic)) || !wide_use(f,n,IC_LEFT(ic),&n->left) ||
            (!wide_use(f,n,IC_RIGHT(ic),&n->right) && !wide_pointer_offset(ic,&n->right))) return wide_import_reject(f,ic,__LINE__);
        /* 浮点算术必须先由前端降为已声明 ABI 的运行库调用。 */
        if(n->floating || n->left.floating || n->right.floating)return wide_import_reject(f,ic,__LINE__);
        if(!IS_CONDITIONAL(ic) && ic->op!=LEFT_OP && ic->op!=RIGHT_OP && n->right.value<0 && n->right.width<n->width) {
          if(n->right.sign && (n->right.literal&(UINT32_C(1)<<(8*n->right.width-1))))n->right.literal|=~((UINT32_C(1)<<(8*n->right.width))-1);
          if(n->width<4)n->right.literal&=(UINT32_C(1)<<(8*n->width))-1;
          n->right.width=n->width;
        }
        if (ic->op==LEFT_OP || ic->op==RIGHT_OP) {
          if (n->width!=n->left.width) return wide_import_reject(f,ic,__LINE__);
          if (n->right.value<0 && n->right.literal>=8u*n->width) return wide_import_reject(f,ic,__LINE__);
        } else if (IS_CONDITIONAL(ic)) {
          if (n->left.width!=n->right.width || n->left.sign!=n->right.sign) return wide_import_reject(f,ic,__LINE__);
        } else if(n->width!=n->left.width || n->width!=n->right.width) {
          /* 非零起点的 switch 会生成宽输入减常量、字节结果，再接跳转表。
           * 这里只接纳此形状；原宽度范围比较仍在前面。按模 256 保留低字节
           * 与宽减法后截断一致，wload 仍按原输入宽度定位低字，不能改窄类型。 */
          bool jumpNormalize=ic->op=='-' && n->width==1 && !n->sign &&
            n->right.value<0 && n->left.width==n->right.width && n->left.width>1 &&
            !wide_pointer(operandType(IC_LEFT(ic))) && ic->next && ic->next->op==JUMPTABLE &&
            IS_ITEMP(IC_JTCOND(ic->next)) && OP_SYMBOL(IC_JTCOND(ic->next))==OP_SYMBOL(IC_RESULT(ic));
          /* 前端范围收缩可留下 uchar & mask -> int 的混合宽度。
           * 仅接纳无符号整数按位操作；wload 按源宽度零扩展到结果宽度，
           * 不改变原值类型/活跃范围，也不放开指针或截断运算。 */
          bool unsignedBitwisePromotion=(ic->op=='^' || ic->op=='|' || ic->op==BITWISEAND) &&
            !f->values[n->result].pointer && !n->left.sign && !n->right.sign &&
            n->left.width<=n->width && n->right.width<=n->width &&
            !wide_pointer(operandType(IC_LEFT(ic))) && !wide_pointer(operandType(IC_RIGHT(ic)));
          if(!jumpNormalize && !unsignedBitwisePromotion && (!f->values[n->result].pointer || (ic->op!='+' && ic->op!='-') || n->left.width!=4 || n->right.width>4))return wide_import_reject(f,ic,__LINE__);
        }
        f->body=true;break;
      default: return wide_import_reject(f,ic,__LINE__);
      }
wide_import_order_samples:
      /* 操作数取样放在原 iCode 前，结果写回放在后。仅重排本条导入时
       * 生成的内存节点，不跨其它 iCode、调用、分支或 volatile 访问。 */
      if(f->nn>first+1) {
        wide_node operation=*n,writeback;bool hasWriteback=false;int at=first;
        for(int k=first+1;k<f->nn;++k) {
          wide_node memory=f->nodes[k];
          if(memory.op==WM_SYMBOL_STORE || memory.op==WM_PARAM_STORE || memory.op==WM_OBJECT_STORE) {if(hasWriteback)return wide_import_reject(f,ic,__LINE__);writeback=memory;hasWriteback=true;}
          else {if(memory.op!=WM_SYMBOL_LOAD && memory.op!=WM_BIT_LOAD && memory.op!=WM_PARAM_LOAD && memory.op!=WM_OBJECT_LOAD)return wide_import_reject(f,ic,__LINE__);f->nodes[at++]=memory;}
        }
        f->nodes[at++]=operation;
        if(hasWriteback)f->nodes[at++]=writeback;
        wassert(at==f->nn);
      }
    }
  }
  if (!f->nn || f->received>2 || pendingMask || pushedCount) return wide_reject(f,"incomplete-function-or-call-sequence",__LINE__);
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    if(n->op!=RETURN && n->op!=ENDFUNCTION && n->op!=GOTO && n->op!=JUMPTABLE && i+1<f->nn) n->succ[0]=i+1;
    if(n->target>=0) {
      int target=-1;
      for(int j=0;j<f->nn;++j) if(f->nodes[j].label==n->target) {target=j;break;}
      if(target<0) {f->rejectionOrigin=n->origin;return wide_reject(f,"missing-branch-target",__LINE__);}
      n->succ[n->op==GOTO?0:1]=target;
    }
  }
  /* 所有表项都是真实后继。缺失目标必须回退，不能把跳转表当作直落边。 */
  for(int k=0;k<f->nj;++k) {
    int target=-1;
    for(int j=0;j<f->nn;++j)if(f->nodes[j].label==f->jumpLabels[k]) {target=j;break;}
    if(target<0)return wide_reject(f,"missing-jump-table-target",__LINE__);
    f->jumpSuccessors[k]=target;
  }
  f->rejectionReason=NULL;f->rejectionLine=0;f->rejectionOrigin=NULL;
  return true;
}

/* 只提升从未取址、非易失且只有入口一次写入的寄存器形参。
 * 整个 MIR 必须只以独占对象节点访问它，不能有地址导出或间接访问。
 * 由此证明所有读取都是同一个入口值；跨调用/分支的活期仍交给原有
 * CFG 活跃分析和调用保存，不对可别名内存作缓存。可修改形参不提升。 */
static void
wide_immutable_parameter_homes(wide_function *f)
{
  if(!f->ordinaryEntry && !mcs251WideOrdinaryEnabled)return;
  bool removed[WO_MAX]={false};int map[WO_MAX],count=0;
  for(int object=0;object<f->no;++object) {
    symbol *s=f->objects[object].sym;
    if(s->addrtaken || !wide_register_parameter(s))continue;
    int initializer=-1,reads=0;bool valid=true;wide_operand input={.value=-1};
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];
      if(n->addressObject!=object)continue;
      if(n->addressSymbol!=s || n->byteAccess || n->addressOffset ||
         n->width!=wide_width(s->type) || n->floating!=IS_FLOAT(s->type) ||
         n->pointerAccess!=wide_pointer(s->type)) {valid=false;break;}
      if(n->op==WM_OBJECT_STORE) {
        if(initializer>=0 || i==0 || n->origin->op!=RECEIVE ||
           n->origin->argreg<1 || n->origin->argreg>wide_register_count(currFunc->type) ||
           f->nodes[i-1].op!=RECEIVE ||
           f->nodes[i-1].origin!=n->origin || n->right.value<0 ||
           f->nodes[i-1].result!=n->right.value) {valid=false;break;}
        initializer=i;input=n->right;
      } else if(n->op==WM_OBJECT_LOAD && initializer>=0)++reads;
      else {valid=false;break;}
    }
    if(!valid || initializer<0 || !reads)continue;
    /* 入口值只能由 RECEIVE 定义；不依赖前端符号名称或旧栈偏移。 */
    for(int i=0;i<f->nn;++i)
      if(i!=initializer-1 && wb_has(f->nodes[i].def,input.value))valid=false;
    if(!valid)continue;
    wide_node *store=&f->nodes[initializer];
    store->op=WM_NOP;store->use=store->def=(wide_bits){{0}};
    store->addressObject=-1;store->addressSymbol=NULL;store->right.value=-1;
    f->values[input.value].reads=0;f->values[input.value].weight=0;
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];
      if(n->addressObject!=object)continue;
      wassert(n->op==WM_OBJECT_LOAD);
      n->op='=';n->right=input;n->use=(wide_bits){{0}};wb_set(&n->use,input.value);
      n->addressObject=-1;n->addressSymbol=NULL;n->pointerAccess=false;
      ++f->values[input.value].reads;f->values[input.value].weight+=1+4*n->origin->depth;
    }
    removed[object]=true;++count;
    dbuf_printf(f->diagnostics,"; wide immutable parameter home removed: %s physical=%d reads=%d\n",s->name,f->objects[object].size,reads);
  }
  if(!count)return;
  int objects=0,frame=0;
  for(int i=0;i<f->no;++i) {
    if(removed[i]) {map[i]=-1;continue;}
    map[i]=objects;wide_object object=f->objects[i];object.slot=frame;
    frame+=object.size;f->objects[objects++]=object;
  }
  for(int i=0;i<f->nn;++i)if(f->nodes[i].addressObject>=0) {
    int object=map[f->nodes[i].addressObject];wassert(object>=0);
    f->nodes[i].addressObject=object;
  }
  f->no=objects;f->frame=frame;
}

static void
wide_reuse_parameter_loads(wide_function *f)
{
  if(!mcs251WideOrdinaryEnabled)return;
  int available[WV_MAX];
  for(int v=0;v<WV_MAX;++v)available[v]=-1;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];bool pure=false;
    /* 只允许跨纯值计算；调用、控制流、取址以及任何其他内存访问均
     * 清空快照。特别是未知别名写入、形参写回和 volatile/SFR 事务，
     * 不能因所读槽相同而沿用旧值。没有跨基本块或调用的内存缓存。 */
    switch(n->op) {
    case WM_PARAM_LOAD:case WM_NOP:case SEND:case '=':case CAST:
    case '+':case '-':case '*':case '^':case '|':case BITWISEAND:
    case LEFT_OP:case RIGHT_OP:case GETABIT:case GETBYTE:case GETWORD:
    case WM_UMULW:case '<':case '>':case LE_OP:case GE_OP:case EQ_OP:case NE_OP:
    case '!':case '~':pure=true;break;
    default:break;
    }
    if(!pure || n->byteAccess) {
      for(int v=0;v<f->nv;++v)available[v]=-1;
      continue;
    }
    /* 被修改的临时值不再代表原来的取样。先失效，避免把对同一 MIR
     * 目标的重新读取错误改成自复制；真实形参存储已在上方作为屏障。 */
    for(int v=0;v<f->nv;++v)if(wb_has(n->def,v))available[v]=-1;
    if(n->op!=WM_PARAM_LOAD || n->result<0)continue;
    int source=-1;
    for(int v=0;v<f->nv;++v)if(available[v]>=0) {
      wide_node *old=&f->nodes[available[v]];
      wide_value *a=&f->values[v],*b=&f->values[n->result];
      /* 同一物理槽、物理读宽及转换结果类型才可复用。保留窄有符号
       * 扩展和三字节指针补位，不将相邻槽或不同 CAST 的读取混合。 */
      if(old->addressOffset==n->addressOffset && old->left.width==n->left.width &&
         old->left.sign==n->left.sign && old->width==n->width &&
         old->sign==n->sign && old->floating==n->floating &&
         a->pointer==b->pointer && a->codePointer==b->codePointer) {source=v;break;}
    }
    if(source<0) {available[n->result]=i;continue;}
    wide_value *value=&f->values[source];
    n->op='=';n->right=(wide_operand){.value=source,.width=value->width,
      .sign=value->sign,.floating=value->floating,.codePointer=value->codePointer};
    n->left=(wide_operand){.value=-1};n->addressOffset=0;
    n->use=(wide_bits){{0}};wb_set(&n->use,source);++f->reusedParameterLoads;
  }
}

static void
wide_strength_reduce (wide_function *f)
{
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    /* 比较编译器支持函数的身份，不能把普通同名用户调用当作乘法。 */
    if(n->op!=CALL || n->args!=2 || n->width!=4 || n->result<0 || n->floating || n->left.floating || n->right.floating ||
       (n->callee!=muldiv[0][2][0] && n->callee!=muldiv[0][2][1]))continue;
    wide_operand input=n->left,constant=n->right;
    if(constant.value>=0) {constant=n->left;input=n->right;}
    if(constant.value>=0 || input.width!=4 || constant.width!=4)continue;
    signed char best[32]={0};int bestCost=100,bestTop=-1;
    for(int form=0;form<2;++form) {
      uint64_t c=constant.literal;signed char digits[32]={0};int top=-1,nonzero=0;
      /* 第二种表示使用有符号数字；第 32 位及以上按模 2^32 丢弃。 */
      for(int k=0;k<32;++k) {
        if(c&1) {
          digits[k]=form && (c&3)==3?-1:1;
          c-=digits[k];top=k;++nonzero;
        }
        c>>=1;
      }
      int cost=top<0?1:top+nonzero-1+(digits[top]<0?2:0);
      if(cost<bestCost) {bestCost=cost;bestTop=top;memcpy(best,digits,sizeof best);}
    }
    /* 限制展开长度，密集常数继续使用运行库，避免无界膨胀。 */
    if(bestCost>7)continue;
    n->op='*';n->left=input;n->right=constant;n->multiplyTop=bestTop;
    memcpy(n->multiplyDigits,best,sizeof best);
  }
}

static void
wide_remove_dead_addresses(wide_function *f)
{
  bool changed;
  do {
    wide_bits used={{0}};changed=false;
    for(int i=0;i<f->nn;++i)used=wb_or(used,f->nodes[i].use);
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];
      if(n->result>=0 && f->values[n->result].pointer && !wb_has(used,n->result) &&
         (n->op==WM_ADDRESS || n->op==CAST || n->op=='=' || n->op=='+' || n->op=='-')) {
        n->op=WM_NOP;n->use=n->def=(wide_bits){{0}};
        n->result=n->left.value=n->right.value=n->addressIndex.value=-1;changed=true;
      }
    }
  } while(changed);
}

/* 只给私有、完整标量 union 选择物理字事务。前端人工 volatile 和
 * MIR byteAccess 都不清除，因此别名屏障、访问次数与访问顺序仍由旧规则
 * 保守保护；这不是 union SROA，也不允许把真 volatile/MMIO 合并成字。 */
static bool
wide_union_guard_only(sym_link *t)
{
  return t && IS_SPEC(t) && SPEC_VOLATILE(t) && SPEC_UNION_VOLATILE_ONLY(t) &&
    !SPEC_ATOMIC(t) && !SPEC_ADDRSPACE(t) && !IS_ABSOLUTE(t);
}

static bool
wide_private_union_type(wide_object *object)
{
  symbol *s=object->sym;
  if(!s || !s->onStack || s->_isparm || !s->level || IS_STATIC(s->etype) ||
     !IS_SPEC(s->type) || !IS_STRUCT(s->type) || isVolatile(s->type) || isAtomic(s->type) ||
     SPEC_ADDRSPACE(s->etype) || IS_ABSOLUTE(s->etype) || object->size!=4)return false;
  structdef *definition=SPEC_STRUCT(s->type);
  if(!definition || definition->type!=UNION || definition->size!=4 || !definition->fields)return false;
  for(symbol *field=definition->fields;field;field=field->next) {
    sym_link *t=field->type;
    if(!wide_union_guard_only(t) || field->bitVar || IS_BITVAR(t) ||
       (!IS_INTEGRAL(t) && !IS_FLOAT(t)) || getSize(t)!=4 ||
       (IS_BITINT(t) && SPEC_BITINTWIDTH(t)!=32))return false;
  }
  return true;
}

static bool
wide_private_union_memory(wide_node *n)
{
  iCode *ic=n->origin;
  if(!ic || (n->op!=WM_LOAD && n->op!=WM_STORE) || !n->byteAccess ||
     n->width!=4 || n->codeAccess || n->pointerAccess || n->addressOp)return false;
  operand *pointer=n->op==WM_STORE?IC_RESULT(ic):IC_LEFT(ic);
  operand *value=n->op==WM_STORE?IC_RIGHT(ic):IC_RESULT(ic);
  if(!pointer || !value)return false;
  sym_link *pt=operandType(pointer),*vt=operandType(value);
  return wide_pointer(pt) && !wide_code_pointer(pt) && wide_union_guard_only(pt->next) &&
    IS_SPEC(vt) && !isAtomic(vt) &&
    (!isOperandVolatile(value,TRUE) || wide_union_guard_only(vt)) &&
    (IS_INTEGRAL(vt) || IS_FLOAT(vt)) && getSize(pt->next)==4 && getSize(vt)==4;
}

static bool
wide_union_address_copy(wide_function *f,wide_node *n,int *source)
{
  *source=-1;
  if(n->result<0 || n->result>=f->nv || !f->values[n->result].pointer ||
     f->values[n->result].symbolSnapshot || n->width!=4)return false;
  if((n->op=='=' || n->op==CAST) && n->right.width==4)
    *source=n->right.value;
  else if((n->op=='+' || n->op=='-') && n->left.width==4 &&
          n->right.value<0 && !n->right.floating)
    *source=n->left.value;
  if(*source<0 || *source>=f->nv || !f->values[*source].pointer ||
     f->values[*source].codePointer || f->values[n->result].codePointer)return false;
  sym_link *t=f->values[n->result].sym->type;
  if(!wide_pointer(t) || isAtomic(t->next) ||
     (isVolatile(t->next) && !wide_union_guard_only(t->next)))return false;
  return true;
}

static void
wide_private_union_prepare(wide_function *f)
{
  const char *enabled=getenv("MX251_PRIVATE_UNION_WORD");
  if(!enabled || strcmp(enabled,"1"))return;
  if(!currFunc || currFunc->mcs251UnionQualifiedCast) {
    dbuf_printf(f->diagnostics,"; wide private-union reject reason=source-qualified-pointer-cast\n");
    return;
  }
  uint32_t origins[WV_MAX]={0},eligible=0,rejected=0;
  for(int o=0;o<f->no;++o)if(wide_private_union_type(&f->objects[o]))eligible|=UINT32_C(1)<<o;
  if(!eligible)return;
  /* 所有地址定义与派生值取并集：多定义/回边不会把已逃逸来源洗掉。
   * 只跟踪地址，不把一次 LOAD 的标量结果误认为地址；未知用法拒绝。 */
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    if(n->op==WM_ADDRESS && n->result>=0 && n->addressObject>=0 &&
       n->addressObject<f->no && (eligible&(UINT32_C(1)<<n->addressObject)))
      origins[n->result]|=UINT32_C(1)<<n->addressObject;
  }
  bool changed;
  do {
    changed=false;
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];int source;
      if(!wide_union_address_copy(f,n,&source))continue;
      uint32_t next=origins[n->result]|origins[source];
      if(next!=origins[n->result]) {origins[n->result]=next;changed=true;}
    }
  } while(changed);
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];uint32_t used=0;int source;
    for(int v=0;v<f->nv;++v)if(wb_has(n->use,v))used|=origins[v];
    if(!used)continue;
    bool allowed=false;
    if(wide_union_address_copy(f,n,&source)) {
      allowed=true;
      for(int v=0;v<f->nv;++v)if(wb_has(n->use,v) && origins[v] && v!=source)allowed=false;
    } else if(wide_private_union_memory(n) && n->left.value>=0) {
      allowed=true;
      for(int v=0;v<f->nv;++v)if(wb_has(n->use,v) && origins[v] && v!=n->left.value)allowed=false;
    }
    if(!allowed) {
      rejected|=used;
      dbuf_printf(f->diagnostics,"; wide private-union reject node=%d reason=address-use-or-qualified-access roots=0x%lx op=%u\n",i,(unsigned long)used,n->op);
    }
  }
  for(int o=0;o<f->no;++o) {
    f->objects[o].privateUnion=(eligible&~rejected&(UINT32_C(1)<<o))!=0;
    if(eligible&(UINT32_C(1)<<o))dbuf_printf(f->diagnostics,
      "; wide private-union object=%d symbol=%s complete=4 private=%d alias-barrier=retained\n",
      o,f->objects[o].sym->name,f->objects[o].privateUnion);
  }
}

static void
wide_private_union_finish(wide_function *f)
{
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];int o=n->addressObject;
    if(o<0 || o>=f->no || !f->objects[o].privateUnion || n->addressOffset ||
       n->left.value>=0 || !wide_private_union_memory(n))continue;
    n->privateUnionWordAccess=true;
    dbuf_printf(f->diagnostics,
      "; wide private-union word-access node=%d object=%d width=4 offset=0 alias-barrier=retained\n",i,o);
  }
}

static void
wide_stack_addresses (wide_function *f)
{
  int object[WV_MAX],offset[WV_MAX];
  for(int v=0;v<WV_MAX;++v) {object[v]=-1;offset[v]=0;}
  /* 只在同一直线片段传播对象身份；不跨控制流汇合猜测来源。
   * 调用可以修改对象内容，但不能修改未取址的 MIR 指针临时值。
   * 此处不缓存内存值、不消除读写，也不缩短栈对象的存储期。 */
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];int resultObject=-1,resultOffset=0;
    if(n->op==LABEL || n->op==FUNCTION || n->op==ENDFUNCTION ||
       n->op==GOTO || n->op==IFX || n->op==JUMPTABLE || n->op==RETURN || n->op==WM_ORDERED_ASM || n->op==WM_VOLATILE_BIT_READ)
      for(int v=0;v<f->nv;++v)object[v]=-1;
    if((n->op==WM_LOAD || n->op==WM_STORE) && n->left.value>=0) {
      int v=n->left.value,o=object[v];
      if(o>=0 && offset[v]>=0 && (n->pointerAccess?3:n->width)<=f->objects[o].size-offset[v]) {
        n->addressObject=o;n->addressOffset=offset[v];
        wide_bits address={{0}};wb_set(&address,v);
        n->use=wb_minus(n->use,address);n->left.value=-1;
      }
    }
    if(n->result>=0 && f->values[n->result].pointer) {
      if(n->op==WM_ADDRESS && n->addressObject>=0) {
        resultObject=n->addressObject;resultOffset=n->addressOffset;
      } else if((n->op=='=' || n->op==CAST) && n->right.value>=0 &&
                f->values[n->right.value].pointer && n->right.width==4) {
        resultObject=object[n->right.value];resultOffset=offset[n->right.value];
      } else if((n->op=='+' || n->op=='-') && n->left.value>=0 &&
                object[n->left.value]>=0 && n->right.value<0 && !n->right.floating) {
        int o=object[n->left.value];int64_t delta=n->right.literal;
        if(n->right.sign && (n->right.literal&(UINT32_C(1)<<(8*n->right.width-1))))
          delta-=INT64_C(1)<<(8*n->right.width);
        int64_t position=offset[n->left.value]+(n->op=='+'?delta:-delta);
        /* 允许形成 one-past，只有整个实际访问都落在对象内才折叠寻址。 */
        if(position>=0 && position<=f->objects[o].size) {resultObject=o;resultOffset=(int)position;}
      }
    }
    for(int v=0;v<f->nv;++v)if(wb_has(n->def,v))object[v]=-1;
    if(n->result>=0) {object[n->result]=resultObject;offset[n->result]=resultOffset;}
  }
  /* 仅移除没有任何剩余使用的纯指针计算；调用、内存读写与入口参数保留。
   * 多次定义的值若还有任意使用，不在这里尝试局部死赋值推断。 */
  wide_remove_dead_addresses(f);
}

static void
wide_stack_indexed_bases(wide_function *f)
{
  /* 栈基址重建可减少存活指针/溢出槽，但 SPX 运算未必比保存基址快。
   * 实板速度回归未通过，因此只在明确选择体积优化时启用。 */
  if(!optimize.codeSize)return;
  int definitions[WV_MAX]={0},object[WV_MAX],offset[WV_MAX]={0};
  for(int v=0;v<f->nv;++v)object[v]=-1;
  for(int i=0;i<f->nn;++i)for(int v=0;v<f->nv;++v)
    definitions[v]+=wb_has(f->nodes[i].def,v);
  /* 只识别单一定义、未取址临时值的不可变对象身份，不推断循环 phi。
   * 地址可跨调用/分支存活，但对象内容仍可改变。任何有定义行为的读取
   * 都已执行唯一来源；未执行定义的读取本身不在有效 C 程序的保证内。
   * 从固定对象地址出发迭代，循环依赖不会自行获得已知身份。 */
  bool changed;
  do {
    changed=false;
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];int v=n->result,o=-1;int64_t position=0;
      if(v<0 || definitions[v]!=1 || object[v]>=0 || !f->values[v].pointer ||
         f->values[v].codePointer)continue;
      if(n->op==WM_ADDRESS && n->addressObject>=0 && !n->addressOp) {
        o=n->addressObject;position=n->addressOffset;
      } else if((n->op=='=' || n->op==CAST) && n->right.value>=0 &&
                n->right.width==4 && f->values[n->right.value].pointer) {
        o=object[n->right.value];position=offset[n->right.value];
      } else if((n->op=='+' || n->op=='-') && n->left.value>=0 &&
                n->right.value<0 && !n->right.floating) {
        o=object[n->left.value];int64_t delta=n->right.literal;
        if(n->right.sign && (n->right.literal&(UINT32_C(1)<<(8*n->right.width-1))))
          delta-=INT64_C(1)<<(8*n->right.width);
        position=offset[n->left.value]+(n->op=='+'?delta:-delta);
      }
      if(o>=0 && position>=0 && position<=f->objects[o].size) {
        object[v]=o;offset[v]=(int)position;changed=true;
      }
    }
  } while(changed);
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];int base=n->left.value;
    if((n->op!='+' && n->op!='-') || n->result<0 ||
       !f->values[n->result].pointer || f->values[n->result].codePointer ||
       base<0 || object[base]<0 || n->right.value<0 ||
       n->right.floating || f->values[n->right.value].pointer)continue;
    /* 仅在原运算位置重建基址，动态偏移仍按原宽度/符号扩展并清除
     * 24 位地址的填充位；不移动、合并或缓存任何内存访问。 */
    n->addressObject=object[base];n->addressOffset=offset[base];
    n->addressOp=n->op;n->addressIndex=n->right;n->op=WM_ADDRESS;
    wide_bits removed={{0}};wb_set(&removed,base);n->use=wb_minus(n->use,removed);
    n->left.value=n->right.value=-1;
  }
  wide_remove_dead_addresses(f);
}

static void
wide_fold_indexed_addresses (wide_function *f)
{
  for(int i=0;i<f->nn;++i) {
    wide_node *address=&f->nodes[i];int result=address->result,definitions=0,uses=0;
    if((address->op!='+' && address->op!='-') || result<0 ||
       !f->values[result].pointer || f->values[result].codePointer ||
       address->left.width!=4 || address->left.codePointer || address->right.floating ||
       address->left.value==result || address->right.value==result ||
       (address->right.value>=0 && f->values[address->right.value].pointer))continue;
    for(int k=0;k<f->nn;++k) {
      definitions+=wb_has(f->nodes[k].def,result);
      uses+=wb_has(f->nodes[k].use,result);
    }
    if(definitions!=1 || uses!=1)continue;
    /* 仅把纯地址运算移到同一直线片段中的唯一普通数据访问。
     * 不移动内存读写；来源若重定义或越过控制流/调用，保留原计算。
     * CODE、volatile 与直接栈对象沿用已经验证的独立访问路径。 */
    for(int j=i+1;j<f->nn;++j) {
      wide_node *n=&f->nodes[j];
      if(n->op==LABEL || n->op==GOTO || n->op==IFX || n->op==JUMPTABLE ||
         n->op==CALL || n->op==RETURN || n->op==ENDFUNCTION || n->op==FUNCTION || n->op==WM_ORDERED_ASM || n->op==WM_VOLATILE_BIT_READ)break;
      if(wb_has(n->use,result)) {
        if((n->op!=WM_LOAD && n->op!=WM_STORE) || n->left.value!=result ||
           n->right.value==result || n->addressOp || n->addressObject>=0 ||
           n->codeAccess || n->byteAccess || n->pointerAccess)break;
        n->addressOp=address->op;n->left=address->left;n->addressIndex=address->right;
        n->use=wb_or(wb_minus(n->use,address->def),address->use);
        address->op=WM_NOP;address->use=address->def=(wide_bits){{0}};
        address->result=address->left.value=address->right.value=-1;
        break;
      }
      if(wb_has(n->def,address->left.value) || wb_has(n->def,address->right.value))break;
    }
  }
}

#include "wide_indexed_field.c.inc"

static void
wide_fold_extensions (wide_function *f)
{
  for(int i=0;i<f->nn;++i) {
    wide_node *cast=&f->nodes[i];
    if(cast->op!=CAST || cast->result<0 || cast->right.value<0 ||
       cast->floating || cast->right.floating || cast->right.sign || cast->width<=cast->right.width ||
       f->values[cast->result].pointer || f->values[cast->right.value].pointer)continue;
    int result=cast->result,definitions=0,uses=0;
    for(int k=0;k<f->nn;++k) {
      wide_node *n=&f->nodes[k];definitions+=wb_has(n->def,result);
      if(wb_has(n->use,result)) {
        uses+=(n->left.value==result)+(n->right.value==result)+(n->addressIndex.value==result);
        for(unsigned k=0;k<n->stackArgs;++k)uses+=n->stackOperands[k].value==result;
      }
    }
    if(definitions!=1 || uses!=1)continue;
    /* 只把无符号零扩展并入同一直线片段的唯一算术使用。
     * 不越过控制流、调用或源值重定义；不移动真实内存/volatile访问。
     */
    for(int j=i+1;j<f->nn;++j) {
      wide_node *n=&f->nodes[j];
      if(n->op==LABEL || n->op==GOTO || n->op==IFX || n->op==JUMPTABLE || n->op==CALL ||
         n->op==RETURN || n->op==FUNCTION || n->op==ENDFUNCTION ||
         n->op==RECEIVE || n->op==WM_ORDERED_ASM || n->op==WM_VOLATILE_BIT_READ || wb_has(n->def,cast->right.value))break;
      if(!wb_has(n->use,result))continue;
      if(n->width!=cast->width || (n->op!='+' && n->op!='-' && n->op!='^' &&
         n->op!='|' && n->op!=BITWISEAND))break;
      if(n->left.value==result)n->left=cast->right;else n->right=cast->right;
      n->use=wb_minus(n->use,cast->def);wb_set(&n->use,cast->right.value);
      cast->op=WM_NOP;cast->use=cast->def=(wide_bits){{0}};
      cast->result=cast->left.value=cast->right.value=-1;++f->foldedCasts;break;
    }
  }
  /* 删除扩展节点后重算权重；死临时值不能继续影响寄存器优先次序。 */
  for(int i=0;i<f->nv;++i)f->values[i].reads=f->values[i].weight=0;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    int values[WA_MAX+2]={n->left.value,n->right.value,n->addressIndex.value};
    for(unsigned k=0;k<n->stackArgs;++k)values[k+3]=n->stackOperands[k].value;
    for(unsigned k=0;k<3+n->stackArgs;++k)if(wb_has(n->use,values[k])) {
      ++f->values[values[k]].reads;f->values[values[k]].weight+=1+4*n->origin->depth;
    }
  }
}

/* MIR 变换后的候选独立重建活跃信息与分配输入；保留 ABI pin 和对象布局。 */
static void
wide_reset_allocation(wide_function *f)
{
  /* 复制候选不能继承旧图、权重或调用保存结论；位置和 pin 重新交给
   * 原分配器，所有实际内存节点及 CFG 后继原封不动。 */
  for(int v=0;v<f->nv;++v) {
    f->values[v].edges=(wide_bits){{0}};f->values[v].reads=f->values[v].weight=0;
    f->values[v].reg=f->values[v].slot=-1;
  }
  f->frame=0;for(int v=0;v<f->no;++v)f->frame+=f->objects[v].size;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];n->in=n->out=(wide_bits){{0}};n->callSaveOmit=0;
    int values[WA_MAX+2]={n->left.value,n->right.value,n->addressIndex.value};
    for(unsigned k=0;k<n->stackArgs;++k)values[k+3]=n->stackOperands[k].value;
    for(unsigned k=0;k<3+n->stackArgs;++k)if(wb_has(n->use,values[k])) {
      ++f->values[values[k]].reads;f->values[values[k]].weight+=1+4*n->origin->depth;
    }
  }
}


#include "wide_affine.c.inc"

/* 只消除直线尾部中仅供 IFX 使用的逻辑非临时值。
 * 已取样的输入原样复用；不删读写、不跨调用/标签/内存节点，也不改变
 * 原 CFG 的目标及后继。浮点和有外部入口的片段继续保留原序列。 */
static unsigned
wide_fold_truth_branch(wide_function *f)
{
  unsigned changed=0;
  for(int i=0;i<f->nn;++i) {
    wide_node *branch=&f->nodes[i];
    if(branch->op!=IFX || branch->left.floating || branch->left.value<0 ||
       branch->orderedReadCount || branch->byteAccess)continue;
    for(int end=i;end>0;) {
      int at=end-1;
      while(at>=0 && f->nodes[at].op==WM_NOP) --at;
      if(at<0)break;
      wide_node *node=&f->nodes[at];int value=branch->left.value;
      bool negate=node->op=='!';
      if(value<0 || value>=f->nv || (!negate && node->op!=CAST && node->op!='=') ||
         node->result!=value || node->floating || node->byteAccess ||
         node->orderedReadCount || node->args || node->stackArgs)break;
      wide_operand input=negate?node->left:node->right;
      if(input.floating || (input.width!=1 && input.width!=2 && input.width!=4))break;
      if(!negate) {
        /* 截断只有在输入来自唯一逻辑非、范围已是 0/1 时才保持真假。
         * 一般整数 0x100 -> uchar 会变零，不得吞掉这种 CAST。 */
        bool canonical=false;unsigned inputDefs=0;
        for(int j=0;j<f->nn;++j)if(input.value>=0 && wb_has(f->nodes[j].def,input.value)) {
          ++inputDefs;canonical=f->nodes[j].op=='!' && !f->nodes[j].floating;
        }
        if(input.value<0 || f->values[input.value].pointer || input.codePointer ||
           (node->width<input.width && !(inputDefs==1 && canonical)))break;
      }
      wide_value *v=&f->values[value];
      if(v->symbolSnapshot || v->pin>=0 || v->pointer || v->floating)break;
      unsigned defs=0,uses=0;bool valid=true;
      for(int j=0;j<f->nn;++j) {
        defs+=wb_has(f->nodes[j].def,value);uses+=wb_has(f->nodes[j].use,value);
        /* 删除计算后，外部边不能直接进入其后缀；回边同样计入。 */
        for(unsigned k=0;k<2;++k) {
          int to=f->nodes[j].succ[k];
          if(to>at && to<=i && !(k==0 && j==to-1))valid=false;
        }
      }
      for(int j=0;j<f->nj;++j)if(f->jumpSuccessors[j]>at && f->jumpSuccessors[j]<=i)valid=false;
      for(int j=at;j<i;++j) {
        wide_node *middle=&f->nodes[j];
        if(middle->succ[0]!=j+1 || middle->succ[1]>=0 || middle->jumpCount ||
           (j!=at && middle->op!=WM_NOP))valid=false;
      }
      if(!valid || defs!=1 || uses!=1)break;
      branch->left=input;branch->width=input.width;
      if(negate)branch->trueBranch=!branch->trueBranch;
      branch->use=(wide_bits){{0}};
      if(branch->left.value>=0)wb_set(&branch->use,branch->left.value);
      node->op=WM_NOP;node->use=node->def=(wide_bits){{0}};
      node->result=node->left.value=node->right.value=node->addressIndex.value=-1;
      ++changed;end=at;
    }
  }
  if(changed)wide_reset_allocation(f);
  return changed;
}

#include "wide_union_forward.c.inc"

/* 只调度已经取得的无符号整数值，不复制表达式或移动内存取样。
 * 一次定义、一个使用节点且不跨控制流，保证计算仍恰好执行一次；
 * CALL 后重新计算值不能据此重新读取其来源对象或省略调用保存。 */
static bool
wide_sink_integer_node(wide_function *f,wide_node *n)
{
  if((n->op!='+' && n->op!='-' && n->op!='^' && n->op!='|' && n->op!=BITWISEAND) ||
     n->result<0 || n->result>=f->nv || n->floating || n->sign ||
     (n->width!=1 && n->width!=2 && n->width!=4) ||
     n->left.width!=n->width || n->right.width!=n->width ||
     n->left.floating || n->right.floating || n->left.sign || n->right.sign ||
     n->left.codePointer || n->right.codePointer || n->addressIndex.value>=0 ||
     n->args || n->stackArgs || n->addressOp || n->byteAccess || n->codeAccess || n->pointerAccess)return false;
  wide_value *result=&f->values[n->result];
  if(result->width!=n->width || result->pointer || result->codePointer ||
     result->floating || result->sign || result->symbolSnapshot || result->pin>=0)return false;
  int inputs[2]={n->left.value,n->right.value};
  for(int k=0;k<2;++k)if(inputs[k]>=0) {
    if(inputs[k]>=f->nv || inputs[k]==n->result)return false;
    wide_value *value=&f->values[inputs[k]];
    if(value->width!=n->width || value->pointer || value->codePointer || value->floating || value->sign)return false;
  }
  for(int v=0;v<f->nv;++v)
    if(wb_has(n->def,v)!=(v==n->result) ||
       wb_has(n->use,v)!=(v==inputs[0] || v==inputs[1]))return false;
  return true;
}

static bool
wide_sink_block_barrier(wide_node *n)
{
  return n->op==FUNCTION || n->op==ENDFUNCTION || n->op==LABEL ||
    n->op==RECEIVE || n->op==RETURN || n->op==IFX || n->op==GOTO || n->op==JUMPTABLE || n->op==WM_ORDERED_ASM || n->op==WM_VOLATILE_BIT_READ;
}

static bool
wide_sink_fallthrough(wide_node *n,int index)
{
  return n->op!=RETURN && n->op!=ENDFUNCTION && n->op!=GOTO && n->op!=JUMPTABLE && n->succ[0]==index+1;
}

/* 移动的是节点身份；顺序边必须按新位置重新连接，显式跳转则按身份
 * 映射。只映射旧直落边会跳过被移动的计算，甚至构造错误的循环。 */
static void
wide_sink_move_before(wide_function *f,int from,int before)
{
  int mapped[WN_MAX],original[WN_MAX];
  for(int i=0;i<f->nn;++i)mapped[i]=i;
  mapped[from]=before-1;
  for(int i=from+1;i<before;++i)mapped[i]=i-1;
  for(int i=0;i<f->nn;++i)original[mapped[i]]=i;
  wide_node moving=f->nodes[from];
  memmove(&f->nodes[from],&f->nodes[from+1],(before-from-1)*sizeof f->nodes[0]);
  f->nodes[before-1]=moving;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];bool fall=wide_sink_fallthrough(n,original[i]);
    for(int k=0;k<2;++k)if(n->succ[k]>=0)
      n->succ[k]=(k==0 && fall)?i+1:mapped[n->succ[k]];
  }
  for(int i=0;i<f->nj;++i)f->jumpSuccessors[i]=mapped[f->jumpSuccessors[i]];
}

static unsigned
wide_sink_integer_expressions(wide_function *f)
{
  unsigned moved=0;
  /* 每个值最多考察一次，顺序固定；不通过复制节点增加代码或扩大搜索。 */
  for(int value=0;value<f->nv;++value) {
    int definition=-1,use=-1,definitions=0,uses=0;
    for(int i=0;i<f->nn;++i) {
      if(wb_has(f->nodes[i].def,value)) {definition=i;++definitions;}
      if(wb_has(f->nodes[i].use,value)) {use=i;++uses;}
    }
    if(definitions!=1 || uses!=1 || use<=definition+1 ||
       !wide_sink_integer_node(f,&f->nodes[definition]))continue;
    wide_node *node=&f->nodes[definition];
    int inputs[2]={node->left.value,node->right.value};unsigned calls=0;bool valid=true;
    for(int i=definition;i<use && valid;++i) {
      wide_node *between=&f->nodes[i];
      if(wide_sink_block_barrier(between) || between->succ[0]!=i+1 ||
         between->succ[1]>=0 || between->jumpCount) {valid=false;break;}
      if(i==definition)continue;
      if(wb_has(between->def,inputs[0]) || wb_has(between->def,inputs[1])) {valid=false;break;}
      calls+=between->op==CALL;
    }
    if(!valid || !calls)continue;
    /* 正常导入的显式边落在 LABEL；这里仍独立检查全部入边，不能把
     * 单一后继误当作单一入口。跳表重复边和回边同样不能越过。 */
    for(int i=0;i<f->nn && valid;++i)for(int k=0;k<2;++k) {
      int to=f->nodes[i].succ[k];
      if(to>=definition && to<=use && !(k==0 && wide_sink_fallthrough(&f->nodes[i],i)))valid=false;
    }
    for(int i=0;i<f->nj;++i)if(f->jumpSuccessors[i]>=definition && f->jumpSuccessors[i]<=use)valid=false;
    if(!valid)continue;
    wide_sink_move_before(f,definition,use);++moved;
    dbuf_printf(f->diagnostics,"; wide integer-sink moved value=%d from=%d before=%d calls=%u\n",value,definition,use,calls);
  }
  if(moved)wide_reset_allocation(f);
  return moved;
}

#include "wide_load_schedule.c.inc"

/* 只合并入口直线前缀中的同地址空间数据指针副本。入口 RECEIVE 的
 * 规范化位模式保持不变；不合并指向的内存，不移动任何读取或写入。 */
static unsigned
wide_entry_pointer_copies(wide_function *f)
{
  unsigned copies=0;
  for(int i=0;i<f->nn;++i) {
    wide_node *copy=&f->nodes[i];
    /* 前缀每个节点只有下一节点一个后继，因此任一后续可达使用必先
     * 经过本节点；不以源代码顺序冒充跨分支的支配关系。 */
    if(copy->succ[0]!=i+1 || copy->succ[1]>=0 || copy->jumpCount)break;
    if((copy->op!='=' && copy->op!=CAST) || copy->result<0 || copy->right.value<0)continue;
    int dst=copy->result,src=copy->right.value;
    if(dst==src)continue;
    wide_value *a=&f->values[src],*b=&f->values[dst];
    if(!a->pointer || !b->pointer || a->codePointer || b->codePointer ||
       a->width!=4 || b->width!=4 || copy->width!=4 || copy->right.width!=4 ||
       a->floating || b->floating || a->sign || b->sign ||
       a->symbolSnapshot || b->symbolSnapshot || b->pin>=0 ||
       !IS_PTR(a->sym->type) || !IS_PTR(b->sym->type) ||
       DCL_TYPE(a->sym->type)!=DCL_TYPE(b->sym->type) ||
       getSize(a->sym->type)!=3 || getSize(b->sym->type)!=3 ||
       isVolatile(a->sym->type) || isVolatile(b->sym->type) ||
       isAtomic(a->sym->type) || isAtomic(b->sym->type))continue;
    int sourceDef=-1,sourceDefs=0,destinationDefs=0;bool valid=true;unsigned uses=0;
    for(int j=0;j<f->nn;++j) {
      wide_node *n=&f->nodes[j];
      if(wb_has(n->def,src)) {sourceDef=j;++sourceDefs;}
      if(wb_has(n->def,dst))++destinationDefs;
      if(!wb_has(n->use,dst))continue;
      /* CALL 的全部寄存器和栈实参都在这些操作数中；未知载体不猜测。 */
      bool represented=n->left.value==dst || n->right.value==dst || n->addressIndex.value==dst;
      for(unsigned k=0;k<n->stackArgs;++k)represented|=n->stackOperands[k].value==dst;
      if(j<=i || !represented)valid=false;
      ++uses;
    }
    if(!valid || !uses || sourceDefs!=1 || destinationDefs!=1 || sourceDef<0 || sourceDef>=i ||
       f->nodes[sourceDef].op!=RECEIVE)continue;
    wide_bits destination={{0}};wb_set(&destination,dst);
    for(int j=0;j<f->nn;++j) {
      wide_node *n=&f->nodes[j];
      if(!wb_has(n->use,dst))continue;
      if(n->left.value==dst)n->left.value=src;
      if(n->right.value==dst)n->right.value=src;
      if(n->addressIndex.value==dst)n->addressIndex.value=src;
      for(unsigned k=0;k<n->stackArgs;++k)if(n->stackOperands[k].value==dst)n->stackOperands[k].value=src;
      n->use=wb_minus(n->use,destination);wb_set(&n->use,src);
    }
    copy->op=WM_NOP;copy->result=copy->left.value=copy->right.value=copy->addressIndex.value=-1;
    copy->use=copy->def=(wide_bits){{0}};
    dbuf_printf(f->diagnostics,"; wide pointer-copy propagated function=%s node=%d source=%d destination=%d uses=%u\n",currFunc->name,i,src,dst,uses);
    ++copies;
  }
  if(copies) {
    wide_reset_allocation(f);
  }
  return copies;
}

/* 单定义对象地址的支配缓存只描述值来源，不描述或缓存对象内容。
 * 使用真实 CFG（包括 switch 全部表项）：移除定义节点后，仍可从入口
 * 到达的节点不受该定义支配。不可达节点不作任何优化推断。 */
typedef struct {
  int entry,definitions[WV_MAX],definition[WV_MAX];
  unsigned char reachable[WN_MAX],ready[WV_MAX],without[WV_MAX][WN_MAX];
} wide_address_dominance;

static void
wide_address_reachable(wide_function *f,int entry,int omitted,unsigned char *seen)
{
  int queue[WN_MAX],head=0,tail=0;
  memset(seen,0,WN_MAX);
  if(entry<0 || entry==omitted)return;
  seen[entry]=1;queue[tail++]=entry;
  while(head<tail) {
    wide_node *n=&f->nodes[queue[head++]];
    for(int k=0;k<2+n->jumpCount;++k) {
      int next=k<2?n->succ[k]:f->jumpSuccessors[n->jumpStart+k-2];
      if(next<0 || next==omitted)continue;
      wassert(next<f->nn);if(seen[next])continue;
      wassert(tail<WN_MAX);seen[next]=1;queue[tail++]=next;
    }
  }
}

static bool
wide_address_dominates(wide_function *f,wide_address_dominance *d,int value,int use)
{
  if(value<0 || d->definitions[value]!=1 || use<0 || !d->reachable[use])return false;
  int definition=d->definition[value];
  if(definition==use || !d->reachable[definition])return false;
  if(!d->ready[value]) {
    wide_address_reachable(f,d->entry,definition,d->without[value]);d->ready[value]=1;
  }
  return !d->without[value][use];
}

#include "wide_rotate.c.inc"

static bool
wide_fixed_address_value(wide_value *v)
{
  return v->pointer && !v->codePointer && v->width==4 && !v->floating && !v->sign &&
    !v->symbolSnapshot && IS_PTR(v->sym->type) && getSize(v->sym->type)==3 &&
    !isVolatile(v->sym->type) && !isAtomic(v->sym->type);
}

static bool
wide_fixed_address_delta(wide_operand o,int64_t *delta)
{
  if(o.value>=0 || o.floating || o.codePointer ||
     (o.width!=1 && o.width!=2 && o.width!=4))return false;
  uint32_t value=o.literal;
  if(o.sign && o.width<4 && (value&(UINT32_C(1)<<(8*o.width-1))))
    value|=~((UINT32_C(1)<<(8*o.width))-1);
  *delta=value;
  if(o.sign && (value&UINT32_C(0x80000000)))*delta-=INT64_C(1)<<32;
  return true;
}

static unsigned
wide_fixed_stack_addresses(wide_function *f)
{
  if(!f->no)return 0;
  wide_address_dominance *d=Safe_alloc(sizeof *d);memset(d,0,sizeof *d);d->entry=-1;
  int object[WV_MAX],offset[WV_MAX]={0};unsigned changedUses=0;
  for(int v=0;v<f->nv;++v) {object[v]=-1;d->definition[v]=-1;}
  for(int i=0;i<f->nn;++i) {
    if(f->nodes[i].op==FUNCTION) {if(d->entry>=0) {Safe_free(d);return 0;}d->entry=i;}
    for(int v=0;v<f->nv;++v)if(wb_has(f->nodes[i].def,v)) {
      ++d->definitions[v];d->definition[v]=i;
    }
  }
  if(d->entry<0) {Safe_free(d);return 0;}
  wide_address_reachable(f,d->entry,-1,d->reachable);
  /* 只从本地完整对象生长身份，不推断 phi、动态偏移或循环携带值。
   * 派生值的来源定义必须支配派生点，不能以源码排列顺序代替。 */
  bool changed;
  do {
    changed=false;
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];int v=n->result,source=-1,o=-1;int64_t position=0;
      if(v<0 || !d->reachable[i] || d->definitions[v]!=1 || d->definition[v]!=i ||
         object[v]>=0 || !wide_fixed_address_value(&f->values[v]))continue;
      if(n->op==WM_ADDRESS && n->addressObject>=0 && !n->addressOp) {
        o=n->addressObject;position=n->addressOffset;
        if(o>=f->no || n->addressSymbol!=f->objects[o].sym)continue;
      } else {
        int64_t delta=0;
        if((n->op=='=' || n->op==CAST) && n->width==4 && n->right.width==4)
          source=n->right.value;
        else if((n->op=='+' || n->op=='-') && n->width==4 && n->left.width==4 &&
                wide_fixed_address_delta(n->right,&delta)) {
          source=n->left.value;if(n->op=='-')delta=-delta;
        } else continue;
        if(source<0 || source==v || object[source]<0 ||
           !wide_fixed_address_value(&f->values[source]) ||
           DCL_TYPE(f->values[source].sym->type)!=DCL_TYPE(f->values[v].sym->type) ||
           !wide_address_dominates(f,d,source,i))continue;
        o=object[source];position=offset[source]+delta;
      }
      if(o>=0 && position>=0 && position<=f->objects[o].size) {
        object[v]=o;offset[v]=(int)position;changed=true;
      }
    }
  } while(changed);
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];int source=n->left.value;
    if((n->op!=WM_LOAD && n->op!=WM_STORE) || n->addressObject>=0 || n->codeAccess ||
       source<0 || n->left.width!=4 || object[source]<0 || !wide_address_dominates(f,d,source,i))continue;
    int o=object[source];int64_t position=offset[source],delta=0;
    if(n->addressOp) {
      if((n->addressOp!='+' && n->addressOp!='-') ||
         !wide_fixed_address_delta(n->addressIndex,&delta))continue;
      position+=n->addressOp=='+'?delta:-delta;
    }
    int physical=n->pointerAccess?3:n->width;
    if(physical<1 || position<0 || position>f->objects[o].size ||
       physical>f->objects[o].size-position)continue;
    /* 留在原节点的字节/字/三字节指针事务保持原序、原宽度；不跨越
     * CALL 缓存内容。自存指针仍是右值，不能清除其真正的读取。 */
    n->addressObject=o;n->addressOffset=(int)position;n->addressSymbol=f->objects[o].sym;
    n->left.value=-1;n->addressOp=0;n->addressIndex=(wide_operand){.value=-1};
    if(n->right.value!=source) {wide_bits removed={{0}};wb_set(&removed,source);n->use=wb_minus(n->use,removed);}
    dbuf_printf(f->diagnostics,"; wide stack-address folded function=%s node=%d source=%d definition=%d object=%d offset=%d physical=%d volatile-bytes=%d\n",
      currFunc->name,i,source,d->definition[source],o,(int)position,physical,n->byteAccess);
    ++changedUses;
  }
  Safe_free(d);
  if(changedUses) {wide_remove_dead_addresses(f);wide_reset_allocation(f);}
  return changedUses;
}

static void
wide_liveness (wide_function *f)
{
  bool changed;
  do {
    changed=false;
    for(int i=f->nn;i-->0;) {
      wide_node *n=&f->nodes[i];wide_bits out={{0}},in;
      for(int k=0;k<2;++k) if(n->succ[k]>=0) out=wb_or(out,f->nodes[n->succ[k]].in);
      for(int k=0;k<n->jumpCount;++k)
        out=wb_or(out,f->nodes[f->jumpSuccessors[n->jumpStart+k]].in);
      in=wb_or(n->use,wb_minus(out,n->def));
      changed |= !wb_equal(n->in,in) || !wb_equal(n->out,out);n->in=in;n->out=out;
    }
  } while(changed);
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    for(int a=0;a<f->nv;++a) for(int b=0;b<a;++b) {
      bool conflict=(wb_has(n->in,a)&&wb_has(n->in,b)) ||
        (wb_has(n->def,a)&&wb_has(n->out,b)) || (wb_has(n->def,b)&&wb_has(n->out,a));
      if(conflict) {wb_set(&f->values[a].edges,b);wb_set(&f->values[b].edges,a);}
    }
  }
}

static uint32_t wide_mask (int reg,int width) {return mcs251RegisterBytes((unsigned)reg,(unsigned)width);}
static bool
wide_available (wide_function *f,int value,int reg)
{
  wide_value *v=&f->values[value];uint32_t mask=wide_mask(reg,v->width);
  for(int i=0;i<f->nv;++i) if(wb_has(v->edges,i) && f->values[i].reg>=0 && (mask&wide_mask(f->values[i].reg,f->values[i].width))) return false;
  return true;
}

static bool
wide_has_leaf_call (wide_function *f)
{
  for(int i=0;i<f->nn;++i) {
    uint32_t writes;
    if(f->nodes[i].op==CALL && wide_leaf_writes(f->nodes[i].callee,&writes))return true;
  }
  return false;
}

static unsigned
wide_call_register_cost (wide_function *f,int value,int reg)
{
  unsigned cost=0;uint32_t mask=wide_mask(reg,f->values[value].width);
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];uint32_t writes;
    if(n->op!=CALL || n->result==value || !wb_has(n->out,value) ||
       !wide_leaf_writes(n->callee,&writes))continue;
    /* 这里只引导寄存器选择，不据此删除保存。参数和结果的真实写集合仍由
     * 发射器重新核验；包括 A/R11、B/R10 及并行实参复制的保留暂存区。 */
    writes|=MCS251_AB_BYTES|wide_mask(16,4)|wide_mask(20,4);
    if(!n->ordinaryCall) {
      if(n->args)writes|=wide_mask(8-n->left.width,n->left.width);
      if(n->args>1)writes|=wide_mask(16-n->right.width,n->right.width);
      if(n->result>=0)writes|=wide_mask(n->width==1?6:8-n->width,n->width);
    }
    if(mask&writes)cost+=1+4*n->origin->depth;
  }
  return cost;
}

static const int *
wide_register_choices(int width,int *count)
{
  static const int dwords[]={4,12,24,28,0};
  static const int words[]={6,14,24,26,28,30,2,0,4,12,8};
  static const int bytes[]={9,8,7,15,3,2,1,0,6,5,4,14,13,12};
  *count=width==4?5:width==2?11:14;
  return width==4?dwords:width==2?words:bytes;
}

static void
wide_layout_spills(wide_function *f)
{
  {
    int objects=0;
    for(int i=0;i<f->no;++i)objects+=f->objects[i].size;
    f->frame=objects;
    /* 对象不可复用；只有未取址的 MIR 溢出值可依冲突图共享字节槽。
     * 按完整宽度检查所有相交区域，支持不同宽度但不依赖同起点。 */
    for(int i=0;i<f->nv;++i)if(f->values[i].reg<0) {
      wide_value *v=&f->values[i];int slot=objects;
      for(;;++slot) {
        bool available=true;
        for(int j=0;j<i;++j) {
          wide_value *other=&f->values[j];
          if(other->reg<0 && wb_has(v->edges,j) && slot<other->slot+other->width && other->slot<slot+v->width) {available=false;break;}
        }
        if(available)break;
      }
      v->slot=slot;if(slot+v->width>f->frame)f->frame=slot+v->width;
    }
  }
}

static bool
wide_validate_allocation(wide_function *f)
{

  /* 分配完成后的自洽性失败是编译器错误，不能假装为语言边界而回退。
   * 在掩码比较之前校验编码范围，避免无效切片的零掩码隐去冲突。 */
  if(WA_FAIL("internal-conflict") && f->nv)f->values[0].reg=33;
  for(int i=0;i<f->nv;++i) if(f->values[i].reg>=0 &&
    (!mcs251RegisterEncodable(f->values[i].reg,f->values[i].width) ||
     !wide_available(f,i,f->values[i].reg))) {f->allocationInvalid=true;return false;}
  for(int i=0;i<f->nv;++i)if(f->values[i].reg<0) {
    wide_value *a=&f->values[i];
    if(a->slot<0) {f->allocationInvalid=true;return false;}
    for(int j=0;j<f->no;++j)if(a->slot<f->objects[j].slot+f->objects[j].size && f->objects[j].slot<a->slot+a->width) {f->allocationInvalid=true;return false;}
    for(int j=0;j<i;++j) {
      wide_value *b=&f->values[j];
      if(b->reg<0 && wb_has(a->edges,j) && a->slot<b->slot+b->width && b->slot<a->slot+a->width) {f->allocationInvalid=true;return false;}
    }
  }
  return f->frame<=512;
}

static bool
wide_allocate (wide_function *f, int strategy, bool callAware)
{
  for(int i=0;i<f->nv;++i) if(f->values[i].pin>=0) {
    if(!wide_available(f,i,f->values[i].pin)) return false;
    f->values[i].reg=f->values[i].pin;
  }
  for(int done=0;done<f->nv;++done) {
    int best=-1;
    unsigned bestScore=0;
    for(int i=0;i<f->nv;++i) if(f->values[i].reg<0 && f->values[i].slot<0) {
      unsigned degree=0,score=f->values[i].weight;
      if(strategy>=2) {
        for(int j=0;j<f->nv;++j)if(wb_has(f->values[i].edges,j))degree+=f->values[j].width;
        /* 长期活值的冲突范围大，不能只凭读取次数占住所有宽寄存器。
         * 两种固定次序用于候选比较，不进行随机或无界搜索。 */
        score=1024*(score+(strategy==3))/(1+degree);
      }
      if(best<0 || score>bestScore) {best=i;bestScore=score;}
    }
    if(best<0) break;
    wide_value *v=&f->values[best];int count;const int *choices=wide_register_choices(v->width,&count);
    if(callAware) {
      unsigned bestCost=0;
      /* 原返回偏好作为同成本时的首选。只在已绑定的叶调用上比较完整
       * 物理切片，不拆宽值、不改冲突图和 ABI 固定参数寄存器。 */
      for(int j=-1;j<count;++j) {
        int reg=j<0?v->preference:choices[j];
        if(reg<0 || !wide_available(f,best,reg))continue;
        unsigned cost=wide_call_register_cost(f,best,reg);
        if(v->reg<0 || cost<bestCost) {v->reg=reg;bestCost=cost;}
      }
    } else {
      if(v->preference>=0 && wide_available(f,best,v->preference)) v->reg=v->preference;
      for(int j=0;j<count && v->reg<0;++j) if(wide_available(f,best,choices[j])) v->reg=choices[j];
    }
    if(v->reg<0) {v->slot=f->frame;f->frame+=v->width;}
  }
  if(strategy)wide_layout_spills(f);
  return wide_validate_allocation(f);
}

static int wide_repeat_mark(wide_function *f,int first);
static bool wide_wordmul_cost(const char *,size_t,const char *,size_t,unsigned *,unsigned *);
#include "wide_emit.c.inc"
#include "wide_cost.h"
#include "wide_loop_cost.h"
#include "wide_wordmul_cost.c.inc"

/* 只使用已通过 MIR 准入的内存节点，不查猜测性的全局名称表。
 * extern direct 对象也有存储类型；CODE/XDATA、栈对象及越界绝对地址
 * 不得冒充 direct8。候选复制后符号仍为同一编译单元的只读身份。 */
static wide_cost_operand
wide_resolve_cost_operand(const char *text,const void *context)
{
  const wide_function *f=context;
  wide_cost_operand unknown={WC_UNKNOWN,0,0};
  for(int i=0;i<f->nn;++i) {
    const wide_node *n=&f->nodes[i];symbol *s=n->addressSymbol;
    if(n->op==WM_VOLATILE_BIT_READ)for(unsigned j=0;j<n->orderedReadCount;++j) {
      symbol *read=n->orderedReadSymbols[j];
      if(!read || !IN_BITSPACE(SPEC_OCLS(read->etype)))continue;
      if(SPEC_ABSA(read->etype) && (SPEC_ADDR(read->etype)<0 || SPEC_ADDR(read->etype)>255))continue;
      if(!strcmp(text,read->rname))return (wide_cost_operand){WC_BIT,0,0};
    }
    if(n->op==WM_ORDERED_ASM)for(unsigned j=0;j<n->orderedAsmCount;++j) {
      const wide_asm_instruction *instruction=&n->orderedAsm[j];char spelling[16];
      if(instruction->operation==1)continue;
      SNPRINTF(spelling,sizeof spelling,"0x%02x.%u",instruction->address,instruction->bit);
      /* as251 将八对齐 SFR 的 byte.bit 编为 classic 两字节；其余
       * 数值 SFR 位使用三字节 A9 扩展，不能把两种形式合并计价。 */
      if(!strcmp(text,spelling))return (wide_cost_operand){instruction->address&7?WC_EXT_BIT:WC_BIT,0,0};
    }
    bool bit=n->op==WM_BIT_LOAD || n->op==WM_BIT_STORE || n->op==WM_BIT_CAST;
    if(!s || (!bit && n->op!=WM_SYMBOL_LOAD && n->op!=WM_SYMBOL_STORE))continue;
    memmap *m=SPEC_OCLS(s->etype);
    if(!m || s->onStack || s->_isparm)continue;
    if(bit) {if(!IN_BITSPACE(m))continue;}
    else if(!IN_DIRSPACE(m) || IN_BITSPACE(m) || IN_CODESPACE(m) ||
      IN_FARSPACE(m) || IN_PAGEDSPACE(m) || (IN_REGSP(m) && m!=sfr))continue;
    int offset=wide_cost_symbol_offset(text,s->rname,bit?1:(unsigned)getSize(s->type));
    if(offset<0)continue;
    if(SPEC_ABSA(s->etype) && (SPEC_ADDR(s->etype)<0 || SPEC_ADDR(s->etype)>255-offset))continue;
    return (wide_cost_operand){bit?WC_BIT:WC_DIRECT,0,0};
  }
  return unknown;
}

static wide_byte_cost
wide_emitted_bytes(const wide_function *f,const char *text,size_t length)
{
  return wide_function_bytes_context(text,length,wide_resolve_cost_operand,f);
}

typedef struct { unsigned bytes,unknown,heuristic,instructions,spills;int frame; } wide_selection_cost;

typedef struct {
  char *text;size_t length;unsigned heuristic,instructions,spills;int frame;wide_byte_cost size;
} wide_allocation_candidate;

/* 接收私有分配副本，所有方案使用同一发射器、调用保存和完整编码成本。 */
static void
wide_emit_candidate(wide_function *f,wide_function *trial,wide_allocation_candidate *c)
{
      wide_function *reemit=NULL;
      if(wideLeafEffects) {reemit=Safe_alloc(sizeof *reemit);memcpy(reemit,trial,sizeof *reemit);}
      trial->emitWeight=1;dbuf_init(&trial->output,1024);wide_emit(trial);
      if(reemit) {
        bool useful=false;
        for(int i=0;i<trial->nn;++i) {reemit->nodes[i].callSaveOmit=trial->nodes[i].callSaveOmit;useful|=trial->nodes[i].callSaveOmit!=0;}
        if(useful) {
          /* 从相同分配结果重新发射，所有 SPX 位移依据新的 stackDepth 计算。
           * 第二遍只要实际参数/结果影响触及省略值，就丢弃整个候选。 */
          reemit->applyCallEffects=true;reemit->emitWeight=1;dbuf_init(&reemit->output,1024);wide_emit(reemit);
          wide_byte_cost oldCost=wide_emitted_bytes(trial,dbuf_get_buf(&trial->output),dbuf_get_length(&trial->output));
          wide_byte_cost newCost=wide_emitted_bytes(reemit,dbuf_get_buf(&reemit->output),dbuf_get_length(&reemit->output));
          bool cheaper=!oldCost.unknown && !newCost.unknown && newCost.bytes<=oldCost.bytes && reemit->emitCost<=trial->emitCost;
          if(!reemit->callEffectsInvalid && cheaper) {
            dbuf_destroy(&trial->output);Safe_free(trial);trial=reemit;reemit=NULL;
          } else {
            dbuf_printf(f->diagnostics,"; wide call-save candidate rejected: function=%s effect-conflict=%u cost=%u\n",currFunc->name,reemit->callEffectsInvalid,!cheaper);
            dbuf_destroy(&reemit->output);
          }
        }
        Safe_free(reemit);
      }
      c->length=dbuf_get_length(&trial->output);
      c->text=Safe_alloc(c->length);
      memcpy(c->text,dbuf_get_buf(&trial->output),c->length);
      c->heuristic=trial->emitCost;c->instructions=trial->emitInstructions;c->frame=trial->frame;
      for(int v=0;v<trial->nv;++v)if(trial->values[v].reg<0)++c->spills;
      c->size=wide_emitted_bytes(trial,c->text,c->length);
      dbuf_destroy(&trial->output);
      Safe_free(trial);
}

#include "wide_recolor.c.inc"

static mcs251_wide_result
wide_choose_allocation(wide_function *f,wide_selection_cost *cost)
{
  wide_allocation_candidate c[8]={{0}};
  int selected=-1;bool allKnown=true,invalid=false;unsigned bestCost=0;
  /* 不扩大候选准入范围。size在所有成功候选均可定长时比较最终编码字节；
   * 任一候选含未知形式，则整组保留原指令数策略，不能奖励未知成本。
   * speed仍为循环深度加权启发式，不声称等于芯片周期。 */
  bool callAware=wide_has_leaf_call(f);
  int baseCount=1,count=callAware?2:1;
  for(int strategy=0;strategy<count;++strategy) {
    wide_function *trial=Safe_alloc(sizeof *trial);memcpy(trial,f,sizeof *trial);
    bool allocated=wide_allocate(trial,strategy%baseCount,strategy>=baseCount);
    if(trial->allocationInvalid) {invalid=true;Safe_free(trial);break;}
    if(allocated && WA_FAIL("after-allocate"))allocated=false;
    /* 只存在虚拟值 spill 的函数同样需要已实现的槽复用候选。
     * 不增加搜索策略数量；新范围仍须经过完整字节/路径成本门槛。 */
    if(!strategy) {
      for(int i=0;i<trial->nv;++i)if(trial->values[i].reg<0) {baseCount=4;count=callAware?8:4;break;}
    }
    if(allocated) {
      wide_emit_candidate(f,trial,&c[strategy]);trial=NULL;
      if(strategy<baseCount && c[strategy].size.unknown)allKnown=false;
    }
    Safe_free(trial);
  }
  int legacyBase=-1;
  if(!f->no && !invalid) {
    bool legacyKnown=!c[0].size.unknown;
    unsigned legacyBest=0;
    for(int k=0;k<(callAware?2:1);++k) {
      int i=k?baseCount:0;
      if(!c[i].text)continue;
      if(k && (legacyBase<0 || c[i].size.unknown || c[legacyBase].size.unknown ||
        c[i].size.bytes>c[legacyBase].size.bytes || c[i].heuristic>c[legacyBase].heuristic ||
        c[i].frame>c[legacyBase].frame))continue;
      unsigned objective=optimize.codeSize?(legacyKnown?c[i].size.bytes:c[i].instructions):c[i].heuristic;
      if(legacyBase<0 || objective<legacyBest ||
        (objective==legacyBest && c[i].frame<c[legacyBase].frame)) {legacyBase=i;legacyBest=objective;}
    }
    selected=legacyBase;bestCost=legacyBest;allKnown=legacyKnown;
  }
  for(int i=0;!invalid && i<count;++i)if(c[i].text) {
    /* 先按原策略选出基线。新方案必须同时不增加确定编码字节、加权
     * 指令代价及栈帧；未知成本不准入。静态代价不能代替实板周期。 */
    if(f->no && i>=baseCount && (selected<0 || c[i].size.unknown || c[selected].size.unknown ||
       c[i].size.bytes>c[selected].size.bytes || c[i].heuristic>c[selected].heuristic ||
       c[i].frame>c[selected].frame))continue;
    /* 资格总是对冻结的旧搜索赢家；中途最小栈帧不能成为新硬门槛。
     * 旧 call-aware 候选同样参与原基线选择，不用新候选覆盖旧更优解。 */
    if(!f->no) {
      if(i==0 || i==baseCount)continue;
      if(legacyBase<0)continue;
      int decision=-1;
      bool staticBetter=!c[i].size.unknown && !c[legacyBase].size.unknown &&
        c[i].size.bytes<=c[legacyBase].size.bytes && c[i].heuristic<=c[legacyBase].heuristic &&
        c[i].frame<=c[legacyBase].frame;
      if(staticBetter)decision=wide_loop_cost_compare_context(c[legacyBase].text,c[legacyBase].length,c[i].text,c[i].length,wide_resolve_cost_operand,f);
      dbuf_printf(f->diagnostics,"; wide spill-candidate id=%d base=%d known=%u loop-decision=%d old-bytes=%u new-bytes=%u old-frame=%d new-frame=%d\n",
        i,legacyBase,staticBetter,decision,c[legacyBase].size.bytes,c[i].size.bytes,c[legacyBase].frame,c[i].frame);
      if(!staticBetter || decision!=1)continue;
    }
    unsigned cost=optimize.codeSize?(allKnown?c[i].size.bytes:c[i].instructions):c[i].heuristic;
    if(selected<0 || cost<bestCost || (cost==bestCost && c[i].frame<c[selected].frame)) {selected=i;bestCost=cost;}
  }
  if(selected>=0 && !invalid) {
    wide_function *allocation=Safe_alloc(sizeof *allocation);memcpy(allocation,f,sizeof *allocation);
    if(wide_allocate(allocation,selected%baseCount,selected>=baseCount)) {
      wide_choose_recolor(f,allocation,&c[selected]);
      wide_choose_byte_recolor(f,allocation,&c[selected]);
    }
    if(allocation->allocationInvalid)invalid=true;
    Safe_free(allocation);
  }
  /* 观察现有候选，不改变准入或选择策略。完整发射文本作指纹，方便识别重复方案。 */
  if(selected>=0 && !invalid) {
    int instructionWinner=-1;unsigned instructionBest=0;
    for(int i=0;i<count;++i)if(c[i].text) {
      if(instructionWinner<0 || c[i].instructions<instructionBest ||
         (c[i].instructions==instructionBest && c[i].frame<c[instructionWinner].frame)) {
        instructionWinner=i;instructionBest=c[i].instructions;
      }
    }
    for(int i=0;i<count;++i) {
      dbuf_printf(f->diagnostics,"; wide candidate-cost function=%s id=%d heuristic=%u spills=%u base=%d original=%d\n",
        currFunc->name,i,c[i].heuristic,c[i].spills,baseCount,count);
      uint32_t fingerprint=UINT32_C(2166136261);
      for(size_t j=0;j<c[i].length;++j)fingerprint=(fingerprint^(unsigned char)c[i].text[j])*UINT32_C(16777619);
      dbuf_printf(f->diagnostics,"; wide candidate function=%s id=%d success=%d instructions=%u bytes=%u unknown=%u frame=%d text-fnv=%08lx instruction-winner=%d selected=%d\n",
        currFunc->name,i,c[i].text!=NULL,c[i].instructions,c[i].size.bytes,c[i].size.unknown,c[i].frame,(unsigned long)fingerprint,instructionWinner,selected);
    }
    dbuf_printf(f->diagnostics,"; wide allocation selected=%d candidates=%d heuristic=%u instructions=%u frame=%d\n",selected,count,c[selected].heuristic,c[selected].instructions,c[selected].frame);
    dbuf_printf(f->diagnostics,"; wide cost objective=%s known-bytes=%u unknown=%u group-known=%d\n",optimize.codeSize?(allKnown?"encoded-bytes":"instruction-fallback"):"weighted-instructions",c[selected].size.bytes,c[selected].size.unknown,allKnown);
    if(cost)*cost=(wide_selection_cost){c[selected].size.bytes,c[selected].size.unknown,c[selected].heuristic,c[selected].instructions,c[selected].spills,c[selected].frame};
    dbuf_append(f->candidateOutput,c[selected].text,c[selected].length);
  }
  for(int i=0;i<count;++i)Safe_free(c[i].text);
  return invalid?MCS251_WIDE_ERROR:selected>=0?MCS251_WIDE_ACCEPTED:MCS251_WIDE_FALLBACK;
}

#include "wide_domain.c.inc"

/* 未选择方案的诊断保留为带前缀注释，避免伪装成最终分配记录。 */
static void
wide_pointer_copy_audit(struct dbuf_s *output,const char *kind,struct dbuf_s *input)
{
  const char *text=dbuf_get_buf(input);size_t length=dbuf_get_length(input);
  for(size_t at=0;at<length;) {
    size_t end=at;while(end<length && text[end]!='\n')++end;
    dbuf_printf(output,"; wide pointer-copy %s ",kind);dbuf_append(output,text+at,end-at);
    dbuf_append_str(output,"\n");at=end+1;
  }
}


#include "callee_cost.h"
static int wide_callee_signature(symbol *callee,char signature[256])
{
  if(!callee || !IS_FUNC(callee->type))return 0;
  sym_link *t=callee->type;
  if((FUNC_SDCCCALL(t)!=2 && FUNC_SDCCCALL(t)!=4) || !IFFUNC_ISREENT(t) ||
     IFFUNC_ISISR(t) || IFFUNC_ISNAKED(t) || IFFUNC_ISCRITICAL(t) || IFFUNC_CALLEESAVES(t) ||
     IFFUNC_ISBANKEDCALL(t) || FUNC_REGBANK(t) || IFFUNC_HASVARARGS(t) || IFFUNC_ISNORETURN(t) ||
     IFFUNC_ISSMALLC(t) || IFFUNC_ISDYNAMICC(t))return 0;
  sym_link *r=t->next;
  if(!IS_SPEC(r)||!IS_INTEGRAL(r)||IS_BITINT(r)||isVolatile(r)||isAtomic(r)||getSize(r)>4||getSize(r)<1)return 0;
  unsigned used=(unsigned)snprintf(signature,256,"a%u-r%c%d-p",(unsigned)FUNC_SDCCCALL(t),IS_UNSIGNED(r)?'u':'s',getSize(r));
  unsigned count=0;
  for(value *v=FUNC_ARGS(t);v;v=v->next) {
    sym_link *a=v->type;
    if(++count>8||!IS_SPEC(a)||!IS_INTEGRAL(a)||IS_BITINT(a)||isVolatile(a)||isAtomic(a)||getSize(a)>4||getSize(a)<1||used>=230)return 0;
    used+=(unsigned)snprintf(signature+used,256-used,"%s%c%d@%u",count>1?",":"",IS_UNSIGNED(a)?'u':'s',getSize(a),(unsigned)SPEC_ARGREG(v->etype));
  }
  if(!count||used>=230)return 0;
  snprintf(signature+used,256-used,"-reentrant-bank0");return 1;
}
static int wide_resolve_callee_cost(const char *name,const void *context,unsigned metric[WLC_METRICS])
{
  const wide_function *f=(const wide_function*)context;
  if(!mcs251RangeNarrowMultiplyEnabled || !f || !wcs_load(mcs251CalleeCostFile,mcs251CalleeCostSha))return 0;
  const wide_callee_summary *summary=NULL;
  for(unsigned i=0;i<wcs_count;++i)if(!strcmp(name,wcs_entries[i].name))summary=&wcs_entries[i];
  if(!summary)return 0;
  bool found=false;
  for(int i=0;i<f->nn;++i) {
    const wide_node *n=&f->nodes[i];
    if(n->op!=CALL || !n->callee || strcmp(name,n->callee->rname))continue;
    char signature[256];
    if(!wide_callee_signature(n->callee,signature)||strcmp(signature,summary->signature))return 0;
    found=true;
  }
  if(!found)return 0;
  memcpy(metric,summary->metric,sizeof summary->metric);
  dbuf_printf(f->diagnostics,"; wide callee-cost bound target=%s signature=%s body-sha256=%s evidence-sha256=%s instructions=%u mov=%u memory=%u stack-read=%u stack-write=%u push=%u pop=%u\n",name,summary->signature,summary->sha,mcs251CalleeCostSha,metric[0],metric[1],metric[2],metric[3],metric[4],metric[5],metric[6]);
  return 1;
}

/* 各 MIR 变换共享完整函数的候选比较；不叠加局部收益猜测。
 * 内部错误一直向上传播，未知编码或任一成本退步时保留完整旧候选。 */
#include "indexed_field_flags.h"

static mcs251_wide_result
wide_choose_transform(wide_function *f,wide_selection_cost *cost,
  unsigned (*transform)(wide_function *),
  mcs251_wide_result (*choose)(wide_function *,wide_selection_cost *),
  const char *kind,const char *countName,bool requireSpill,
  const char *originalKind,const char *trialKind,bool requireKnownPaths,bool useOperandContext)
{
  wide_function *trial=Safe_alloc(sizeof *trial);memcpy(trial,f,sizeof *trial);
  struct dbuf_s originalCode,originalDiagnostics,trialCode,trialDiagnostics;
  dbuf_init(&originalCode,1024);dbuf_init(&originalDiagnostics,256);
  dbuf_init(&trialCode,1024);dbuf_init(&trialDiagnostics,256);
  trial->candidateOutput=&trialCode;trial->diagnostics=&trialDiagnostics;
  unsigned changes=transform(trial);
  mcs251_wide_result result;
  if(!changes)result=choose(f,cost);
  else {
    struct dbuf_s *code=f->candidateOutput,*diagnostics=f->diagnostics;
    wide_selection_cost oldCost={0},newCost={0};bool selected=false;
    f->candidateOutput=&originalCode;f->diagnostics=&originalDiagnostics;
    result=choose(f,&oldCost);
    f->candidateOutput=code;f->diagnostics=diagnostics;
    mcs251_wide_result originalResult=result,trialResult=MCS251_WIDE_FALLBACK;
    if(result==MCS251_WIDE_ACCEPTED && (!requireSpill || oldCost.spills)) {
      wide_liveness(trial);trialResult=choose(trial,&newCost);
      if(trialResult==MCS251_WIDE_ERROR)result=MCS251_WIDE_ERROR;
      else selected=trialResult==MCS251_WIDE_ACCEPTED && !oldCost.unknown && !newCost.unknown &&
        newCost.bytes<=oldCost.bytes && newCost.heuristic<=oldCost.heuristic && newCost.frame<=oldCost.frame &&
        (newCost.bytes<oldCost.bytes || newCost.heuristic<oldCost.heuristic || newCost.frame<oldCost.frame);
      /* 完整静态成本通过后，再逐个检查最终循环的一次迭代；冷路径不能补偿热循环。 */
      if(selected) {
        /* 仅新候选显式使用现有 symbol 解析器；未知地址仍为拒绝。
         * 旧选择器默认 false，保持历史规则和永久失败样本的重放。 */
        const char *canonicalOption=getenv("MX251_CANONICAL_BRANCH_COST");
        bool canonicalBranches=canonicalOption && !strcmp(canonicalOption,"1");
        int loopDecision=canonicalBranches?
          wide_loop_cost_compare_canonical(dbuf_get_buf(&originalCode),dbuf_get_length(&originalCode),
            dbuf_get_buf(&trialCode),dbuf_get_length(&trialCode),wide_resolve_cost_operand,wide_resolve_callee_cost,f,trial):useOperandContext?
          wide_loop_cost_compare_callees(dbuf_get_buf(&originalCode),dbuf_get_length(&originalCode),
            dbuf_get_buf(&trialCode),dbuf_get_length(&trialCode),wide_resolve_cost_operand,wide_resolve_callee_cost,f,trial):
          wide_loop_cost_compare(dbuf_get_buf(&originalCode),dbuf_get_length(&originalCode),
            dbuf_get_buf(&trialCode),dbuf_get_length(&trialCode));
        dbuf_printf(diagnostics,"; wide loop-cost kind=%s function=%s decision=%d legacy-fallback=%u\n",kind,currFunc->name,loopDecision,loopDecision<0);
        if(loopDecision==0 || (requireKnownPaths && loopDecision<0))selected=false;
      }
      if(selected && trial->nativeIndexedTrial) {
        unsigned sites=0;
        bool known=wide_indexed_flags_safe(dbuf_get_buf(&trialCode),dbuf_get_length(&trialCode),&sites);
        dbuf_printf(diagnostics,"; wide indexed-field flags-known=%u emitted-sites=%u\n",known,sites);
        if(!known)selected=false;
      }
    }
    dbuf_printf(diagnostics,"; wide %s choice function=%s %s=%u selected=%u original-result=%d trial-result=%d old-bytes=%u new-bytes=%u old-unknown=%u new-unknown=%u old-weighted=%u new-weighted=%u old-frame=%d new-frame=%d old-spills=%u new-spills=%u\n",
      kind,currFunc->name,countName,changes,selected,originalResult,trialResult,oldCost.bytes,newCost.bytes,oldCost.unknown,newCost.unknown,
      oldCost.heuristic,newCost.heuristic,oldCost.frame,newCost.frame,oldCost.spills,newCost.spills);
    const char *textAudit=getenv("MX251_TRANSFORM_TEXT_AUDIT");
    if(textAudit && !strcmp(textAudit,"1")) {
      struct dbuf_s *texts[2]={&originalCode,&trialCode};
      for(unsigned t=0;t<2;++t) {
        const char *s=dbuf_get_buf(texts[t]);size_t length=dbuf_get_length(texts[t]);
        for(size_t start=0;start<length;) {
          size_t end=start;while(end<length && s[end]!='\n')++end;
          dbuf_printf(diagnostics,"; transform-text %s %s %.*s\n",kind,t?"trial":"original",(int)(end-start),s+start);start=end+1;
        }
      }
    }
    struct dbuf_s *chosenCode=selected?&trialCode:&originalCode;
    struct dbuf_s *chosenDiagnostics=selected?&trialDiagnostics:&originalDiagnostics;
    wide_pointer_copy_audit(diagnostics,selected?originalKind:trialKind,selected?&originalDiagnostics:&trialDiagnostics);
    dbuf_append(diagnostics,dbuf_get_buf(chosenDiagnostics),dbuf_get_length(chosenDiagnostics));
    if(result==MCS251_WIDE_ACCEPTED)dbuf_append(code,dbuf_get_buf(chosenCode),dbuf_get_length(chosenCode));
    if(cost && result==MCS251_WIDE_ACCEPTED)*cost=selected?newCost:oldCost;
  }
  dbuf_destroy(&originalCode);dbuf_destroy(&originalDiagnostics);
  dbuf_destroy(&trialCode);dbuf_destroy(&trialDiagnostics);Safe_free(trial);
  return result;
}

static mcs251_wide_result
wide_choose_pointer_copy(wide_function *f,wide_selection_cost *cost)
{
  return wide_choose_transform(f,cost,wide_entry_pointer_copies,wide_choose_domain,
    "pointer-copy","copies",true,"original","trial",false,false);
}

/* 旧选择器的顺序和门槛不变：固定栈地址包住指针副本，后者仍只在
 * 旧分配候选存在 spill 时准入；向外提供完整候选成本供调度比较。 */
static mcs251_wide_result
wide_choose_stack_addresses(wide_function *f,wide_selection_cost *cost)
{
  return wide_choose_transform(f,cost,wide_fixed_stack_addresses,wide_choose_pointer_copy,
    "stack-address","accesses",false,"stack-original","stack-trial",false,false);
}

/* 新调度只与完整旧选择器的输出比较，不能提前消掉 spill 而使原有
 * 指针副本候选失去准入资格，再错误地把局部胜出当作整体不退步。 */
static mcs251_wide_result
wide_choose_integer_sinking(wide_function *f,wide_selection_cost *cost)
{
  return wide_choose_transform(f,cost,wide_sink_integer_expressions,wide_choose_stack_addresses,
    "integer-sink","moves",true,"sink-original","sink-trial",false,false);
}

static mcs251_wide_result
wide_choose_load_scheduling(wide_function *f,wide_selection_cost *cost)
{
  return wide_choose_transform(f,cost,wide_schedule_last_loads,wide_choose_integer_sinking,
    "load-schedule","loads",true,"loads-original","loads-trial",true,false);
}

/* ABI 只规定函数入口的物理位置，不要求参数在整个活期固定在那里。
 * 本轮仅接受闭合契约的两个完整整数参数；可取址、混宽、指针和浮点
 * 参数继续保留原路径。入口并行复制由发射器统一完成，不能逐条
 * RECEIVE 写目的寄存器而毁掉尚未接收的另一个参数。 */
static unsigned
wide_free_native_receives(wide_function *f)
{
  const char *enabled=getenv("MX251_NATIVE_RECEIVE_FREEDOM");
  if(!enabled || strcmp(enabled,"1") || f->ordinaryEntry || f->nativeReceiveFree ||
     f->formalAddress || f->received!=2 || !mcs251InternalNative(currFunc))return 0;
  unsigned arguments=0;
  for(value *arg=FUNC_ARGS(currFunc->type);arg;arg=arg->next) {
    if(++arguments>2 || !IS_SPEC(arg->type) || !IS_INTEGRAL(arg->type) ||
       getSize(arg->type)!=4 || isVolatile(arg->type) || isAtomic(arg->type) ||
       (arg->sym && arg->sym->addrtaken))return 0;
  }
  if(arguments!=2)return 0;
  int values[2]={-1,-1},end=-1;unsigned found=0;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    if(n->op==RECEIVE) {
      int index=n->origin->argreg-1;
      if(index<0 || index>1 || values[index]>=0 || n->result<0 ||
         n->result>=f->nv || n->width!=4)return 0;
      wide_value *v=&f->values[n->result];
      if(v->width!=4 || v->floating || v->pointer || v->codePointer ||
         v->pin!=(index?12:4) || !v->reads)return 0;
      values[index]=n->result;if(++found==2)end=i+1;
    } else if(found<2 && n->op!=FUNCTION && n->op!=LABEL && n->op!=WM_NOP)return 0;
  }
  if(found!=2 || values[0]==values[1] || end<0)return 0;
  /* 复制只发生在真实入口；入口标签不得由后续控制流重新进入。
   * 不凭 iCode key 的数值推断顺序，逐项核对已建 CFG 的标签引用。 */
  for(int i=0;i<end;++i)if(f->nodes[i].op==LABEL) {
    int label=f->nodes[i].label;
    for(int j=0;j<f->nn;++j) {
      wide_node *n=&f->nodes[j];
      if((n->op==GOTO || n->op==IFX) && n->target==label)return 0;
      for(int k=0;k<n->jumpCount;++k)
        if(f->jumpLabels[n->jumpStart+k]==label)return 0;
    }
  }
  for(int i=0;i<2;++i)f->values[values[i]].pin=-1;
  f->nativeReceiveFree=true;
  wide_reset_allocation(f);
  dbuf_printf(f->diagnostics,"; wide native-receive trial inputs=%d,%d physical=DR4,DR12 parallel=1\n",values[0],values[1]);
  return 2;
}

static mcs251_wide_result
wide_choose_native_receives(wide_function *f,wide_selection_cost *cost)
{
  /* 完整旧候选始终保留。入口搬运与跨调用保存都经过真实发射计价；
   * 字节、加权代价或栈帧任一退步、未知编码/循环路径均不能晋级。 */
  return wide_choose_transform(f,cost,wide_free_native_receives,wide_choose_load_scheduling,
    "native-receive","inputs",false,"receive-original","receive-trial",true,false);
}

static mcs251_wide_result
wide_choose_private_union_forward(wide_function *f,wide_selection_cost *cost)
{
  /* 对象消除必须与原完整选择器比较：重新分配产生的保存、spill 和
   * 入口搬运一并计价，未知路径或任何静态/热点退步均保留旧候选。 */
  return wide_choose_transform(f,cost,wide_private_union_forward,wide_choose_native_receives,
    "private-union-forward","objects",false,"union-original","union-trial",true,false);
}

static mcs251_wide_result
wide_choose_truth_branch(wide_function *f,wide_selection_cost *cost)
{
  return wide_choose_transform(f,cost,wide_fold_truth_branch,wide_choose_private_union_forward,
    "truth-branch","negations",false,"truth-original","truth-trial",true,true);
}


/* Explicit private diagnostics only; no candidate or target-name policy. */
static void
wide_affine_dump(wide_function *f)
{
  const char *enabled=getenv("MX251_AFFINE_LOOP_AUDIT");
  if(!enabled || strcmp(enabled,"1"))return;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    dbuf_printf(f->diagnostics,"; affine-mir function=%s node=%d op=%u key=%d left=%d:%d:%u right=%d:%d:%u result=%d width=%d sign=%u label=%d target=%d true=%u succ=%d,%d symbol=%s offset=%d byte=%u code=%u pointer=%u\n",
      currFunc->name,i,n->op,n->origin?n->origin->key:-1,n->left.value,n->left.width,n->left.literal,
      n->right.value,n->right.width,n->right.literal,n->result,n->width,n->sign,n->label,n->target,n->trueBranch,n->succ[0],n->succ[1],
      n->addressSymbol?n->addressSymbol->name:"-",n->addressOffset,n->byteAccess,n->codeAccess,n->pointerAccess);
  }
}

#include "wide_unsigned_affine.c.inc"

static mcs251_wide_result
wide_choose_unsigned_affine(wide_function *f,wide_selection_cost *cost)
{
  return wide_choose_transform(f,cost,wide_unsigned_affine_induction,wide_choose_truth_branch,
    "unsigned-affine","loops",false,"unsigned-affine-original","unsigned-affine-trial",true,true);
}

static mcs251_wide_result
wide_choose_affine(wide_function *f,wide_selection_cost *cost)
{
  return wide_choose_transform(f,cost,wide_affine_byte_loops,wide_choose_unsigned_affine,
    "affine-byte-loop","loops",false,"affine-original","affine-trial",true,true);
}

static mcs251_wide_result
wide_choose_native_indexed(wide_function *f,wide_selection_cost *cost)
{
  if(!mcs251IndexedFieldEnabled)return wide_choose_affine(f,cost);
  return wide_choose_transform(f,cost,wide_native_indexed_fields,wide_choose_affine,
    "native-indexed-field","sites",false,"indexed-old","indexed-trial",true,true);
}

#include "wide_loop_initializer.c.inc"

static mcs251_wide_result
wide_choose_loop_initializers(wide_function *f,wide_selection_cost *cost)
{
  if(!wide_loop_initializer_enabled())return wide_choose_native_indexed(f,cost);
  return wide_choose_transform(f,cost,wide_sink_loop_initializers,wide_choose_native_indexed,
    "loop-initializer","moves",true,"initializer-original","initializer-trial",true,true);
}

static unsigned
wide_range_multiply(wide_function *f)
{
  if(!mcs251RangeNarrowMultiplyEnabled)return 0;
  unsigned changes=0;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    if(n->op!=CALL || !n->ordinaryCall || n->callee!=muldiv[0][2][0] ||
       n->args!=2 || n->stackArgs!=1 || n->stackBytes!=4 || n->width!=4 ||
       n->result<0 || n->sign || n->floating || !n->rangeMultiplyMask)continue;
    wide_operand a=n->left,b=n->stackOperands[0];
    if(a.width!=4 || b.width!=4 || a.sign || b.sign || a.floating || b.floating ||
       a.codePointer || b.codePointer || (a.value>=0 && f->values[a.value].pointer) ||
       (b.value>=0 && f->values[b.value].pointer))continue;
    /* left 保存任意 u32，right 是已证明高 16 位全零的当前值快照。
     * 不删除/移动输入计算，不修改任何 memory、volatile 或 index 节点。 */
    n->left=(n->rangeMultiplyMask&2)?a:b;
    n->right=(n->rangeMultiplyMask&2)?b:a;
    n->op=WM_UMUL32N;n->args=n->stackArgs=n->stackBytes=0;
    n->ordinaryCall=false;n->callee=NULL;n->callSaveOmit=0;
    memset(n->stackOperands,0,sizeof n->stackOperands);
    memset(n->stackWidths,0,sizeof n->stackWidths);
    n->use=(wide_bits){{0}};wb_set(&n->use,n->left.value);wb_set(&n->use,n->right.value);
    ++changes;
    dbuf_printf(f->diagnostics,"; wide range-multiply trial icode=%d proof=point-range-u16 result=low32 input-order=preserved\n",n->origin->key);
  }
  if(changes)wide_reset_allocation(f);
  return changes;
}

static mcs251_wide_result
wide_choose_range_multiply(wide_function *f,wide_selection_cost *cost)
{
  return wide_choose_transform(f,cost,wide_range_multiply,wide_choose_loop_initializers,
    "range-multiply","calls",false,"multiply-original","multiply-trial",true,true);
}

/* 先以真实 callee 摘要完成 range 的整函数成本选择，再处理最终 rotate
 * 发射。rotate 不回流至 range 候选比较，避免把局部替换后的字节数与
 * 替换前的热路径向量混合用于重新选择。两开关关闭时走原 range 路径。 */
#include "wide_rotate_cost.c.inc"

static mcs251_wide_result
wide_choose_rotate(wide_function *f,wide_selection_cost *cost)
{
  if(!mcs251ConstantRotateFusionEnabled)return wide_choose_range_multiply(f,cost);
  struct dbuf_s baseline,candidate;dbuf_init(&baseline,1024);dbuf_init(&candidate,1024);
  struct dbuf_s *output=f->candidateOutput;f->candidateOutput=&baseline;
  wide_selection_cost original={0};mcs251_wide_result result=wide_choose_range_multiply(f,&original);
  f->candidateOutput=output;
  unsigned selected=0;if(result==MCS251_WIDE_ACCEPTED && !original.unknown)
    selected=wide_rotate_finalize(f,dbuf_get_buf(&baseline),dbuf_get_length(&baseline),&candidate);
  struct dbuf_s *chosen=selected?&candidate:&baseline;
  if(result==MCS251_WIDE_ACCEPTED)dbuf_append(output,dbuf_get_buf(chosen),dbuf_get_length(chosen));
  if(cost) {*cost=original;if(selected)cost->bytes=wide_emitted_bytes(f,dbuf_get_buf(chosen),dbuf_get_length(chosen)).bytes;}
  dbuf_destroy(&baseline);dbuf_destroy(&candidate);return result;
}

#include "wide_repeat_sequence.c.inc"

static bool
wide_is_function_body (ebbIndex *ebbi)
{
  unsigned entries = 0, ends = 0;
  /* 静态局部对象的初始化也经过寄存器分配，此时 currFunc 仍指向
   * 所属函数，但片段不含函数边界。只让完整函数进入宽发射器，
   * 防止在 GSINIT 中重复输出函数标签及函数入口/退出代码。 */
  for (int b = 0; b < ebbi->count; ++b)
    {
      eBBlock *bb = ebbi->bbOrder[b];
      for (iCode *ic = bb->sch; ic; ic = ic == bb->ech ? NULL : ic->next)
        if (ic->op == FUNCTION || ic->op == ENDFUNCTION)
          {
            if (!IS_SYMOP (IC_LEFT (ic)) || OP_SYMBOL (IC_LEFT (ic)) != currFunc)
              return false;
            if (ic->op == FUNCTION) ++entries;
            else ++ends;
          }
    }
  return entries == 1 && ends == 1;
}

static mcs251_wide_result
wide_try_function (ebbIndex *ebbi, struct dbuf_s *output, struct dbuf_s *diagnostics)
{
  mcs251_wide_result result=MCS251_WIDE_FALLBACK;wide_function *f;
  const char *rejectionStage="import";
  if(!currFunc)return MCS251_WIDE_FALLBACK;
  if(!mcs251WideEnabled) {dbuf_printf(diagnostics,"; wide admission function=%s skip=disabled\n",currFunc->name);return MCS251_WIDE_FALLBACK;}
  if(!wide_is_function_body(ebbi)) {dbuf_printf(diagnostics,"; wide admission function=%s skip=non-function-initializer\n",currFunc->name);return MCS251_WIDE_FALLBACK;}
  bool ordinary=FUNC_SDCCCALL(currFunc->type)==2;
  if((FUNC_SDCCCALL(currFunc->type)!=4 && !ordinary) ||
     (ordinary && (!mcs251WideOrdinaryEnabled || currFunc->cdef ||
                   !(IFFUNC_ISREENT(currFunc->type) || options.stackAuto))) ||
     options.debug || options.useXstack || options.noRegParams ||
     IFFUNC_ISISR(currFunc->type) || IFFUNC_ISNAKED(currFunc->type) ||
     IFFUNC_ISCRITICAL(currFunc->type) || IFFUNC_CALLEESAVES(currFunc->type) ||
     IFFUNC_ISBANKEDCALL(currFunc->type) || FUNC_REGBANK(currFunc->type) ||
     IFFUNC_HASVARARGS(currFunc->type) ||
     (!wide_abi_type(currFunc->type->next,currFunc->type) && !IS_VOID(currFunc->type->next))) {dbuf_printf(diagnostics,"; wide admission function=%s skip=entry-contract\n",currFunc->name);return MCS251_WIDE_FALLBACK;}
  /* 普通 ABI2 复用原三字节 CODE 指针合同；显式 ABI4 的 CODE 形参
   * 继续回退，让原 genFunction 给出诊断，不隐式扩展原生公开 ABI。 */
  for(value *arg=FUNC_ARGS(currFunc->type);arg;arg=arg->next)
    if((wide_code_pointer(arg->type) && !ordinary) || (ordinary && !wide_abi_type(arg->type,currFunc->type))) {dbuf_printf(diagnostics,"; wide admission function=%s skip=parameter-type\n",currFunc->name);return MCS251_WIDE_FALLBACK;}
  if(!wide_stack_signature(currFunc->type)) {dbuf_printf(diagnostics,"; wide admission function=%s skip=stack-signature\n",currFunc->name);return MCS251_WIDE_FALLBACK;}
  f=Safe_alloc(sizeof *f);memset(f,0,sizeof *f);f->ordinaryEntry=ordinary;f->diagnostics=diagnostics;f->candidateOutput=output;
  f->domainCount=mcs251ParameterDomainValues(currFunc,FUNC_ARGS(currFunc->type),1,f->domainValues);
  if(fatalError){Safe_free(f);return MCS251_WIDE_ERROR;}
  if(!wide_capacity_profile(f))goto fallback;
  if(!wide_ordered_asm_profile(f))goto fallback;
  dbuf_printf(diagnostics,"; wide capacity function=%s value-limit=%d node-limit=%d storage-values=%u storage-nodes=%u\n",
              currFunc->name,f->valueLimit,f->nodeLimit,(unsigned)WV_MAX,(unsigned)WN_MAX);
  if(mcs251RangeNarrowMultiplyEnabled)
    f->rangeAnalysis=computeIsolatedValinfos(ebbi,"_mx251_narrow_multiply");
  if(wide_import(f,ebbi)) {
    dbuf_printf(diagnostics,"; wide imported function=%s values=%d nodes=%d objects=%d frame=%d\n",currFunc->name,f->nv,f->nn,f->no,f->frame);
    rejectionStage="transform";
    if(WA_FAIL("after-import"))goto fallback;
    bool importedStackObjects=f->no!=0;
    wide_private_union_prepare(f);
    wide_immutable_parameter_homes(f);
    wide_strength_reduce(f);
    wide_stack_addresses(f);
    wide_private_union_finish(f);
    /* 早期浮点联合体调用因间接栈寻址而延后迁移。现在只在显式普通
     * ABI2 试验开关内，允许所有浮点内存访问已证明落在本地对象范围
     * 的函数使用直接栈访问；不缓存内容、不删 volatile 字节事务。
     * 闭合契约的 ABI4 可显式使用相同对象证明；外部/不明指针与
     * 未经闭合验证的 ABI4 仍保留回退，不因原生参数而放宽别名语义。 */
    bool floatMemory=false,call=false,directFloatMemory=true;
    for(int i=0;i<f->nn;++i) {
      wide_node *n=&f->nodes[i];call|=n->op==CALL;
      bool floating=(n->op==WM_LOAD && n->floating) || (n->op==WM_STORE && n->right.floating);
      floatMemory|=floating;
      if(floating && n->addressObject<0)directFloatMemory=false;
    }
    if(f->sampledQualified && importedStackObjects && floatMemory && call) {
      const char *nativeFloat=getenv("MX251_NATIVE_STACK_FLOAT");
      bool closedNative=nativeFloat && !strcmp(nativeFloat,"1") && mcs251InternalNative(currFunc);
      if((!f->ordinaryEntry && !closedNative) || !directFloatMemory) {
        dbuf_printf(diagnostics,"; wide migration deferred: stack float memory with call\n");
        wide_reject(f,"stack-float-memory-with-call-contract",__LINE__);
        goto fallback;
      }
      dbuf_printf(diagnostics,"; wide stack float call migration: all float accesses direct\n");
    }
    wide_stack_indexed_bases(f);
    wide_fold_indexed_addresses(f);
    wide_reuse_parameter_loads(f);
    wide_fold_extensions(f);
    wide_liveness(f);
    if(WA_FAIL("after-transform"))goto fallback;
    rejectionStage="allocation-or-emission";
    wide_affine_dump(f);
    result=wide_choose_repeat(f);
    if(result==MCS251_WIDE_ACCEPTED && WA_FAIL("after-emit")) {
      dbuf_append_str(output,"\n_mx251_discarded_test_marker:\n\t.db 0xa5\n");
      result=MCS251_WIDE_FALLBACK;
    }
  }
fallback:
  if(result==MCS251_WIDE_FALLBACK) {
    dbuf_printf(diagnostics,"; mcs251 wide-mir fallback: %s\n",currFunc->name);
    /* 保留拒绝位置，便于区分语义边界和容量限制；不改变回退机器码。 */
    if(f->nn)dbuf_printf(diagnostics,"; wide rejection: node=%d icode=%d opcode=%u values=%d objects=%d frame=%d\n",
                        f->nn,f->nodes[f->nn-1].origin->key,f->nodes[f->nn-1].origin->op,f->nv,f->no,f->frame);
    iCode *origin=f->rejectionOrigin;
    dbuf_printf(diagnostics,"; wide refusal function=%s stage=%s reason=%s source-line=%d icode=%d opcode=%u values=%d nodes=%d value-limit=%d node-limit=%d objects=%d frame=%d\n",
                currFunc->name,rejectionStage,f->rejectionReason?f->rejectionReason:rejectionStage,f->rejectionLine,
                origin?origin->key:-1,origin?origin->op:0,f->nv,f->nn,f->valueLimit,f->nodeLimit,f->no,f->frame);
  }
  freeIsolatedValinfos(f->rangeAnalysis);
  Safe_free(f);return result;
}


#include "wide_direct_byte_rmw.c.inc"

void mcs251CleanupWideCfg (struct dbuf_s *, ebbIndex *);

mcs251_wide_result
mcs251TryWideFunction (ebbIndex *ebbi)
{
  struct dbuf_s output, diagnostics;
  mcs251_wide_result result;
  dbuf_init(&output,1024);dbuf_init(&diagnostics,256);
#ifdef MX251_WIDE_AUDIT
  wa_snapshot *before=wa_begin(ebbi);
#endif
  result=wide_try_function(ebbi,&output,&diagnostics);
  if(mcs251InternalReturn(currFunc) && result!=MCS251_WIDE_ACCEPTED) {
    /* 同一封闭构建的调用点已使用 DR4，旧 ABI 回退会造成静默错码。 */
    dbuf_printf(&diagnostics,"; internal return contract refused backend fallback: %s\n",currFunc->name);
    result=MCS251_WIDE_ERROR;
  }
#ifdef MX251_WIDE_AUDIT
  if(WA_FAIL("mutate-input") && ebbi->count && ebbi->bbOrder[0]->sch)
    ebbi->bbOrder[0]->sch->key^=1;
  if(!wa_end(before,result))result=MCS251_WIDE_ERROR;
#endif
  /* 诊断不是候选代码：拒绝时保留原因，丢弃全部标签、指令和栈操作。
   * 内部错误明确失败，旧后端不能把不一致状态重新解释成可用代码。 */
  if(dbuf_get_length(&diagnostics))dbuf_append(codeOutBuf,dbuf_get_buf(&diagnostics),dbuf_get_length(&diagnostics));
  if(result==MCS251_WIDE_ACCEPTED && dbuf_get_length(&output))
  {
    /* 闭合叶函数过滤器单独绑定真实 CALL 与有限机器体；默认关闭。
     * 契约不符明确失败，不能把未证的替换退化成静默成功。过滤后的
     * 最终体另行验收，下面摘要必须读取它，而不是旧候选打分文本。 */
    if(!wide_wordmul_finalize(&output) || !mcs251InlineClosedLeafText(&output,currFunc,ebbi))result=MCS251_WIDE_ERROR;
    else {
    /* 只发布最终接受的候选；回退/失败和分配试算不能污染函数摘要。
     * 本版仅审计，不以 may-write 上界改变 ABI 或调用保存。 */
    /* 同一份物理切片模型用于普通和宽后端；函数标签和控制转移清空事实。 */
    const char *extended=getenv("MX251_EXTENDED_GPR_CONSTANTS");
    bool extendedOpaque=false;
    for(int bi=0;bi<ebbi->count;++bi) {
      eBBlock *bb=ebbi->bbOrder[bi];
      for(iCode *ic=bb->sch;ic;ic=ic==bb->ech?NULL:ic->next)
        if(ic->op==INLINEASM)extendedOpaque=true;
    }
    if((!extended || !strcmp(extended,"1")) && !extendedOpaque) {
      struct dbuf_s clean;dbuf_init(&clean,dbuf_get_length(&output)+64);
      char *text=Safe_strdup(dbuf_c_str(&output)),*line=text;
      mx251_extended_constants constants;mx251ExtendedReset(&constants);
      while(*line) {
        char *end=strchr(line,'\n');if(end)*end=0;
        if(mx251ExtendedApply(&constants,line))
          dbuf_printf(&clean,"; MX251 extended constant removed: %s\n",line);
        else dbuf_printf(&clean,"%s\n",line);
        if(!end)break;line=end+1;
      }
      dbuf_set_length(&output,0);dbuf_append(&output,dbuf_get_buf(&clean),dbuf_get_length(&clean));
      dbuf_destroy(&clean);Safe_free(text);
    }
    if(!extendedOpaque)wide_direct_byte_rmw(&output);
    if(!extendedOpaque)mcs251CleanupWideCfg(&output,ebbi);
    dbuf_printf(&output,"; mx251-bank-contract schema=1 symbol=%s abi=%u bank=%u naked=%u\n",currFunc->rname,(unsigned)FUNC_SDCCCALL(currFunc->type),(unsigned)FUNC_REGBANK(currFunc->type),IFFUNC_ISNAKED(currFunc->type)?1u:0u);
    mx251_effects e=mx251TextEffects(dbuf_get_buf(&output),dbuf_get_length(&output));
    dbuf_printf(&output,"; mx251-effects schema=1 function=%s abi=%u complete=%u reads=%08lx writes=%08lx specialReads=%08lx specialWrites=%08lx flagsReads=%08lx flagsWrites=%08lx memory=%u instructions=%u unknown=%u calls=%u\n",
      currFunc->rname,(unsigned)FUNC_SDCCCALL(currFunc->type),!e.unknown,
      (unsigned long)e.reads,(unsigned long)e.writes,(unsigned long)e.specialReads,(unsigned long)e.specialWrites,
      (unsigned long)e.flagsReads,(unsigned long)e.flagsWrites,e.memory,e.instructions,e.unknown,e.calls);
    dbuf_append(codeOutBuf,dbuf_get_buf(&output),dbuf_get_length(&output));
    }
  }
  dbuf_destroy(&output);dbuf_destroy(&diagnostics);
  if(result==MCS251_WIDE_ERROR)
    werror(E_INTERNAL_ERROR,__FILE__,__LINE__,"mcs251 wide invariant failed; legacy fallback prohibited");
  return result;
}
