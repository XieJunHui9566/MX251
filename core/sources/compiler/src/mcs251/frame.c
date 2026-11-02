/* MCS251 frame-address lowering. GPL-2.0-or-later. */
#include "common.h"
#include "ralloc.h"
#include "frame.h"

static mcs251_frame_view *views;

const mcs251_frame_view *
mcs251FrameView (operand *op)
{
  mcs251_frame_view *v;
  if (!op || !IS_TRUE_SYMOP (op))
    return NULL;
  for (v = views; v; v = v->next)
    if (v->view == OP_SYMBOL (op))
      return v;
  return NULL;
}

void
mcs251ClearFrameAccesses (void)
{
  while (views)
    {
      mcs251_frame_view *v = views;
      views = v->next;
      Safe_free (v);
    }
}

static bool
flatPointer (operand *op)
{
  sym_link *type;
  if (!op || !IS_ITEMP (op) || isOperandVolatile (op, TRUE) || isAtomic (operandType (op)))
    return FALSE;
  type = operandType (op);
  return IS_PTR (type) && getSize (type) == 3 &&
    (DCL_TYPE (type) == FPOINTER || DCL_TYPE (type) == GPOINTER);
}

/* 沿唯一 SSA 式定义恢复对象和字节偏移。仅接受保持地址空间的复制、
   转换和常量加减；多定义、动态下标、整数往返、循环链均保留原路径。 */
static bool
frameAddress (operand *op, symbol **object, long *offset, unsigned depth)
{
  iCode *def;
  if (depth > 16 || !flatPointer (op) || bitVectnBitsOn (OP_DEFS (op)) != 1)
    return FALSE;
  def = hTabItemWithKey (iCodehTab, bitVectFirstBit (OP_DEFS (op)));
  if (!def || !isOperandEqual (IC_RESULT (def), op))
    return FALSE;
  if (def->op == ADDRESS_OF && IS_TRUE_SYMOP (IC_LEFT (def)))
    {
      symbol *s = OP_SYMBOL (IC_LEFT (def));
      if (!s->onStack || !SPEC_OCLS (s->etype) || SPEC_OCLS (s->etype)->paged ||
          isOperandVolatile (IC_LEFT (def), TRUE) || isAtomic (operandType (IC_LEFT (def))) ||
          (IC_RIGHT (def) && !IS_OP_LITERAL (IC_RIGHT (def))))
        return FALSE;
      *object = s;
      *offset = IC_RIGHT (def) ? operandLitValue (IC_RIGHT (def)) : 0;
      return *offset >= 0 && *offset <= getSize (s->type);
    }
  if ((def->op == '=' && !POINTER_SET (def)) || def->op == CAST)
    return frameAddress (IC_RIGHT (def), object, offset, depth + 1);
  if ((def->op == '+' || def->op == '-') && IS_OP_LITERAL (IC_RIGHT (def)) &&
      frameAddress (IC_LEFT (def), object, offset, depth + 1))
    {
      double delta = operandLitValue (IC_RIGHT (def));
      if (delta < -65535 || delta > 65535)
        return FALSE;
      *offset += (def->op == '+' ? 1 : -1) * (long)delta;
      return *offset >= 0 && *offset <= getSize ((*object)->type);
    }
  return FALSE;
}

static bool
pureAddressDefinition (iCode *ic)
{
  symbol *object;
  long offset;
  return ic->op != IFX && ic->op != JUMPTABLE && !POINTER_SET (ic) &&
    IC_RESULT (ic) && frameAddress (IC_RESULT (ic), &object, &offset, 0);
}

