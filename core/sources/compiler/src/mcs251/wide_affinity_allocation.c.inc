/* SPDX-License-Identifier: GPL-2.0-or-later
 * Allocate physical slices with copy, destructive-operation and call costs.
 * Affinities choose locations; the original interference graph still decides
 * which locations can coexist. No value identities or ABI pins are changed.
 */
typedef struct {int destination,source,offset;unsigned weight;} wide_affinity;
typedef struct {
  unsigned costs[WV_MAX][32];
  wide_affinity links[WN_MAX*2];int count;
} wide_affinity_costs;

static void
wide_affinity_add(wide_function *f,wide_affinity_costs *costs,int result,wide_operand source,unsigned weight)
{
  if(result<0 || source.value<0 || result==source.value || costs->count>=WN_MAX*2)return;
  int width=f->values[result].width,from=f->values[source.value].width;
  if(source.width!=from || width>from)return;
  costs->links[costs->count++]=(wide_affinity){result,source.value,from-width,weight};
}

static void
wide_affinity_prefer(wide_function *f,wide_affinity_costs *costs,int value,int preferred,unsigned weight)
{
  if(value<0)return;
  int count;const int *choices=wide_register_choices(f->values[value].width,&count);
  for(int k=0;k<count;++k)if(choices[k]!=preferred)costs->costs[value][choices[k]]+=weight;
}

static void
wide_affinity_prepare(wide_function *f,wide_affinity_costs *costs)
{
  memset(costs,0,sizeof *costs);
  for(int i=0;i<f->nn;++i) {
    wide_node *n=&f->nodes[i];unsigned weight=1+4*n->origin->depth;
    if((n->op=='=' || n->op==CAST || n->op==WM_RAW32_COPY) &&
       !n->orderedReadCount && !n->byteAccess)
      wide_affinity_add(f,costs,n->result,n->right,weight);
    else if(n->op=='+' || n->op=='-' || n->op=='^' || n->op=='|' || n->op==BITWISEAND ||
            n->op==LEFT_OP || n->op==RIGHT_OP || n->op==WM_SDIVP2) {
      if(n->result>=0 && n->left.width==f->values[n->result].width)
        wide_affinity_add(f,costs,n->result,n->left,weight);
      if((n->op=='+' || n->op=='^' || n->op=='|' || n->op==BITWISEAND) &&
         n->result>=0 && n->right.width==f->values[n->result].width)
        wide_affinity_add(f,costs,n->result,n->right,weight);
    }
    if(n->op==RETURN && n->left.value>=0 && f->values[n->left.value].preference>=0)
      wide_affinity_prefer(f,costs,n->left.value,f->values[n->left.value].preference,weight);
    if(n->op!=CALL && n->op!=PCALL)continue;
    uint32_t writes=n->op==CALL?wide_call_writes(f,n):UINT32_MAX;
    for(int v=0;v<f->nv;++v)if(wb_has(n->out,v) && !wb_has(n->def,v)) {
      int count;const int *choices=wide_register_choices(f->values[v].width,&count);
      for(int k=0;k<count;++k)if(wide_mask(choices[k],f->values[v].width)&writes)
        costs->costs[v][choices[k]]+=2*weight;
    }
    if(n->op==CALL && !n->ordinaryCall) {
      if(n->args)wide_affinity_prefer(f,costs,n->left.value,8-n->left.width,weight);
      if(n->args>1)wide_affinity_prefer(f,costs,n->right.value,16-n->right.width,weight);
    }
    if(n->op==CALL && n->result>=0 && (!n->ordinaryCall || mcs251InternalReturn(n->callee)))
      wide_affinity_prefer(f,costs,n->result,n->width==1?7:8-n->width,weight);
  }
}

static unsigned
wide_affinity_cost(wide_function *f,const wide_affinity_costs *costs,int value,int reg)
{
  unsigned cost=costs->costs[value][reg];
  for(int k=0;k<costs->count;++k) {
    const wide_affinity *a=&costs->links[k];int other=-1,preferred=-1;
    if(a->destination==value) {other=a->source;preferred=f->values[other].reg+a->offset;}
    else if(a->source==value) {other=a->destination;preferred=f->values[other].reg-a->offset;}
    if(other>=0 && f->values[other].reg>=0 && reg!=preferred)cost+=a->weight;
  }
  return cost;
}

