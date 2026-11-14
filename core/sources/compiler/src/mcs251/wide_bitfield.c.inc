/* SPDX-License-Identifier: GPL-2.0-or-later
 * C fields lower to ordinary byte memory and integer nodes.
 * Expansion precedes CFG construction: later passes see the actual reads,
 * masks and writes, including one-byte volatile transactions. */
static bool
wide_field_import(wide_function *f,wide_node *n,iCode *ic,bool store,symbol *object,int offset)
{
  operand *p=store?IC_RESULT(ic):IC_LEFT(ic),*value=store?IC_RIGHT(ic):IC_RESULT(ic);
  sym_link *pt=operandType(p),*vt=operandType(value);
  sym_link *field=store?pt->next:vt;
  if(!IS_BITFIELD(field) || isAtomic(field) || isAtomic(vt) ||
     (!object && !wide_pointer(pt)) || (store && (isConst(field) || wide_code_pointer(pt))))return false;
  unsigned start=SPEC_BSTR(field),bits=SPEC_BLEN(field);
  if(!bits || bits>32 || start>7 || (bits<=8 ? bits>8-start : start!=0) ||
     getSize(field)!=(bits+7)/8)
    return wide_reject(f,"bitfield-layout",__LINE__);
  if(!IS_INTEGRAL(vt) || !wide_value_type(vt,true))return false;
  if(!store && IC_RIGHT(ic) && (!IS_OP_LITERAL(IC_RIGHT(ic)) || operandLitValue(IC_RIGHT(ic))!=0))return false;
  if(object) {
    if(offset<0 || offset+(int)((bits+7)/8)>getSize(object->type))return false;
    n->addressSymbol=object;n->addressOffset=offset;
  } else if(!wide_use(f,n,p,&n->left))return false;
  if(store) {if(!wide_use(f,n,value,&n->right))return false;}
  else if(!wide_result(f,n,value))return false;
  n->op=store?WM_FIELD_STORE:WM_FIELD_LOAD;n->width=1;
  n->fieldStart=start;n->fieldBits=bits;n->fieldBoolean=IS_BOOLEAN(field);
  n->sign=!SPEC_USIGN(field) && !n->fieldBoolean;
  n->codeAccess=wide_code_pointer(pt);
  n->byteAccess=isVolatile(pt->next) || isOperandVolatile(value,TRUE) ||
    (object && isVolatile(object->type));
  f->body=true;return true;
}

static wide_operand
wide_field_literal(unsigned value)
{return (wide_operand){.value=-1,.width=1,.literal=value};}

static int
wide_field_value_width(wide_function *f,int width)
{
  if(!wide_capacity_room(f,1,0,__LINE__))return -1;
  int v=f->nv++;
  f->values[v]=(wide_value){.sym=currFunc,.width=width,.symbolSnapshot=true,
    .reg=-1,.slot=-1,.pin=-1,.preference=-1};
  return v;
}

static int
wide_field_value(wide_function *f)
{return wide_field_value_width(f,1);}

static bool
wide_field_append(wide_function *f,wide_node *out,int *count,wide_node n)
{
  if(*count>=f->nodeLimit)return wide_reject(f,"node-capacity",__LINE__);
  n.use=n.def=n.in=n.out=(wide_bits){{0}};
  if(n.result>=0)wb_set(&n.def,n.result);
  wb_set(&n.use,n.left.value);wb_set(&n.use,n.right.value);
  out[(*count)++]=n;return true;
}

static bool
wide_field_op_width(wide_function *f,wide_node *out,int *count,iCode *origin,unsigned op,int width,
              wide_operand left,wide_operand right,int destination,bool sign,wide_operand *result)
{
  int value=destination>=0?destination:wide_field_value_width(f,width);
  if(value<0)return false;
  wide_node n={.op=op,.origin=origin,.result=value,.width=width,.sign=sign,
    .left=left,.right=right,.addressIndex={.value=-1},.addressObject=-1,
    .label=-1,.target=-1,.succ={-1,-1}};
  if(!wide_field_append(f,out,count,n))return false;
  *result=(wide_operand){.value=value,.width=width,.sign=sign};return true;
}

static bool
wide_field_op(wide_function *f,wide_node *out,int *count,iCode *origin,unsigned op,
              wide_operand left,wide_operand right,int destination,bool sign,wide_operand *result)
{return wide_field_op_width(f,out,count,origin,op,1,left,right,destination,sign,result);}

