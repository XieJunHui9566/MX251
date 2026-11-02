/* SPDX-License-Identifier: GPL-2.0-or-later
 * 有界物理重着色：保留同一 MIR、冲突图、ABI pin 和 scratch 保留集。
 * 深度/尝试上限固定；失败完整回滚，绝不把部分分配交给发射器。
 */
static bool
wide_recolor_place(wide_function *f,int value,int reg,unsigned depth,
  wide_bits active,unsigned *budget)
{
  if(!*budget)return false;
  --*budget;
  wide_value *v=&f->values[value];
  if(v->pin>=0 && v->pin!=reg)return false;
  int saved[WV_MAX];for(int i=0;i<f->nv;++i)saved[i]=f->values[i].reg;
  v->reg=reg;wb_set(&active,value);
  for(int b=0;b<f->nv;++b) {
    wide_value *other=&f->values[b];
    if(!wb_has(v->edges,b) || other->reg<0 ||
       !(wide_mask(reg,v->width)&wide_mask(other->reg,other->width)))continue;
    bool moved=false;
    if(depth && !wb_has(active,b) && other->pin<0) {
      int count;const int *choices=wide_register_choices(other->width,&count);
      for(int k=0;k<count && *budget;++k) {
        int next=choices[k];bool blocked=next==other->reg;
        for(int a=0;!blocked && a<f->nv;++a)if(wb_has(active,a) && wb_has(other->edges,a) &&
          (wide_mask(next,other->width)&wide_mask(f->values[a].reg,f->values[a].width)))blocked=true;
        if(!blocked && wide_recolor_place(f,b,next,depth-1,active,budget)) {moved=true;break;}
      }
    }
    if(!moved) {
      for(int i=0;i<f->nv;++i)f->values[i].reg=saved[i];
      return false;
    }
  }
  return true;
}

static void
wide_choose_recolor(wide_function *f,wide_function *original,wide_allocation_candidate *selected)
{
  if(!selected->spills || selected->size.unknown)return;
  wide_allocation_candidate baseline=*selected;
  /* 与静态准入使用同一不可变原始分配，避免中间候选改变比较基准。
   * 缓存成本而非保存text指针：选中首个候选后原文本会释放。 */
  wide_loop_costs baselineLoops=wide_loop_cost_analyze(baseline.text,baseline.length);
  unsigned attempts=0,emitted=0,accepted=0;
  /* 每个原溢出值只尝试现有物理切片；全函数最多 64 个候选。
   * 不累计被拒更改，所有候选从同一旧分配开始。 */
  for(int v=0;v<original->nv && attempts<64;++v) {
    if(original->values[v].reg>=0 || original->values[v].pin>=0)continue;
    int count;const int *choices=wide_register_choices(original->values[v].width,&count);
    for(int k=0;k<count && attempts<64;++k) {
      ++attempts;
      wide_function *trial=Safe_alloc(sizeof *trial);memcpy(trial,original,sizeof *trial);
      unsigned budget=1024;wide_bits active={{0}};
      bool recolored=wide_recolor_place(trial,v,choices[k],3,active,&budget);
      if(!recolored) {Safe_free(trial);continue;}
      for(int a=0;a<trial->nv;++a)trial->values[a].slot=-1;
      wide_layout_spills(trial);
      if(!wide_validate_allocation(trial)) {
        original->allocationInvalid|=trial->allocationInvalid;Safe_free(trial);continue;
      }
      unsigned moved=0;for(int a=0;a<trial->nv;++a)moved+=trial->values[a].reg!=original->values[a].reg;
      wide_allocation_candidate c={0};wide_emit_candidate(f,trial,&c);++emitted;
      bool admissible=!c.size.unknown && c.size.bytes<=baseline.size.bytes &&
        c.heuristic<=baseline.heuristic && c.frame<=baseline.frame &&
        (c.size.bytes<baseline.size.bytes || c.heuristic<baseline.heuristic || c.frame<baseline.frame);
      /* 所有重着色候选独立对照同一原始分配，不以候选间路径补偿决定准入。 */
      if(admissible) {
        wide_loop_costs candidateLoops=wide_loop_cost_analyze(c.text,c.length);
        int loopDecision=wide_loop_cost_decide(&baselineLoops,&candidateLoops);
        dbuf_printf(f->diagnostics,"; wide loop-cost kind=recolor function=%s decision=%d legacy-fallback=%u\n",currFunc->name,loopDecision,loopDecision<0);
        if(loopDecision==0)admissible=false;
      }
      unsigned objective=optimize.codeSize?c.size.bytes:c.heuristic;
      unsigned oldObjective=optimize.codeSize?selected->size.bytes:selected->heuristic;
      bool better=admissible && (objective<oldObjective ||
        (objective==oldObjective && c.frame<selected->frame));
      dbuf_printf(f->diagnostics,"; wide recolor function=%s value=%d reg=%d mode=%s moved=%u search=%u bytes=%u weighted=%u frame=%d spills=%u admissible=%u chosen=%u\n",
        currFunc->name,v,choices[k],"move",moved,1024-budget,
        c.size.bytes,c.heuristic,c.frame,c.spills,admissible,better);
      if(better) {Safe_free(selected->text);*selected=c;++accepted;}
      else Safe_free(c.text);
    }
  }
  dbuf_printf(f->diagnostics,"; wide recolor summary function=%s attempts=%u emitted=%u improvements=%u old-bytes=%u bytes=%u old-weighted=%u weighted=%u old-frame=%d frame=%d old-spills=%u spills=%u\n",
    currFunc->name,attempts,emitted,accepted,baseline.size.bytes,selected->size.bytes,
    baseline.heuristic,selected->heuristic,baseline.frame,selected->frame,baseline.spills,selected->spills);
}

