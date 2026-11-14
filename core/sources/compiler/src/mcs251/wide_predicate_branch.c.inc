/* SPDX-License-Identifier: GPL-2.0-or-later
 * Consume a single-use masked value directly as a branch predicate. A/B are
 * emitter scratch registers; the source value remains available to other uses.
 * Only an adjacent byte load may join the sequence, with one original read.
 */
static void
wpredicate_byte(wide_function *f,wide_operand input,int index)
{
  wassert(input.value>=0 && index>=0 && index<input.width);
  wide_source_location location=wsource_location(f,input.value);
  if(location.reg<0) {
    char address[32];wa(address,location.slot+index-f->frame-f->stackDepth+1);
    we(f,"\tmov\ta,%s\n",address);
  } else if(location.reg+index<16)we(f,"\tmov\ta,r%d\n",location.reg+index);
  else {
    int reg=location.reg+index;
    we(f,"\tmov\twr10,wr%d\n",reg&~1);
    if(!(reg&1))we(f,"\tmov\ta,b\n");
  }
}

static int
wpredicate_branch(wide_function *f,int first)
{
  int at=first;
  wide_node *load=NULL,*n=&f->nodes[at];
  if(n->width==1 && n->result>=0 && f->values[n->result].reads==1 &&
     (n->op!=WM_PARAM_LOAD || n->left.width==1) &&
     (n->op==WM_SYMBOL_LOAD || n->op==WM_EDATA_LOAD || n->op==WM_PARAM_LOAD || n->op==WM_OBJECT_LOAD)) {
    load=n;
    do {++at;} while(at<f->nn && f->nodes[at].op==WM_NOP);
    if(at>=f->nn)return first;
    n=&f->nodes[at];
  }
  if((n->op!=BITWISEAND && n->op!=GETABIT && n->op!=RIGHT_OP) || n->result<0 ||
     f->values[n->result].reads!=1 || at+1>=f->nn)return first;
  int next=at+1;
  while(next<f->nn && f->nodes[next].op==WM_NOP)++next;
  if(next>=f->nn)return first;
  wide_node *extract=NULL;
  unsigned extractMask=255;
  if(n->op==RIGHT_OP && !n->left.sign && n->width==1 && f->nodes[next].op==BITWISEAND) {
    wide_node *and=&f->nodes[next];wide_operand input=and->left,constant=and->right;
    if(input.value<0) {wide_operand tmp=input;input=constant;constant=tmp;}
    if(and->width!=1 || and->result<0 || input.value!=n->result || constant.value>=0 ||
       f->values[and->result].reads!=1)return first;
    extract=and;extractMask=constant.literal&255;
    do {++next;} while(next<f->nn && f->nodes[next].op==WM_NOP);
    if(next>=f->nn)return first;
  }
  wide_node *branch=&f->nodes[next];
  if(branch->op!=IFX || branch->left.value!=(extract?extract->result:n->result) ||
     wb_has(branch->out,n->result) || (extract && wb_has(branch->out,extract->result)) ||
     !wide_call_plain(f,first+1,next-first))return first;
  wide_operand input=n->left,constant=n->right;
  if(n->op==BITWISEAND && input.value<0) {wide_operand tmp=input;input=constant;constant=tmp;}
  if(input.value<0 || constant.value>=0 ||
     (input.width!=1 && input.width!=2 && input.width!=4))return first;
  unsigned shift=0,mask;
  if(n->op==GETABIT) {
    if(constant.literal>=(unsigned)input.width*8)return first;
    shift=constant.literal/8;mask=1u<<(constant.literal%8);
  } else if(n->op==RIGHT_OP) {
    /* A byte shifted right is nonzero exactly when any retained source bit
     * is set, for both logical and arithmetic shifts. Test the sampled byte. */
    if(n->width!=1 || input.width!=1 || constant.literal>=8)return first;
    mask=(extractMask<<constant.literal)&255;
    if(!mask)return first;
  } else {
    if(n->width<input.width || constant.width!=n->width)return first;
    uint32_t bits=constant.literal;
    if(n->width<4)bits&=(UINT32_C(1)<<(8*n->width))-1;
    if(!bits)return first;
    while(!(bits&255)) {bits>>=8;++shift;}
    if(bits>255 || shift>=(unsigned)input.width)return first;
    mask=(unsigned)bits;
  }
  int index=input.width-1-(int)shift;
  if(load && (input.value!=load->result || input.width!=1 ||
     wb_has(branch->out,load->result)))return first;
  we(f,"; wide predicate branch width=%d byte=%d mask=%02x load=%d\n",input.width,index,mask,load?load->op:0);
  if(load) {
    if(load->op==WM_SYMBOL_LOAD || load->op==WM_EDATA_LOAD)
      we(f,"\tmov\ta,%s(%s+%d)\n",load->op==WM_EDATA_LOAD?"e:":"",load->addressSymbol->rname,load->addressOffset);
    else {
      int offset=load->addressOffset-f->frame-f->stackDepth;
      if(load->op==WM_PARAM_LOAD)offset-=f->preservedBytes;
      else offset+=f->objects[load->addressObject].slot+1;
      char address[32];wa(address,offset);we(f,"\tmov\ta,%s\n",address);
    }
  } else wpredicate_byte(f,input,index);
  if(!(mask&(mask-1))) {
    static const char *bits[]={"acc.0","acc.1","acc.2","acc.3","acc.4","acc.5","acc.6","acc.7"};
    unsigned bit=0;while((1u<<bit)!=mask)++bit;
    wbranch_operand(f,branch->trueBranch?"jb":"jnb",bits[bit],branch->target);
  } else {
    if(mask!=255)we(f,"\tanl\ta,#0x%02x\n",mask);
    wbranch(f,branch->trueBranch?"jnz":"jz",branch->target);
  }
  return next;
}
