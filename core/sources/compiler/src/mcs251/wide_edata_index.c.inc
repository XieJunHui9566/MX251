/* SPDX-License-Identifier: GPL-2.0-or-later
 * A proven EDATA object's relocation supplies the base; WR16 carries the
 * sampled index. Memory operations stay at their original MIR position. */
static void
wmemory_edata_index(wide_function *f,wide_node *n)
{
  bool store=n->op==WM_STORE;
  if(n->addressOp=='+')wload(f,n->addressIndex,16,2);
  else {
    wload(f,n->addressIndex,20,2);wi(f,16,2,0);we(f,"\tsub\twr16,wr20\n");
  }
  we(f,"; wide edata indexed %s symbol=%s offset=%d width=%d volatile=%u\n",
     store?"store":"load",n->addressSymbol->rname,n->addressOffset,n->width,n->byteAccess);
  if(n->width>1) {
    int data=store?wread(f,n->right,20,n->width):f->values[n->result].reg;
    if(data<0)data=20;
    for(int k=0;k<n->width;k+=2) {
      if(store)we(f,"\tmov\t@wr16+(%s+%d),wr%d\n",n->addressSymbol->rname,n->addressOffset+k,data+k);
      else we(f,"\tmov\twr%d,@wr16+(%s+%d)\n",data+k,n->addressSymbol->rname,n->addressOffset+k);
    }
    if(!store)wstore(f,n->result,data);
    return;
  }
  int value=store?n->right.value:n->result;
  int reg=value>=0?f->values[value].reg:-1;
  int byte=reg>=0?reg:11;
  if(store) {
    if(reg<0)wload_byte(f,n->right,11);
    we(f,"\tmov\t@wr16+(%s+%d),r%d\n",n->addressSymbol->rname,n->addressOffset,byte);
  } else {
    we(f,"\tmov\tr%d,@wr16+(%s+%d)\n",byte,n->addressSymbol->rname,n->addressOffset);
    wstore_byte(f,n->result,byte);
  }
}