/* 无 spill 不等于没有寄存器布局成本。高组宽寄存器不能逐字节寻址，
 * 有序栈字节访问和普通 ABI 的三字节指针运输可能需要额外拆装。
 * 只搜索直线 MIR 的既有物理切片，
 * 不改访问宽度、顺序、volatile/union 保护、调用 ABI 或 scratch 保留集。
 * 每个候选均从同一原分配重建并完整发射；未知成本一律不准入。 */
static void
wide_choose_byte_recolor(wide_function *f,wide_function *original,wide_allocation_candidate *selected)
{
  if(selected->spills || selected->size.unknown)return;
  for(int v=0;v<original->nv;++v)if(original->values[v].reg<0)return;
  wide_bits roots={{0}};
  for(int i=0;i<original->nn;++i) {
    wide_node *n=&original->nodes[i];
    if(n->op==IFX || n->op==GOTO || n->op==JUMPTABLE || n->jumpCount)return;
    if(mcs251WideOrdinaryEnabled && n->op==CALL && n->ordinaryCall) {
      int values[2]={n->args?n->left.value:-1,
        wb_has(n->out,n->result)?n->result:-1};
      for(unsigned a=0;a<2;++a)if(values[a]>=0) {
        wide_value *v=&original->values[values[a]];
        if(v->pointer && v->width==4 && v->pin<0 && v->reg>=16)wb_set(&roots,values[a]);
      }
    }
    if((n->op!=WM_LOAD && n->op!=WM_STORE) || !n->byteAccess ||
       n->addressObject<0 || n->pointerAccess || n->width<=1)continue;
    int v=n->op==WM_STORE?n->right.value:n->result;
    if(v>=0 && original->values[v].pin<0 && original->values[v].reg>=16)wb_set(&roots,v);
  }
  wide_allocation_candidate baseline=*selected;
  wide_loop_costs baselineLoops=wide_loop_cost_analyze(baseline.text,baseline.length);
  if(baselineLoops.unknown)return;
  unsigned attempts=0,emitted=0,accepted=0;
  for(int v=0;v<original->nv && attempts<64;++v)if(wb_has(roots,v)) {
    int count;const int *choices=wide_register_choices(original->values[v].width,&count);
    for(int k=0;k<count && attempts<64;++k)for(unsigned group=0;group<2 && attempts<64;++group) {
      int reg=choices[k],width=original->values[v].width;
      if(reg+width>16 || (wide_mask(reg,width)&wide_mask(10,2)))continue;
      /* 同一原物理切片上互不活跃的根值可一起迁移；仍从不可变原分配
       * 开始，不能累计已拒绝方案。单值方案也保留，整组失败完整丢弃。 */
      wide_bits targets={{0}};wb_set(&targets,v);unsigned targetCount=1;
      if(group)for(int a=v+1;a<original->nv;++a)if(wb_has(roots,a) &&
        original->values[a].reg==original->values[v].reg && original->values[a].width==width) {
        wb_set(&targets,a);++targetCount;
      }
      if(group && targetCount==1)continue;
      ++attempts;
      wide_function *trial=Safe_alloc(sizeof *trial);memcpy(trial,original,sizeof *trial);
      unsigned budget=1024;bool recolored=true;
      for(int a=0;a<trial->nv && recolored;++a)if(wb_has(targets,a))
        recolored=wide_recolor_place(trial,a,reg,3,targets,&budget);
      if(!recolored) {Safe_free(trial);continue;}
      if(!wide_validate_allocation(trial)) {
        original->allocationInvalid|=trial->allocationInvalid;Safe_free(trial);continue;
      }
      unsigned moved=0;for(int a=0;a<trial->nv;++a)moved+=trial->values[a].reg!=original->values[a].reg;
      wide_allocation_candidate c={0};wide_emit_candidate(f,trial,&c);++emitted;
      wide_loop_costs candidateLoops=wide_loop_cost_analyze(c.text,c.length);
      int loopDecision=wide_loop_cost_decide(&baselineLoops,&candidateLoops);
      bool admissible=!c.size.unknown && !c.spills && c.frame<=baseline.frame &&
        c.size.bytes<=baseline.size.bytes && c.heuristic<=baseline.heuristic &&
        (c.size.bytes<baseline.size.bytes || c.heuristic<baseline.heuristic) && loopDecision==1;
      unsigned objective=optimize.codeSize?c.size.bytes:c.heuristic;
      unsigned oldObjective=optimize.codeSize?selected->size.bytes:selected->heuristic;
      bool better=admissible && (objective<oldObjective ||
        (objective==oldObjective && c.frame<selected->frame));
      dbuf_printf(f->diagnostics,"; wide byte-recolor function=%s value=%d reg=%d targets=%u moved=%u search=%u bytes=%u weighted=%u frame=%d spills=%u loop-decision=%d admissible=%u chosen=%u\n",
        currFunc->name,v,reg,targetCount,moved,1024-budget,c.size.bytes,c.heuristic,c.frame,c.spills,loopDecision,admissible,better);
      if(better) {Safe_free(selected->text);*selected=c;++accepted;}
      else Safe_free(c.text);
    }
  }
  if(attempts)dbuf_printf(f->diagnostics,"; wide byte-recolor summary function=%s attempts=%u emitted=%u improvements=%u old-bytes=%u bytes=%u old-weighted=%u weighted=%u old-frame=%d frame=%d\n",
    currFunc->name,attempts,emitted,accepted,baseline.size.bytes,selected->size.bytes,
    baseline.heuristic,selected->heuristic,baseline.frame,selected->frame);
}
