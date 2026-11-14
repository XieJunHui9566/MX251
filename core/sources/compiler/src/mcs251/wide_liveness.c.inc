/* SPDX-License-Identifier: GPL-2.0-or-later
 * Build interference from live sets, rather than examining every pair of
 * virtual values at every node. Keep the same CFG fixed point and the same
 * live-in/live-in and definition/live-out conflicts, including switch edges.
 */
static void
wide_liveness(wide_function *f)
{
  bool changed;
  do {
    changed=false;
    for(int i=f->nn;i-->0;) {
      wide_node *n=&f->nodes[i];wide_bits out={{0}},in;
      for(int k=0;k<2;++k)if(n->succ[k]>=0)out=wb_or(out,f->nodes[n->succ[k]].in);
      for(int k=0;k<n->jumpCount;++k)
        out=wb_or(out,f->nodes[f->jumpSuccessors[n->jumpStart+k]].in);
      in=wb_or(n->use,wb_minus(out,n->def));
      changed|=!wb_equal(n->in,in) || !wb_equal(n->out,out);n->in=in;n->out=out;
    }
  } while(changed);
  int words=(f->nv+63)/64;
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];
    for(int a=0;a<f->nv;++a) {
      wide_bits *edges=&f->values[a].edges;
      if(wb_has(n->in,a))for(int w=0;w<words;++w)edges->w[w]|=n->in.w[w];
      if(wb_has(n->def,a))for(int w=0;w<words;++w)edges->w[w]|=n->out.w[w];
      if(wb_has(n->out,a))for(int w=0;w<words;++w)edges->w[w]|=n->def.w[w];
    }
  }
  /* A value never interferes with itself, including read/modify/write nodes.
   * Other existing edges remain, as in the original incremental analysis. */
  for(int a=0;a<f->nv;++a)f->values[a].edges.w[a/64]&=~(UINT64_C(1)<<(a%64));
}
