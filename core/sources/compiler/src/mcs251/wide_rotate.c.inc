/* SPDX-License-Identifier: GPL-2.0-or-later
 * 仅折叠已取样的同一无符号值；不读取对象、不移动节点、不合并 volatile。
 * 严格相邻三节点与完整 CFG 支配证明一起排除中途入口及 barrier。 */
static bool
wide_rotate_value(wide_function *f,int index)
{
  if(index<0 || index>=f->nv)return false;
  wide_value *v=&f->values[index];
  return v->width==4 && !v->sign && !v->floating && !v->pointer && !v->codePointer &&
    !v->symbolSnapshot && v->sym && !isAtomic(v->sym->type);
}

static bool
wide_rotate_pure(wide_function *f,wide_node *n)
{
  if(!wide_rotate_value(f,n->result) || n->width!=4 || n->sign || n->floating ||
     n->left.width!=4 || n->left.sign || n->left.floating || n->left.codePointer ||
     n->right.floating || n->right.codePointer || n->addressIndex.value>=0 ||
     n->args || n->stackArgs || n->addressOp || n->byteAccess || n->codeAccess ||
     n->pointerAccess || n->volatileDataWideAccess || n->orderedAsmCount || n->orderedReadCount || n->jumpCount)return false;
  for(int v=0;v<f->nv;++v)
    if(wb_has(n->def,v)!=(v==n->result) ||
       wb_has(n->use,v)!=(v==n->left.value || v==n->right.value))return false;
  return true;
}

static unsigned
wide_constant_rotate(wide_function *f)
{
  if(!mcs251ConstantRotateFusionEnabled)return 0;
  wide_address_dominance *d=Safe_alloc(sizeof *d);memset(d,0,sizeof *d);d->entry=-1;
  unsigned uses[WV_MAX]={0},preds[WN_MAX]={0},changed=0;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    if(n->op==FUNCTION) {if(d->entry>=0) {Safe_free(d);return 0;}d->entry=i;}
    for(int v=0;v<f->nv;++v) {
      if(wb_has(n->def,v)) {++d->definitions[v];d->definition[v]=i;}
      if(wb_has(n->use,v))++uses[v];
    }
    for(int k=0;k<2+n->jumpCount;++k) {
      int next=k<2?n->succ[k]:f->jumpSuccessors[n->jumpStart+k-2];
      if(next>=0 && next<f->nn)++preds[next];
    }
  }
  if(d->entry<0) {Safe_free(d);return 0;}
  wide_address_reachable(f,d->entry,-1,d->reachable);
  for(int i=0;i+2<f->nn;i++) {
    wide_node *a=&f->nodes[i],*b=&f->nodes[i+1],*o=&f->nodes[i+2];
    wide_node *l=a->op==LEFT_OP?a:b,*r=a->op==RIGHT_OP?a:b;
    if(l->op!=LEFT_OP || r->op!=RIGHT_OP || o->op!='|' ||
       !wide_rotate_pure(f,l) || !wide_rotate_pure(f,r) || !wide_rotate_pure(f,o))continue;
    int source=l->left.value,lv=l->result,rv=r->result;
    if(!wide_rotate_value(f,source) || r->left.value!=source || lv==rv || lv==source || rv==source ||
       o->result==source || o->result==lv || o->result==rv ||
       l->right.value>=0 || r->right.value>=0 || l->right.literal<1 || l->right.literal>7 ||
       l->right.literal+r->right.literal!=32 ||
       o->right.width!=4 || o->right.sign ||
       !((o->left.value==lv && o->right.value==rv)||(o->left.value==rv && o->right.value==lv)) ||
       uses[lv]!=1 || uses[rv]!=1 || d->definitions[lv]!=1 || d->definitions[rv]!=1 ||
       a->succ[0]!=i+1 || b->succ[0]!=i+2 || a->succ[1]>=0 || b->succ[1]>=0 ||
       preds[i+1]!=1 || preds[i+2]!=1 ||
       !wide_address_dominates(f,d,source,i) || !wide_address_dominates(f,d,source,i+1) ||
       !wide_address_dominates(f,d,lv,i+2) || !wide_address_dominates(f,d,rv,i+2))continue;
    a->rotateCandidate=true;
    ++changed;i+=2;
  }
  Safe_free(d);return changed;
}
