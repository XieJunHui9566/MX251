/* SPDX-License-Identifier: GPL-2.0-or-later
 * 比较固定分配下的直线调用点。旧指标只计调用方机器码，是完整旧成本的
 * 下界；并非把未知 callee 当作零成本。其它路径和调用文本完全保留。
 * 静态指标不是硬件周期，完整工程的耗时资格必须另行测量。 */
static bool
wide_wordmul_cost(const char *old,size_t oldLength,const char *trial,size_t trialLength,
                 unsigned *oldBytes,unsigned *newBytes)
{
  wide_byte_cost a=wide_function_bytes(old,oldLength),b=wide_function_bytes(trial,trialLength);
  *oldBytes=a.bytes;*newBytes=b.bytes;
  if(a.unknown || b.unknown || b.bytes>=a.bytes)return false;
  wlc_graph *g[2]={calloc(1,sizeof(wlc_graph)),calloc(1,sizeof(wlc_graph))};
  bool valid=g[0] && g[1];unsigned metrics[2][WLC_METRICS]={{0}},calls[2]={0};
  if(valid)valid=wlc_parse_context(g[0],old,oldLength,NULL,NULL) &&
    wlc_parse_context(g[1],trial,trialLength,NULL,NULL);
  for(unsigned k=0;valid && k<2;++k) {
    if(g[k]->nl)valid=false;
    for(unsigned i=0;valid && i<g[k]->ni;++i) {
      wlc_instruction *n=&g[k]->ins[i];
      if(n->branch || (i+1<g[k]->ni && n->next[0]<0))valid=false;
      calls[k]+=n->call;
      for(unsigned j=0;j<WLC_METRICS;++j)metrics[k][j]+=n->metric[j];
    }
  }
  if(calls[0]!=1 || calls[1])valid=false;
  for(unsigned j=0;j<WLC_METRICS;++j)if(metrics[1][j]>metrics[0][j])valid=false;
  free(g[0]);free(g[1]);return valid;
}

/* 提案只由已核对类型的 MIR 调用点生成。注释不参与旧候选成本，直到
 * 所有整函数选择结束才解包；替换只缩短直线块，不改变标签和相对边。
 * 原块/新块再次比较，防止中间文本变换使旧成本证明失效。 */
static bool
wide_wordmul_finalize(struct dbuf_s *output)
{
  static const char begin[]="; mx251-wordmul-begin\n",end[]="; mx251-wordmul-end\n";
  static const char alternative[]="; mx251-wordmul-alternative ",line[]="; mx251-wordmul-line ";
  const char *text=dbuf_get_buf(output);size_t length=dbuf_get_length(output);
  struct dbuf_s clean,old,trial;dbuf_init(&clean,length+64);dbuf_init(&old,256);dbuf_init(&trial,256);
  unsigned state=0;bool valid=true;
  for(size_t at=0;valid && at<length;) {
    size_t next=at;while(next<length && text[next]!='\n')++next;
    if(next<length)++next;size_t size=next-at;const char *p=text+at;
    if(size==sizeof begin-1 && !memcmp(p,begin,size)) {
      if(state){valid=false;break;}state=1;dbuf_set_length(&old,0);dbuf_set_length(&trial,0);
    } else if(size>=sizeof alternative-1 && !memcmp(p,alternative,sizeof alternative-1)) {
      if(state!=1){valid=false;break;}state=2;
    } else if(size>=sizeof line-1 && !memcmp(p,line,sizeof line-1)) {
      if(state!=2){valid=false;break;}dbuf_append(&trial,p+sizeof line-1,size-(sizeof line-1));
    } else if(size==sizeof end-1 && !memcmp(p,end,size)) {
      if(!state){valid=false;break;}
      unsigned a=0,b=0;bool choose=state==2 && wide_wordmul_cost(dbuf_get_buf(&old),dbuf_get_length(&old),
        dbuf_get_buf(&trial),dbuf_get_length(&trial),&a,&b);
      struct dbuf_s *selected=choose?&trial:&old;
      dbuf_printf(&clean,"; mx251-local-word-multiply selected=%u old-bytes=%u new-bytes=%u fixed-allocation=1 lower-bound=caller-only\n",choose,a,b);
      dbuf_append(&clean,dbuf_get_buf(selected),dbuf_get_length(selected));state=0;
    } else if(state==2)valid=false;
    else dbuf_append(state?&old:&clean,p,size);
    at=next;
  }
  valid=valid && !state;
  if(valid){dbuf_set_length(output,0);dbuf_append(output,dbuf_get_buf(&clean),dbuf_get_length(&clean));}
  dbuf_destroy(&clean);dbuf_destroy(&old);dbuf_destroy(&trial);return valid;
}