/* SDCC's field ABI is low byte first, including on big-endian MCS251.
 * Fields larger than a byte start at bit zero. Read/write exactly the bytes
 * occupied by the field; only the last partial byte needs read-modify-write.
 * Three physical bytes become a four-byte MIR value, never a four-byte access. */
static bool
wide_field_multibyte(wide_function *f,wide_node *out,int *count,wide_node n)
{
  bool store=n.op==WM_FIELD_STORE;
  unsigned bytes=(n.fieldBits+7)/8,width=bytes<=2?2:4;
  wide_operand none={.value=-1},input=n.right,joined=none;
  if(store && input.value>=0 &&
     !wide_field_op_width(f,out,count,n.origin,CAST,width,none,input,-1,false,&input))return false;
  input.sign=false;
  for(unsigned k=0;k<bytes;++k) {
    unsigned bits=k+1==bytes?n.fieldBits-8*k:8,mask=(1u<<bits)-1;
    wide_node memory=n;
    memory.width=1;memory.sign=false;memory.result=-1;memory.right=none;
    memory.fieldStart=memory.fieldBits=0;memory.fieldBoolean=false;
    if(n.addressSymbol)memory.addressOffset+=(int)k;
    else if(k && !wide_field_op_width(f,out,count,n.origin,'+',4,n.left,
            (wide_operand){.value=-1,.width=4,.literal=k},-1,false,&memory.left))return false;
    wide_operand data=none,raw=none;
    if(store) {
      if(input.value<0)data=wide_field_literal((input.literal>>(8*k))&mask);
      else {
        if(!wide_field_op(f,out,count,n.origin,GETBYTE,input,wide_field_literal(8*k),-1,false,&data))return false;
        if(bits<8 && !wide_field_op(f,out,count,n.origin,BITWISEAND,data,wide_field_literal(mask),-1,false,&data))return false;
      }
    }
    if(!store || bits<8) {
      wide_node load=memory;load.op=n.addressSymbol?WM_SYMBOL_LOAD:WM_LOAD;
      load.result=wide_field_value(f);
      if(load.result<0 || !wide_field_append(f,out,count,load))return false;
      raw=(wide_operand){.value=load.result,.width=1};
    }
    if(store) {
      if(bits<8) {
        if(!wide_field_op(f,out,count,n.origin,BITWISEAND,raw,wide_field_literal(255u^mask),-1,false,&raw) ||
           !wide_field_op(f,out,count,n.origin,'|',raw,data,-1,false,&data))return false;
      }
      memory.op=n.addressSymbol?WM_SYMBOL_STORE:WM_STORE;memory.right=data;
      if(!wide_field_append(f,out,count,memory))return false;
    } else {
      if(bits<8 && !wide_field_op(f,out,count,n.origin,BITWISEAND,raw,wide_field_literal(mask),-1,false,&raw))return false;
      if(!wide_field_op_width(f,out,count,n.origin,CAST,width,none,raw,-1,false,&raw))return false;
      if(k && !wide_field_op_width(f,out,count,n.origin,LEFT_OP,width,raw,wide_field_literal(8*k),-1,false,&raw))return false;
      if(!k)joined=raw;
      else if(!wide_field_op_width(f,out,count,n.origin,'|',width,joined,raw,-1,false,&joined))return false;
    }
  }
  if(!store) {
    unsigned shift=8*width-n.fieldBits;
    if(n.sign && shift) {
      if(!wide_field_op_width(f,out,count,n.origin,LEFT_OP,width,joined,wide_field_literal(shift),-1,false,&joined))return false;
      joined.sign=true;
      if(!wide_field_op_width(f,out,count,n.origin,RIGHT_OP,width,joined,wide_field_literal(shift),n.result,true,&joined))return false;
    } else if(!wide_field_op_width(f,out,count,n.origin,CAST,width,none,joined,n.result,n.sign,&joined))return false;
  }
  return true;
}

