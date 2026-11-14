/* SPDX-License-Identifier: GPL-2.0-or-later */
static void
wtable_jump(wide_function *f,int target)
{
  wide_branch_site site={target,NULL,dbuf_get_length(&f->output),0,f->emitUpper,0,f->emitBarrier,f->emitWeight,NULL};
  wj(f,"ejmp",target);site.end=dbuf_get_length(&f->output);site.upperEnd=f->emitUpper;
  if(f->branchCount<(unsigned)f->nodeLimit)f->branches[f->branchCount++]=site;else f->relaxDisabled=true;
}

static void
wtable_entry(wide_function *f,wide_node *n,wide_table_entry entry)
{
  int reg=f->values[n->result].reg;
  if(n->width==1) {
    if(reg<0)reg=11;
    we(f,"\tmov\tr%d,#0x%02lx\n",reg,(unsigned long)(entry.literal.literal&255));
    wstore_byte(f,n->result,reg);
  } else {
    if(reg<0)reg=16;
    if(entry.symbol)
      we(f,"\tmov\ta,#((%s%+d)>>16)\n\tmovz\twr%d,a\n\tmov\twr%d,#(%s%+d)\n",
         entry.symbol->rname,entry.offset,reg,reg+2,entry.symbol->rname,entry.offset);
    else wi(f,reg,n->width,entry.literal.literal);
    wstore_known(f,n->result,reg,true);
  }
}

static void
wconstant_lookup(wide_function *f,wide_node *n)
{
  wide_constant_table *table=&f->constantTables[n->constantTable];
  we(f,"; wide constant lookup entries=%u width=%d\n",table->count,n->width);
  if(n->left.value<0) {wtable_entry(f,n,table->entries[n->left.literal]);return;}
  int labels[WTABLE_ENTRIES],end=wt(f),reg=-1;
  if(n->left.width==1)wload_byte(f,n->left,11);
  else reg=wread(f,n->left,20,2); /* proven index is at most seven */
  for(unsigned k=0;k+1<table->count;++k) {
    labels[k]=wt(f);
    if(reg<0 && !k)wbranch(f,"jz",labels[k]);
    else {
      if(reg<0)we(f,"\tcmp\tr11,#%u\n",k);
      else we(f,"\tcmp\twr%d,#%u\n",reg,k);
      wbranch(f,"je",labels[k]);
    }
  }
  wtable_entry(f,n,table->entries[table->count-1]);wtable_jump(f,end);
  for(unsigned k=0;k+1<table->count;++k) {
    wl(f,labels[k]);wtable_entry(f,n,table->entries[k]);
    if(k+2<table->count)wtable_jump(f,end);
  }
  wl(f,end);
}
