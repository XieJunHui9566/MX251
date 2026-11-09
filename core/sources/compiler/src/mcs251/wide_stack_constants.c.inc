/* SPDX-License-Identifier: GPL-2.0-or-later
 * Pack consecutive nonvolatile literal stores to a stack object into words.
 * The run has no intervening reads or entries. Reuse WR20 while its literal
 * remains unchanged; an odd final byte uses A. This also covers nonzero data.
 */
static bool
wstack_constant(wide_node *n)
{
  return (n->op==WM_STORE || n->op==WM_OBJECT_STORE) && n->addressObject>=0 &&
    !n->byteAccess && !n->pointerAccess && !n->codeAccess && !n->addressOp &&
    !n->orderedReadCount && !n->orderedAsmCount && n->result<0 &&
    wb_empty(n->use) && wb_empty(n->def) && n->right.value<0 &&
    !n->right.floating && n->right.width==n->width &&
    (n->width==1 || n->width==2 || n->width==4);
}

static int
wstack_constant_run(wide_function *f,int first)
{
  wide_node *head=&f->nodes[first];
  if(!wstack_constant(head))return first;
  unsigned char bytes[WN_MAX*4];unsigned length=0;int last=first;
  for(int i=first;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    if(i!=first && !wide_call_plain(f,i,1))break;
    if(n->op==WM_NOP)continue;
    if(!wstack_constant(n) || n->addressObject!=head->addressObject ||
       n->addressOffset!=head->addressOffset+(int)length)break;
    for(int k=n->width;k-->0;)bytes[length++]=(n->right.literal>>(8*k))&255;
    last=i;
  }
  if(last==first)return first;
  int base=f->objects[head->addressObject].slot+head->addressOffset-f->frame-f->stackDepth+1;
  we(f,"; wide stack constants object=%d offset=%d bytes=%u\n",head->addressObject,head->addressOffset,length);
  int literal=-1;unsigned offset=0;
  for(;offset+1<length;offset+=2) {
    int word=(bytes[offset]<<8)|bytes[offset+1];char address[32];wa(address,base+offset);
    if(word!=literal)we(f,"\tmov\twr20,#0x%04x\n",word);
    we(f,"\tmov\t%s,wr20\n",address);literal=word;
  }
  if(offset<length) {
    char address[32];wa(address,base+offset);
    we(f,"\tmov\ta,#0x%02x\n\tmov\t%s,a\n",bytes[offset],address);
  }
  return last;
}