static bool
wide_fields_lower(wide_function *f)
{
  unsigned fields=0;
  for(int i=0;i<f->nn;++i)fields+=f->nodes[i].op==WM_FIELD_LOAD || f->nodes[i].op==WM_FIELD_STORE;
  if(!fields)return true;
  wide_node *out=Safe_alloc(f->nodeLimit*sizeof *out);int count=0;
  wide_operand none={.value=-1};
  for(int i=0;i<f->nn;++i) {
    wide_node n=f->nodes[i];bool store=n.op==WM_FIELD_STORE;
    if(!store && n.op!=WM_FIELD_LOAD) {
      if(count>=f->nodeLimit) {wide_reject(f,"node-capacity",__LINE__);goto failed;}
      out[count++]=n;continue;
    }
    unsigned start=n.fieldStart,bits=n.fieldBits,mask=0;
    if(bits<=8)mask=((1u<<bits)-1)<<start;
    bool fieldSign=n.sign;
    f->rejectionOrigin=n.origin;
    wide_operand data=none,raw=none;
    if(bits>8) {
      if(!wide_field_multibyte(f,out,&count,n))goto failed;
      goto field_done;
    }
    if(store) {
      data=n.right;
      if(data.value<0) data=wide_field_literal(((n.fieldBoolean?!!data.literal:data.literal)<<start)&mask);
      else {
        if(!wide_field_op(f,out,&count,n.origin,n.fieldBoolean?WM_BIT_CAST:CAST,none,data,-1,false,&data))goto failed;
        if(bits<8 && !n.fieldBoolean &&
           !wide_field_op(f,out,&count,n.origin,BITWISEAND,data,wide_field_literal((1u<<bits)-1),-1,false,&data))goto failed;
        if(start && !wide_field_op(f,out,&count,n.origin,LEFT_OP,data,wide_field_literal(start),-1,false,&data))goto failed;
      }
    }
    if(!store || bits<8) {
      wide_node load=n;
      load.op=n.addressSymbol?WM_SYMBOL_LOAD:WM_LOAD;
      load.result=(!store && bits==8)?n.result:wide_field_value(f);
      if(load.result<0)goto failed;
      load.right=none;load.sign=false;load.fieldStart=load.fieldBits=0;load.fieldBoolean=false;
      if(!wide_field_append(f,out,&count,load))goto failed;
      raw=(wide_operand){.value=load.result,.width=1};
    }
    if(store) {
      if(bits<8) {
        if(!wide_field_op(f,out,&count,n.origin,BITWISEAND,raw,wide_field_literal(255u^mask),-1,false,&raw))goto failed;
        if(!wide_field_op(f,out,&count,n.origin,'|',raw,data,-1,false,&data))goto failed;
      }
      n.op=n.addressSymbol?WM_SYMBOL_STORE:WM_STORE;n.result=-1;n.right=data;n.sign=false;
      n.fieldStart=n.fieldBits=0;n.fieldBoolean=false;
      if(!wide_field_append(f,out,&count,n))goto failed;
    } else if(bits<8) {
      if(n.sign) {
        unsigned up=8-start-bits,down=8-bits;
        if(up && !wide_field_op(f,out,&count,n.origin,LEFT_OP,raw,wide_field_literal(up),-1,false,&raw))goto failed;
        raw.sign=true;
        if(!wide_field_op(f,out,&count,n.origin,RIGHT_OP,raw,wide_field_literal(down),n.result,true,&raw))goto failed;
      } else {
        if(start && !wide_field_op(f,out,&count,n.origin,RIGHT_OP,raw,wide_field_literal(start),-1,false,&raw))goto failed;
        if(!wide_field_op(f,out,&count,n.origin,BITWISEAND,raw,wide_field_literal((1u<<bits)-1),n.result,false,&raw))goto failed;
      }
    }
field_done:
    dbuf_printf(f->diagnostics,"; wide bitfield lowered function=%s key=%d store=%d start=%u bits=%u signed=%d volatile=%d\n",
      currFunc->name,n.origin->key,store,start,bits,fieldSign,n.byteAccess);
  }
  memcpy(f->nodes,out,count*sizeof *out);f->nn=count;Safe_free(out);
  for(int v=0;v<f->nv;++v)f->values[v].reads=f->values[v].weight=0;
  for(int i=0;i<f->nn;++i)for(int v=0;v<f->nv;++v)if(wb_has(f->nodes[i].use,v)) {
    ++f->values[v].reads;f->values[v].weight+=1+4*f->nodes[i].origin->depth;
  }
  return true;
failed:
  Safe_free(out);return false;
}
