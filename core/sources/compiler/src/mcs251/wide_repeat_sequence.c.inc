/* SPDX-License-Identifier: GPL-2.0-or-later
 * 有限重复序列压缩：typed MIR 先证明来源/分配/事件，最终 emission
 * 再逐行核对。仅两次循环，保留原尾项恢复 NZ；不改 IR 或寄存器分配。
 * 默认关闭，未知编码/来源/成本保留原文本。计时是独立资格，不称周期。
 */
static bool
wide_repeat_enabled(void)
{
  const char *option=getenv("MX251_REPEAT_SEQUENCE_COMPACTION");
  return option && !strcmp(option,"1");
}

/* 首版只接无帧、无调用的原生 ABI 纯标量函数。原始汇编只准 NOP；
 * 这样既无 A/P 的显式观察者，也无新增 scratch 跨调用/栈效应。 */
static bool
wide_repeat_scope(const wide_function *f)
{
  if(f->ordinaryEntry || f->frame || f->no || f->stackDepth ||
     FUNC_SDCCCALL(currFunc->type)!=4 || IFFUNC_ISISR(currFunc->type) ||
     IFFUNC_ISNAKED(currFunc->type) || IFFUNC_CALLEESAVES(currFunc->type) ||
     IFFUNC_ISBANKEDCALL(currFunc->type) || FUNC_REGBANK(currFunc->type))return false;
  for(int v=0;v<f->nv;++v) {
    const wide_value *value=&f->values[v];
    if(value->width!=4 || value->sign || value->pointer || value->codePointer ||
       value->floating || value->reg<0 ||
       (wide_mask(value->reg,value->width)&MCS251_AB_BYTES))return false;
  }
  for(int i=0;i<f->nn;++i) {
    const wide_node *n=&f->nodes[i];
    if(n->op==WM_SYMBOL_LOAD) {
      if(!n->volatileDataWideAccess || !n->addressSymbol ||
         !wide_volatile_data_object(n->addressSymbol,n->addressSymbol->type))return false;
    } else if(n->op==WM_ORDERED_ASM) {
      for(unsigned k=0;k<n->orderedAsmCount;++k)if(n->orderedAsm[k].operation!=1)return false;
    } else if(n->op!=FUNCTION && n->op!=ENDFUNCTION && n->op!=RECEIVE &&
              n->op!=RETURN && n->op!=LABEL && n->op!=WM_NOP && n->op!='+' && n->op!='^')return false;
  }
  return true;
}

static bool
wide_repeat_pair(const wide_function *f,int i,int *work,int *scratch,int *input)
{
  if(i<0 || i+1>=f->nn)return false;
  const wide_node *load=&f->nodes[i],*add=&f->nodes[i+1];
  if(load->op!=WM_SYMBOL_LOAD || !load->origin || load->width!=4 || load->sign || load->floating ||
     !load->byteAccess || !load->volatileDataWideAccess || load->addressOffset ||
     load->addressObject>=0 || load->codeAccess || load->pointerAccess || !load->addressSymbol ||
     !wide_volatile_data_object(load->addressSymbol,load->addressSymbol->type) ||
     load->result<0 || load->result>=f->nv || add->op!='+' || add->width!=4 ||
     add->sign || add->floating || add->result<0 || add->result>=f->nv ||
     add->left.width!=4 || add->right.width!=4 || add->left.value<0 || add->right.value<0 ||
     load->succ[0]!=i+1 || load->succ[1]>=0 || add->succ[0]!=i+2 || add->succ[1]>=0)return false;
  int other=add->left.value==load->result?add->right.value:
            add->right.value==load->result?add->left.value:-1;
  if(other<0 || other>=f->nv || other==load->result || f->values[load->result].reads!=1 ||
     wb_has(add->out,load->result))return false;
  int a=f->values[other].reg,b=f->values[load->result].reg,c=f->values[add->result].reg;
  if(a<0 || a!=c || b<0 || a%4 || b%4 || a>28 || b>28 ||
     (wide_mask(a,4)&wide_mask(b,4)) ||
     ((wide_mask(a,4)|wide_mask(b,4))&MCS251_AB_BYTES))return false;
  *work=a;*scratch=b;*input=other;return true;
}