void
mcs251LowerFrameAccesses (eBBlock **blocks, int count)
{
  int b;
  bool changed;
  wassert (!views);
  if (options.useXstack)
    return;
  for (b = 0; b < count; ++b)
    for (iCode *ic = blocks[b]->sch; ic; ic = ic->next)
      {
        bool store = POINTER_SET (ic);
        operand *pointer, *value, *location;
        sym_link *ptype;
        symbol *object, *view;
        long offset;
        int size;
        char name[64];
        mcs251_frame_view *v;
        if (!store && !POINTER_GET (ic))
          continue;
        pointer = store ? IC_RESULT (ic) : IC_LEFT (ic);
        value = store ? IC_RIGHT (ic) : IC_RESULT (ic);
        if (!flatPointer (pointer))
          continue;
        ptype = operandType (pointer);
        size = getSize (operandType (value));
        if ((size != 1 && size != 2 && size != 4) ||
            IS_AGGREGATE (ptype->next) || IS_AGGREGATE (operandType (value)) ||
            IS_BITVAR (ptype->next) || IS_BITVAR (operandType (value)) ||
            isAtomic (ptype->next) || isAtomic (operandType (value)) ||
            getSize (ptype->next) != size ||
            (!store && IC_RIGHT (ic) && (!IS_OP_LITERAL (IC_RIGHT (ic)) || operandLitValue (IC_RIGHT (ic)) != 0)) ||
            !frameAddress (pointer, &object, &offset, 0) ||
            offset > (long)getSize (object->type) - size)
          continue;
        /* 第一阶段排除已有特殊寄存器约束的值；普通寄存器或溢出值均受支持。
           不取消先前为调用、条件和累加器选定的约束。 */
        if (!(store && IS_OP_LITERAL (value)) &&
            (!IS_ITEMP (value) || OP_SYMBOL (value)->remat ||
             OP_SYMBOL (value)->accuse || OP_SYMBOL (value)->ruonly ||
             OP_SYMBOL (value)->regType == REG_CND || OP_SYMBOL (value)->regType == REG_BIT))
          continue;
        SNPRINTF (name, sizeof name, "__mcs251_frame_%d", ic->key);
        view = newSymbol (name, 1);
        view->type = copyLinkChain (ptype->next);
        view->etype = getSpec (view->type);
        SPEC_OCLS (view->etype) = SPEC_OCLS (object->etype);
        view->onStack = view->addrtaken = view->allocreq = 1;
        view->localof = currFunc;
        /* 每次访问独立视图，且对剩余共享值分析显式不透明。
           不能把别名视图当成不相交局部变量，更不能缓存对象内容。 */
        SPEC_VOLATILE (view->etype) = 1;
        location = operandFromSymbol (view, FALSE);
        location->isaddr = 0;
        v = Safe_alloc (sizeof *v);
        v->view = view; v->object = object; v->byteOffset = offset;
        v->byteAccess = isVolatile (ptype->next) || isOperandVolatile (value, TRUE);
        v->next = views; views = v;
        object->allocreq = 1;
        bitVectUnSetBit (OP_USES (pointer), ic->key);
        ic->op = '=';
        IC_LEFT (ic) = NULL;
        if (store)
          IC_RESULT (ic) = location;
        else
          IC_RIGHT (ic) = location;
      }
  /* 仅删除现在没有使用的纯地址定义。真实逃逸、传参或比较仍保留地址。
     删除按使用链迭代，绝不删除对应对象或内存读写。 */
  do
    {
      changed = FALSE;
      for (b = 0; b < count; ++b)
        for (iCode *ic = blocks[b]->sch, *next; ic; ic = next)
          {
            next = ic->next;
            if (pureAddressDefinition (ic) && bitVectIsZero (OP_USES (IC_RESULT (ic))))
              {
                if (IC_LEFT (ic) && IS_SYMOP (IC_LEFT (ic)))
                  bitVectUnSetBit (OP_USES (IC_LEFT (ic)), ic->key);
                if (IC_RIGHT (ic) && IS_SYMOP (IC_RIGHT (ic)))
                  bitVectUnSetBit (OP_USES (IC_RIGHT (ic)), ic->key);
                bitVectUnSetBit (OP_DEFS (IC_RESULT (ic)), ic->key);
                remiCodeFromeBBlock (blocks[b], ic);
                hTabDeleteItem (&iCodehTab, ic->key, ic, DELETE_ITEM, NULL);
                changed = TRUE;
              }
          }
    }
  while (changed);
}
