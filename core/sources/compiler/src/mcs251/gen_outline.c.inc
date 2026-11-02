/* Stack-aware exact-region outlining. GPL-2.0-or-later. */
#include "outline_region.h"
typedef struct {
  lineNode *node;
  char key[M251_OUTLINE_TEXT], body[M251_OUTLINE_TEXT];
  unsigned run;
  bool used;
} m251_outline_line;

/* ECALL 改变 SP/SPH，因此按地址拒绝它们的任意别名。
 * PSW/PSW1 也保持保守；bit 地址先转换为所属 SFR 字节。
 * 只依赖目标寄存器契约和 IR 类型，不依赖器件头文件或函数名。 */
static bool m251OutlineReadSfr (operand *op, const char **name, int *bit)
{
  sym_link *spec;
  unsigned address;
  if (!op || !IS_SYMOP(op) || IS_ITEMP(op) || !IS_SPEC(operandType(op)) ||
      isAtomic(operandType(op))) return FALSE;
  spec=OP_SYM_ETYPE(op);
  if (!SPEC_ABSA(spec) || (SPEC_SCLS(spec)!=S_SFR && SPEC_SCLS(spec)!=S_SBIT)) return FALSE;
  address=SPEC_ADDR(spec);
  *bit=SPEC_SCLS(spec)==S_SBIT;
  if (address<0x80 || address>0xff || (!*bit && getSize(operandType(op))!=1)) return FALSE;
  if (*bit) address &= 0xf8;
  if (address==0x81 || address==0xbe || address==0xd0 || address==0xd1) return FALSE;
  *name=OP_SYMBOL(op)->rname;
  return *name && **name;
}

static bool m251OutlineLine (lineNode *p, char *key, char *body)
{
  operand *inputs[2]={IC_LEFT(p->ic),IC_RIGHT(p->ic)};
  const char *readName=NULL;
  int readBit=0;
  operand *result=IC_RESULT(p->ic);
  /* 指针本身不带 volatile 不代表其指向对象可透明处理。
   * 间接外设/原子访问没有绝对 SFR 证明，保持屏障。 */
  operand *all[3]={inputs[0],inputs[1],result};
  for (unsigned i=0;i<3;++i)
    if (all[i] && IS_PTR(operandType(all[i])) &&
        (isVolatile(operandType(all[i])->next) || isAtomic(operandType(all[i])->next))) return FALSE;
  if (result && (isAtomic(operandType(result)) || mcs251TransferObservable(result))) return FALSE;
  for (unsigned i=0;i<2;++i)
    {
      operand *op=inputs[i];const char *name=NULL;int bit=0;
      if (!op) continue;
      if (isAtomic(operandType(op))) return FALSE;
      if (!mcs251TransferObservable(op)) continue;
      if (!m251OutlineReadSfr(op,&name,&bit) || (readName && strcmp(name,readName))) return FALSE;
      readName=name;readBit=bit;
    }
  return m251OutlineParseRead(p->line,key,body,readName,readBit);
}

static void mcs251OutlineRegions (lineNode *head, struct dbuf_s *helpers)
{
  static unsigned serial;
  unsigned count=0, run=1, capacity=0;
  m251_outline_line *lines;
  lineNode *last=NULL;
  const char *off=getenv("MX251_DISABLE_OUTLINING");
  if ((off && !strcmp(off,"1")) || !currFunc || options.useXstack ||
      IFFUNC_ISNAKED(currFunc->type) || IFFUNC_CALLEESAVES(currFunc->type) ||
      IFFUNC_ISISR(currFunc->type) || FUNC_REGBANK(currFunc->type)) return;
  for (lineNode *p=head;p;p=p->next)
    {
      if (p->isInline || (p->ic && (p->ic->op==CRITICAL || p->ic->op==ENDCRITICAL))) return;
      if (!p->isComment && !p->isDebug && !p->isLabel) last=p;
      ++capacity;
    }
  /* helper 放在函数后；普通顺序执行不得落入其正文。 */
  if (!capacity || !last || (!lineIsInst(last,"eret") && !lineIsInst(last,"ret") &&
      !lineIsInst(last,"ejmp") && !lineIsInst(last,"ljmp") && !lineIsInst(last,"sjmp"))) return;
  lines=Safe_alloc(capacity*sizeof(*lines));
  for (lineNode *p=head;p;p=p->next)
    {
      char key[M251_OUTLINE_TEXT], body[M251_OUTLINE_TEXT];
      if (p->isComment || p->isDebug) continue;
      if (p->isLabel || !p->ic || !m251OutlineLine(p,key,body)) { ++run; continue; }
      lines[count].node=p; lines[count].run=run; lines[count].used=FALSE;
      strcpy(lines[count].key,key); strcpy(lines[count].body,body); ++count;
    }
  /* 字节收益使用严格下界：每条被接纳的机器指令至少 1 B。
   * 每次 ECALL 4 B、每个 ERET 1 B。不是周期模型，也不声称零开销。
   * 只取长区域、至少三份，防止大量短 helper 消耗运行时间和调试可读性。 */
  for (;;)
    {
      unsigned bestStart=0,bestLength=0,bestCopies=0,bestGain=63;
      for (unsigned i=0;i<count;++i)
        {
          if (lines[i].used) continue;
          unsigned max=0;
          while (i+max<count && max<M251_OUTLINE_MAX && !lines[i+max].used &&
                 lines[i+max].run==lines[i].run) ++max;
          for (unsigned length=M251_OUTLINE_MIN;length<=max;++length)
            {
              unsigned copies=0;
              for (unsigned j=i;j+length<=count;)
                {
                  unsigned k=0;
                  while (k<length && !lines[j+k].used && lines[j+k].run==lines[j].run &&
                         !strcmp(lines[i+k].key,lines[j+k].key)) ++k;
                  if (k==length) { ++copies; j+=length; } else ++j;
                }
              unsigned gain=copies>=3?(copies-1)*length-copies*4-1:0;
              if (gain>bestGain) { bestGain=gain;bestStart=i;bestLength=length;bestCopies=copies; }
            }
        }
      if (!bestLength) break;
      symbol *label=newiTempLabel(NULL);
      char target[64],call[96];
      SNPRINTF(target,sizeof target,"%05d$",labelKey2num(label->key));
      SNPRINTF(call,sizeof call,"ecall\t%s",target);
      dbuf_printf(helpers,"; MX251 outline %u: %u instructions, %u copies, byte saving >= %u; extra stack 3\n%s:\n",
                  ++serial,bestLength,bestCopies,bestGain,target);
      for (unsigned k=0;k<bestLength;++k) dbuf_printf(helpers,"\t%s\n",lines[bestStart+k].body);
      dbuf_append_str(helpers,"\teret\n");
      for (unsigned j=bestStart;j+bestLength<=count;)
        {
          unsigned k=0;
          while (k<bestLength && !lines[j+k].used && lines[j+k].run==lines[j].run &&
                 !strcmp(lines[bestStart+k].key,lines[j+k].key)) ++k;
          if (k!=bestLength) { ++j; continue; }
          for (k=0;k<bestLength;++k)
            {
              lineNode *p=lines[j+k].node;
              p->line=Safe_strdup(k?"; MX251 outlined instruction":call);
              p->isComment=k!=0;p->aln=NULL;lines[j+k].used=TRUE;
            }
          j+=bestLength;
        }
    }
  Safe_free(lines);
}