/* 标记由已分配 MIR 生成，普通候选成本仍读取原始展开指令。 */
static int
wide_repeat_mark(wide_function *f,int first)
{
  int work,scratch,input;
  if(!wide_repeat_enabled() || !wide_repeat_pair(f,first,&work,&scratch,&input) ||
     !wide_repeat_scope(f))return first;
  int count=1;
  while(first+2*count+1<f->nn) {
    int a,b,previous;int i=first+2*count;
    if(!wide_repeat_pair(f,i,&a,&b,&previous) || a!=work || b!=scratch ||
       f->nodes[i].addressSymbol!=f->nodes[first].addressSymbol ||
       previous!=f->nodes[i-1].result || f->values[previous].reads!=1)break;
    ++count;
  }
  int copies=(count-1)/2;if(copies>25)copies=25;
  if(copies*5-5<16)return first;
  we(f,"; reroll-proposal count=%d work=%d scratch=%d\n",count,work,scratch);
  for(int k=0;k<count;++k) {
    int i=first+2*k;
    we(f,"; reroll-event ordinal=%d load-icode=%d add-icode=%d\n",k,f->nodes[i].origin->key,f->nodes[i+1].origin->key);
    f->emitWeight=1+4*f->nodes[i].origin->depth;wmemory_symbol(f,&f->nodes[i]);
    f->emitWeight=1+4*f->nodes[i+1].origin->depth;wbinary(f,&f->nodes[i+1]);
  }
  we(f,"; reroll-end\n");return first+2*count-1;
}

/* 所有长度来自 dbuf 有效区间。只接受自己发射的完整单行头部。 */
static bool
wide_repeat_header(const char *p,const char *end,unsigned *count,unsigned *work,unsigned *scratch)
{
  return wide_rotate_prefix(&p,end,"; reroll-proposal count=") &&
    wide_rotate_number(&p,end,WN_MAX/2,count) && *count>0 &&
    wide_rotate_prefix(&p,end," work=") && wide_rotate_number(&p,end,28,work) &&
    wide_rotate_prefix(&p,end," scratch=") && wide_rotate_number(&p,end,28,scratch) &&
    p==end && !(*work%4) && !(*scratch%4) && *work!=*scratch && *work!=8 && *scratch!=8;
}

