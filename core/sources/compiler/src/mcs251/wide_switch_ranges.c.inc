/* SPDX-License-Identifier: GPL-2.0-or-later
 * The imported table selector is already sampled, range checked and normalized.
 * Adjacent equal destinations form intervals. Compare their upper bounds without
 * changing predecessor checks, live values, case bodies or volatile accesses.
 */
static void
wjump_range_target(wide_function *f,wide_node *n,int entry,const char *condition)
{
  int tail=wjump_tail_target(f,f->jumpSuccessors[n->jumpStart+entry]);
  if(tail>=0) {
    int skip=condition?wt(f):0;
    if(condition)wj(f,!strcmp(condition,"jg")?"jle":"jg",skip);
    we(f,"\tejmp\t%s\n",f->nodes[tail].callee->rname);
    if(condition)wl(f,skip);
  } else if(condition)wbranch(f,condition,f->jumpLabels[n->jumpStart+entry]);
  else wj(f,"ejmp",f->jumpLabels[n->jumpStart+entry]);
}

static bool
wjump_ranges(wide_function *f,wide_node *n)
{
  const char *enable=getenv("MX251_SWITCH_RANGES");
  if((enable && !strcmp(enable,"0")) || n->left.width!=1)return false;
  int ends[4],runs=0;
  for(int k=0;k<n->jumpCount;++k) {
    if(k+1<n->jumpCount && f->jumpLabels[n->jumpStart+k]==f->jumpLabels[n->jumpStart+k+1])continue;
    /* At most three comparisons on any path, versus the fixed table dispatch.
     * Larger decision trees need their own execution-cost selection. */
    if(runs==4)return false;
    ends[runs++]=k;
  }
  if(!runs)return false;
  int next=(int)(n-f->nodes)+1;
  bool firstFalls=f->jumpSuccessors[n->jumpStart]==next && wjump_tail_target(f,next)<0;
  bool lastFalls=f->jumpSuccessors[n->jumpStart+n->jumpCount-1]==next && wjump_tail_target(f,next)<0;
  int reg=n->left.value>=0?f->values[n->left.value].reg:-1;
  /* Source-mode encodings: byte CMP=3, inverted condition+EJMP=6, EJMP=4.
   * A sampled byte reload is at most 4 bytes. Table dispatch alone is 16 bytes,
   * plus four per slot; omitting its selector load gives a lower bound. */
  unsigned rangeMax=(runs>1 && reg<0?4:0)+9*(runs-1)+(firstFalls||lastFalls?0:4);
  unsigned tableMin=16+4*n->jumpCount;
  if(rangeMax>=tableMin)return false;
  we(f,"; wide switch ranges entries=%d runs=%d table-min-bytes=%u range-max-bytes=%u\n",
    n->jumpCount,runs,tableMin,rangeMax);
  if(runs>1 && reg<0) {wload_byte(f,n->left,11);reg=11;}
  if(firstFalls) {
    /* High intervals first leave the low interval at the following body. */
    for(int k=runs-1;k>0;--k) {
      we(f,"\tcmp\tr%d,#0x%02x\n",reg,ends[k-1]);
      wjump_range_target(f,n,ends[k],"jg");
    }
  } else {
    for(int k=0;k<runs-1;++k) {
      we(f,"\tcmp\tr%d,#0x%02x\n",reg,ends[k]);
      wjump_range_target(f,n,ends[k],"jle");
    }
    if(!lastFalls)wjump_range_target(f,n,ends[runs-1],NULL);
  }
  return true;
}
