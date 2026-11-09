/* SPDX-License-Identifier: GPL-2.0-or-later
 * A cast-only temporary used exclusively in flat pointer addition/subtraction
 * needs its low 24 bits, not a standalone 24-bit integer representation. Keep
 * the original integer extension in a native register; the pointer result
 * supplies modulo-2^24 truncation. Do not change the shared C iCode types. */
static bool
wide_address_offset_operand(wide_function *f,operand *op,wide_operand *v)
{
  if(!op || !IS_ITEMP(op) || IS_OP_GLOBAL(op) || OP_SYMBOL(op)->addrtaken ||
     !f->sourceBlocks)return false;
  sym_link *t=operandType(op);
  if(!IS_BITINT(t) || SPEC_BITINTWIDTH(t)!=24 || getSize(t)!=3 ||
     isVolatile(t) || isAtomic(t) || isOperandVolatile(op,FALSE))return false;
  symbol *s=OP_SYMBOL(op);
  for(int i=0;i<f->nv;++i)if(f->values[i].addressOnlyOffset && f->values[i].sym==s) {
    *v=(wide_operand){.value=i,.width=f->values[i].width,.sign=f->values[i].sign};return true;
  }
  unsigned definitions=0,uses=0;
  int nativeWidth=0;
  bool nativeSign=false,mixed=false;
  for(int b=0;b<f->sourceBlocks->count;++b) {
    eBBlock *bb=f->sourceBlocks->bbOrder[b];
    if(!bb->sch || (bb->noPath && optimize.label4 &&
       bb->entryLabel!=entryLabel && bb->entryLabel!=returnLabel))continue;
    for(iCode *ic=bb->sch;ic;ic=ic==bb->ech?NULL:ic->next) {
      operand *left=IC_LEFT(ic),*right=IC_RIGHT(ic),*result=IC_RESULT(ic);
      if(left && IS_SYMOP(left) && OP_SYMBOL(left)==s)return false;
      if(right && IS_SYMOP(right) && OP_SYMBOL(right)==s) {
        if((ic->op!='+' && ic->op!='-') || !left || !result ||
           !wide_pointer(operandType(left)) || !wide_pointer(operandType(result)))return false;
        ++uses;
      }
      if(result && IS_SYMOP(result) && OP_SYMBOL(result)==s) {
        if(ic->op!=CAST || POINTER_SET(ic) || !right ||
           !IS_INTEGRAL(operandType(right)) || !wide_value_type(operandType(right),true))return false;
        int width=wide_width(operandType(right));
        bool sign=!IS_UNSIGNED(operandType(right));
        if(!definitions) {nativeWidth=width;nativeSign=sign;}
        else if(nativeWidth!=width || nativeSign!=sign)mixed=true;
        ++definitions;
      }
    }
  }
  if(!definitions || !uses || !wide_capacity_room(f,1,0,__LINE__))return false;
  /* Matching narrow definitions can keep their original representation.
   * Mixed definitions extend at each cast so every path has the same bits. */
  if(mixed) {nativeWidth=4;nativeSign=false;}
  int value=f->nv++;
  f->values[value]=(wide_value){.sym=s,.width=nativeWidth,.sign=nativeSign,.addressOnlyOffset=true,
    .reg=-1,.slot=-1,.pin=-1,.preference=-1};
  *v=(wide_operand){.value=value,.width=nativeWidth,.sign=nativeSign};
  dbuf_printf(f->diagnostics,"; wide address-only offset temporary=%s bits=24 native=%d sign=%u definitions=%u uses=%u\n",s->name,8*nativeWidth,nativeSign,definitions,uses);
  return true;
}