static unsigned
wide_repeat_finalize(wide_function *f,const char *text,size_t length,struct dbuf_s *out,unsigned *saved)
{
  const char *p=text,*end=text+length;unsigned selected=0;*saved=0;
  wide_byte_cost all=wide_emitted_bytes(f,text,length);
  if(all.unknown)return 0;
  while(p<end) {
    const char *begin=wide_rotate_find(p,end,"; reroll-proposal ");
    if(!begin) {dbuf_append(out,p,(size_t)(end-p));break;}
    dbuf_append(out,p,(size_t)(begin-p));
    const char *line=memchr(begin,'\n',(size_t)(end-begin));unsigned count,work,scratch;
    if(!line || !wide_repeat_header(begin,line,&count,&work,&scratch))return 0;
    const char *body=line+1,*finish=wide_rotate_find(body,end,"; reroll-end\n");
    if(!finish)return 0;
    unsigned instructions=0,events=0;char pair[512]={0};size_t pairLength=0;
    for(const char *q=body;q<finish;) {
      const char *nl=memchr(q,'\n',(size_t)(finish-q));if(!nl)return 0;
      if(*q=='\t') {
        size_t size=(size_t)(nl-q+1);unsigned expected=instructions%2;
        if(!expected) {
          char prefix[40];int n=snprintf(prefix,sizeof prefix,"\tmov\tdr%u,(",scratch);
          if(size<=(size_t)n+2 || memcmp(q,prefix,(size_t)n) || nl[-1]!=')')return 0;
        } else {
          char add[48];int n=snprintf(add,sizeof add,"\tadd\tdr%u,dr%u\n",work,scratch);
          if(size!=(size_t)n || memcmp(q,add,size))return 0;
        }
        if(instructions<2) {
          if(pairLength+size>=sizeof pair)return 0;
          memcpy(pair+pairLength,q,size);pairLength+=size;
        } else {
          const char *match=expected?strchr(pair,'\n')+1:pair;
          const char *stop=strchr(match,'\n');
          if(!stop || (size_t)(stop-match+1)!=size || memcmp(q,match,size))return 0;
        }
        ++instructions;
      } else if(*q==';') {
        if((size_t)(nl-q)>=15 && !memcmp(q,"; reroll-event ",15))++events;
      } else if(q!=nl)return 0;
      q=nl+1;
    }
    if(instructions!=2*count || events!=count)return 0;
    wide_byte_cost pc=wide_emitted_bytes(f,pair,pairLength);
    if(pc.unknown || pc.bytes!=5)return 0;
    unsigned copies=(count-1)/2;if(copies>25)copies=25;
    unsigned tail=count-2*copies,benefit=5*copies-5;
    if(!tail || copies<1 || benefit<16 || 5*copies+3>128)return 0;
    /* 已知固定两次回边：新增一条初始化+两次 DJNZ，每到达一次区域
     * 动态新增3条控制指令。每函数总量显式累计，不用静态条数充周期。 */
    dbuf_printf(out,"; wide reroll selected region=%u count=%u copies=%u iterations=2 tail=%u saved=%u added-dynamic=3 counter=A clobber=A,P flags-tail=NZ runtime-qualified=0\n",selected,count,copies,tail,benefit);
    dbuf_append_str(out,"\tmov\ta,#2\n");
    dbuf_printf(out,"__wm_reroll_%s_%u:\n",currFunc->rname,selected);
    for(unsigned j=0;j<copies;++j)dbuf_append(out,pair,pairLength);
    dbuf_printf(out,"\tdjnz\tacc,__wm_reroll_%s_%u\n",currFunc->rname,selected);
    for(unsigned j=0;j<tail;++j)dbuf_append(out,pair,pairLength);
    ++selected;*saved+=benefit;p=finish+13;
  }
  if(*saved>=all.bytes)return 0;
  if(selected)dbuf_printf(f->diagnostics,"; wide reroll final regions=%u saved-bytes=%u added-control-instructions=%u frame=frozen allocation=frozen unknown=0 runtime-qualified=0\n",selected,*saved,3*selected);
  return selected;
}

static mcs251_wide_result
wide_choose_repeat(wide_function *f)
{
  if(!wide_repeat_enabled())return wide_choose_rotate(f,NULL);
  struct dbuf_s baseline,candidate;dbuf_init(&baseline,1024);dbuf_init(&candidate,1024);
  struct dbuf_s *output=f->candidateOutput;f->candidateOutput=&baseline;
  wide_selection_cost old={0};mcs251_wide_result result=wide_choose_rotate(f,&old);
  f->candidateOutput=output;unsigned saved=0,selected=0;
  if(result==MCS251_WIDE_ACCEPTED && !old.unknown)
    selected=wide_repeat_finalize(f,dbuf_get_buf(&baseline),dbuf_get_length(&baseline),&candidate,&saved);
  struct dbuf_s *chosen=selected?&candidate:&baseline;
  if(result==MCS251_WIDE_ACCEPTED)dbuf_append(output,dbuf_get_buf(chosen),dbuf_get_length(chosen));
  /* 唯一调用者是 wide_try_function 的末级发射，不把压缩后静态条数
   * 或动态控制债回流给前面的分配/变换 chooser。原 heuristic 不代表
   * 最终体，故本接口不返回 selection_cost。按真实文本独立数静态条数；
   * 每到达区域新增3条动态控制指令只在专用摘要记录。 */
  if(selected) {
    mx251_effects before=mx251TextEffects(dbuf_get_buf(&baseline),dbuf_get_length(&baseline));
    mx251_effects after=mx251TextEffects(dbuf_get_buf(&candidate),dbuf_get_length(&candidate));
    dbuf_printf(f->diagnostics,"; wide reroll static instructions-before=%u instructions-after=%u bytes-before=%u bytes-after=%u dynamic-extra-per-region=3 downstream-chooser=none\n",before.instructions,after.instructions,old.bytes,old.bytes-saved);
  }
  dbuf_destroy(&candidate);dbuf_destroy(&baseline);return result;
}