static bool
wide_allocate_affinity(wide_function *f,const wide_affinity_costs *costs,int strategy)
{
  int order[WV_MAX],length=0;bool pending[WV_MAX]={0};
  unsigned degree[WV_MAX]={0};
  for(int v=0;v<f->nv;++v) {
    wide_value *value=&f->values[v];value->reg=value->slot=-1;
    pending[v]=value->pin<0;
    for(int a=0;a<f->nv;++a)if(wb_has(value->edges,a))degree[v]+=f->values[a].width;
  }
  for(int v=0;v<f->nv;++v)if(f->values[v].pin>=0) {
    if(!wide_available(f,v,f->values[v].pin))return false;
    f->values[v].reg=f->values[v].pin;
  }
  /* Two deterministic orders: weighted pressure first, or simplify/select.
   * Simplification removes low-pressure nodes first and colors in reverse. */
  for(int step=0;step<f->nv;++step) {
    int best=-1;uint64_t score=0;
    for(int v=0;v<f->nv;++v)if(pending[v]) {
      int choices;wide_register_choices(f->values[v].width,&choices);
      bool easy=degree[v]<(unsigned)(choices*f->values[v].width);
      uint64_t next=!strategy?UINT64_C(1024)*(f->values[v].weight+1)/(degree[v]+1):
        easy?UINT64_C(1024)*degree[v]+f->values[v].weight:
        (UINT64_C(1)<<40)+UINT64_C(1024)*(f->values[v].weight+1)/(degree[v]+1);
      if(best<0 || (strategy?next<score:next>score)) {best=v;score=next;}
    }
    if(best<0)break;
    pending[best]=false;order[length++]=best;
    if(strategy)for(int v=0;v<f->nv;++v)if(pending[v] && wb_has(f->values[best].edges,v))
      degree[v]-=f->values[best].width;
  }
  for(int k=0;k<length;++k) {
    int v=order[strategy?length-1-k:k],count;unsigned best=0;
    const int *choices=wide_register_choices(f->values[v].width,&count);
    for(int j=0;j<count;++j) {
      int reg=choices[j];if(!wide_available(f,v,reg))continue;
      unsigned cost=wide_affinity_cost(f,costs,v,reg);
      if(f->values[v].reg<0 || cost<best) {f->values[v].reg=reg;best=cost;}
    }
  }
  /* Once both endpoints have locations, improve copy placement without
   * disturbing pinned values, spilling a register value or adding searches. */
  for(int pass=0;pass<2;++pass)for(int k=length;k-->0;) {
    int v=order[k],reg=f->values[v].reg;if(reg<0)continue;
    unsigned best=wide_affinity_cost(f,costs,v,reg);int count;
    const int *choices=wide_register_choices(f->values[v].width,&count);
    for(int j=0;j<count;++j)if(wide_available(f,v,choices[j])) {
      unsigned cost=wide_affinity_cost(f,costs,v,choices[j]);
      if(cost<best){f->values[v].reg=choices[j];best=cost;}
    }
  }
  wide_layout_spills(f);return wide_validate_allocation(f);
}

static void
wide_choose_affinity(wide_function *f,wide_allocation_candidate *selected)
{
  const char *enabled=getenv("MX251_REGISTER_AFFINITY");
  if((enabled && !strcmp(enabled,"0")) || selected->size.unknown)return;
  wide_affinity_costs *costs=Safe_alloc(sizeof *costs);wide_affinity_prepare(f,costs);
  wide_allocation_candidate baseline=*selected;
  wide_loop_costs loops=wide_loop_cost_analyze_context(baseline.text,baseline.length,wide_resolve_cost_operand,f);
  for(int strategy=-1;strategy<2;++strategy) {
    wide_function *trial=Safe_alloc(sizeof *trial);memcpy(trial,f,sizeof *trial);
    const char *reuse=getenv("MX251_SPILL_REGISTER_REUSE");
    trial->reuseSpillRegisters=!reuse || strcmp(reuse,"0");
    wide_allocation_candidate c={0};
    if(strategy<0) {
      struct dbuf_s output;dbuf_init(&output,1024);trial->candidateOutput=&output;
      wide_selection_cost cost={0};mcs251_wide_result result=wide_choose_allocation(trial,&cost);
      Safe_free(cost.form);f->allocationInvalid|=trial->allocationInvalid;Safe_free(trial);
      if(result!=MCS251_WIDE_ACCEPTED){dbuf_destroy(&output);continue;}
      c.length=dbuf_get_length(&output);c.text=Safe_alloc(c.length);
      memcpy(c.text,dbuf_get_buf(&output),c.length);dbuf_destroy(&output);
      c.size=(wide_byte_cost){cost.bytes,cost.unknown};c.heuristic=cost.heuristic;
      c.instructions=cost.instructions;c.spills=cost.spills;c.frame=cost.frame;
    } else {
    if(!wide_allocate_affinity(trial,costs,strategy)) {
      f->allocationInvalid|=trial->allocationInvalid;Safe_free(trial);continue;
    }
    wide_emit_candidate(f,trial,&c);
    }
    bool admissible=!c.size.unknown && c.size.bytes<=baseline.size.bytes &&
      c.heuristic<=baseline.heuristic && c.frame<=baseline.frame;
    int paths=-1;
    if(admissible) {
      wide_loop_costs candidate=wide_loop_cost_analyze_context(c.text,c.length,wide_resolve_cost_operand,f);
      paths=wide_loop_cost_decide(&loops,&candidate);admissible=paths!=0;
    }
    bool better=admissible && (c.size.bytes<selected->size.bytes ||
      (c.size.bytes==selected->size.bytes && c.heuristic<selected->heuristic));
    dbuf_printf(f->diagnostics,"; wide register-affinity function=%s strategy=%d copies=%d old-bytes=%u bytes=%u old-weighted=%u weighted=%u old-frame=%d frame=%d spills=%u paths=%d selected=%u\n",
      currFunc->name,strategy,costs->count,baseline.size.bytes,c.size.bytes,baseline.heuristic,c.heuristic,
      baseline.frame,c.frame,c.spills,paths,better);
    if(better){Safe_free(selected->text);*selected=c;}else Safe_free(c.text);
  }
  Safe_free(costs);
}
