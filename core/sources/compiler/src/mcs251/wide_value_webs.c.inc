/* SPDX-License-Identifier: GPL-2.0-or-later
 * Give disconnected definition/use webs separate allocation identities.
 * Definitions which reach a common live use stay together, including loop
 * backedges and every switch edge. No edge copies or machine nodes are added.
 */
static int
wide_web_root(int *parent,int value)
{
  while(parent[value]!=value) {
    parent[value]=parent[parent[value]];value=parent[value];
  }
  return value;
}

static unsigned
wide_value_webs(wide_function *f)
{
  int entry=-1,originalValues=f->nv;unsigned split=0;
  for(int i=0;i<f->nn;++i)if(f->nodes[i].op==FUNCTION)entry=i;
  if(entry<0)return 0;
  unsigned char reachable[WN_MAX];wide_address_reachable(f,entry,-1,reachable);
  for(int v=0;v<originalValues;++v) {
    if(f->values[v].pin>=0)continue;
    int definitions=0;
    for(int i=0;i<f->nn;++i)if(reachable[i] && wb_has(f->nodes[i].def,v))++definitions;
    if(definitions<2)continue;
    int parent[WN_MAX+1],incoming[WN_MAX],mapped[WN_MAX+1];
    for(int i=0;i<=f->nn;++i)parent[i]=i,mapped[i]=-1;
    for(int i=0;i<f->nn;++i)incoming[i]=-1;
    incoming[entry]=0;
    bool changed;
    do {
      changed=false;
      for(int i=0;i<f->nn;++i)if(reachable[i]) {
        wide_node *n=&f->nodes[i];
        int out=wb_has(n->def,v)?i+1:incoming[i];
        if(out<0)continue;
        for(int k=0;k<2+n->jumpCount;++k) {
          int to=k<2?n->succ[k]:f->jumpSuccessors[n->jumpStart+k-2];
          if(to<0 || !wb_has(f->nodes[to].in,v))continue;
          if(incoming[to]<0) {incoming[to]=out;changed=true;}
          else {
            int a=wide_web_root(parent,incoming[to]),b=wide_web_root(parent,out);
            if(a!=b) {parent[b]=a;changed=true;}
          }
        }
      }
    } while(changed);
    int webs=0;
    for(int i=0;i<f->nn;++i)if(reachable[i]) {
      wide_node *n=&f->nodes[i];
      int ids[2]={wb_has(n->use,v)?incoming[i]:-1,wb_has(n->def,v)?i+1:-1};
      for(int k=0;k<2;++k)if(ids[k]>=0) {
        int root=wide_web_root(parent,ids[k]);
        if(mapped[root]<0)mapped[root]=webs++;
      }
    }
    if(webs<2 || f->nv+webs-1>f->valueLimit)continue;
    for(int i=0;i<=f->nn;++i)if(mapped[i]>=0)
      mapped[i]=mapped[i]?f->nv+mapped[i]-1:v;
    for(int k=1;k<webs;++k)f->values[f->nv++]=f->values[v];
    for(int i=0;i<f->nn;++i)if(reachable[i]) {
      wide_node *n=&f->nodes[i];wide_bits old={{0}};wb_set(&old,v);
      if(wb_has(n->use,v)) {
        wassert(incoming[i]>=0);
        int use=mapped[wide_web_root(parent,incoming[i])];
        wide_operand *operands[WA_MAX+3]={&n->left,&n->right,&n->addressIndex};
        for(unsigned k=0;k<n->stackArgs;++k)operands[k+3]=&n->stackOperands[k];
        for(unsigned k=0;k<3+n->stackArgs;++k)if(operands[k]->value==v)operands[k]->value=use;
        n->use=wb_minus(n->use,old);wb_set(&n->use,use);
      }
      if(wb_has(n->def,v)) {
        int definition=mapped[wide_web_root(parent,i+1)];
        wassert(n->result==v);n->result=definition;
        n->def=wb_minus(n->def,old);wb_set(&n->def,definition);
      }
    }
    split+=webs-1;
    dbuf_printf(f->diagnostics,"; wide value-web value=%d definitions=%d webs=%d\n",v,definitions,webs);
  }
  if(split)wide_reset_allocation(f);
  return split;
}
