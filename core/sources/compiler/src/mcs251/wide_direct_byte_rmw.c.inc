#include "direct_byte_rmw.h"
static bool wide_rmw_data_byte(const char *name,const void *ctx)
{
 (void)ctx;if(!name[0] || !data)return false;
 for(const set *node=data->syms;node;node=node->next) {
  const symbol *s=node->item;
  if(!s || !s->rname[0] || s->undefined || s->onStack || s->_isparm ||
     !IS_SPEC(s->type) || getSize(s->type)!=1 || IS_BIT(s->type) || IS_BOOL(s->type) ||
     SPEC_ABSA(s->etype) || SPEC_EXTR(s->etype) || isAtomic(s->type) || getAddrspace(s->type))continue;
  if(wide_cost_symbol_offset(name,s->rname,1)==0)return true;
 }
 return false;
}
static void wide_direct_byte_rmw(struct dbuf_s *output)
{
 const char *enable=getenv("MX251_DIRECT_BYTE_RMW");
 if((enable && !strcmp(enable,"0")) || !currFunc || FUNC_SDCCCALL(currFunc->type)!=2 ||
    IFFUNC_ISISR(currFunc->type) || IFFUNC_ISNAKED(currFunc->type) || FUNC_REGBANK(currFunc->type) ||
    options.debug || mcs251InternalReturn(currFunc) || mcs251InternalArgument(currFunc))return;
 const char *text=dbuf_get_buf(output);size_t length=dbuf_get_length(output);
 drmw_line *lines=calloc(DRMW_LINES,sizeof *lines);char **owned=calloc(DRMW_LINES,sizeof *owned);
 if(!lines || !owned){free(lines);free(owned);return;}
 unsigned count=0,sites=0,saved=0;bool overflow=false;
 for(size_t at=0;at<length;) {
  size_t end=at;while(end<length && text[end]!='\n')++end;if(end<length)++end;
  if(count==DRMW_LINES){overflow=true;break;}drmw_parse(&lines[count++],text+at,end-at);at=end;
 }
 if(!overflow)for(unsigned n=count;n;--n) {
  unsigned i=n-1,j,k,before,after;char replacement[512];
  if(!drmw_select(lines,count,i,wide_rmw_data_byte,NULL,&j,&k,&before,&after,replacement,sizeof replacement))continue;
  owned[i]=Safe_strdup(replacement);drmw_parse(&lines[i],owned[i],strlen(owned[i]));
  lines[j].removed=lines[k].removed=true;++sites;saved+=before-after;
 }
 if(sites) {
  struct dbuf_s clean;dbuf_init(&clean,length+128);
  dbuf_printf(&clean,"; mx251-direct-byte-rmw sites=%u saved-bytes=%u allocation=fixed memory=allocated-DATA read=1 write=1 flags=equivalent\n",sites,saved);
  for(unsigned i=0;i<count;++i)if(!lines[i].removed)dbuf_append(&clean,lines[i].raw,lines[i].length);
  dbuf_set_length(output,0);dbuf_append(output,dbuf_get_buf(&clean),dbuf_get_length(&clean));dbuf_destroy(&clean);
 }
 for(unsigned i=0;i<count;++i)Safe_free(owned[i]);free(lines);free(owned);
}
