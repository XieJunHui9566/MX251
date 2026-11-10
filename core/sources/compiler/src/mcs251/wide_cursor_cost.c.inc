/* Compare transformed straight-line loops independently, including when they
 * live inside an outer event loop. All other emitted instructions must match;
 * only emitter-private label numbering and comments are normalized. */
static void
wide_cursor_normalize(struct dbuf_s *out,const char *text,size_t length)
{
  char names[128][160];unsigned count=0;
  for(size_t at=0;at<length;) {
    size_t end=at;while(end<length && text[end]!='\n')++end;
    size_t p=at;while(p<end && isspace((unsigned char)text[p]))++p;
    for(;p<end && text[p]!=';';++p) {
      if(isspace((unsigned char)text[p]))continue;
      if(p+5<end && !strncmp(text+p,"__wm_",5)) {
        size_t last=p;while(last<end && (isalnum((unsigned char)text[last]) || text[last]=='_'))++last;
        size_t digits=last;while(digits>p && isdigit((unsigned char)text[digits-1]))--digits;
        if(digits<last && strtoul(text+digits,NULL,10)>=1000000 && last-p<160) {
          char name[160];memcpy(name,text+p,last-p);name[last-p]=0;
          unsigned n=0;while(n<count && strcmp(names[n],name))++n;
          if(n==count && count<128)strcpy(names[count++],name);
          if(n<count) {dbuf_printf(out,"@private%u",n);p=last-1;continue;}
        }
      }
      dbuf_append(out,text+p,1);
    }
    if(p>at)dbuf_append(out,"\n",1);
    at=end+1;
  }
}

static bool
wide_cursor_extract(const char *text,size_t length,int label,
                    struct dbuf_s *loop,size_t *begin,size_t *end)
{
  char name[160],definition[164];
  SNPRINTF(name,sizeof name,"__wm_%s_%d",currFunc->rname,label);
  SNPRINTF(definition,sizeof definition,"%s:",name);
  const char *start=strstr(text,definition);
  if(!start)return false;
  *begin=(size_t)(start-text);
  bool found=false;
  for(size_t at=*begin;at<length;) {
    size_t finish=at;while(finish<length && text[finish]!='\n')++finish;
    const char *p=text+at;while(p<text+finish && isspace((unsigned char)*p))++p;
    char op[16],target[160];
    if(sscanf(p,"%15s %159s",op,target)==2 && !strcmp(target,name) &&
       (wlc_conditional(op) || !strcmp(op,"ejmp") || !strcmp(op,"sjmp"))) {
      *end=finish+1;found=true;
      /* Long conditional backedges have a private fallthrough label after
       * the jump. Include that label, then supply the local exit below. */
      if(!wlc_conditional(op)) {
        size_t q=*end;while(q<length && text[q]!='\n')++q;
        char next[164];size_t n=q-*end;
        if(n && n<sizeof next) {
          memcpy(next,text+*end,n);next[n]=0;
          if(strstr(next,"__wm_") && strchr(next,':'))*end=q+1;
        }
      }
      break;
    }
    at=finish+1;
  }
  if(!found)return false;
  dbuf_append(loop,text+*begin,*end-*begin);dbuf_append_str(loop,"\teret\n");
  return true;
}

static int
wide_cursor_cost_compare(wide_function *old,wide_function *trial,
                         struct dbuf_s *before,struct dbuf_s *after)
{
  struct dbuf_s rest[2],normalized[2];int answer=1;
  for(int t=0;t<2;++t) {dbuf_init(&rest[t],1024);dbuf_init(&normalized[t],1024);}
  size_t consumed[2]={0,0};
  /* Transform records loops from last to first; emission is forward. */
  for(int k=trial->cursorCount-1;k>=0 && answer==1;--k) {
    struct dbuf_s parts[2];wide_loop_costs costs[2];
    for(int t=0;t<2;++t) {
      dbuf_init(&parts[t],512);
      struct dbuf_s *code=t?after:before;size_t begin,end;
      const char *text=dbuf_c_str(code);size_t length=dbuf_get_length(code);
      if(!wide_cursor_extract(text,length,trial->cursorLabels[k],&parts[t],&begin,&end) ||
         begin<consumed[t]) {answer=-1;continue;}
      dbuf_append(&rest[t],text+consumed[t],begin-consumed[t]);consumed[t]=end;
      costs[t]=wide_loop_cost_analyze_context_calls_mode(dbuf_get_buf(&parts[t]),dbuf_get_length(&parts[t]),
        wide_resolve_cost_operand,t?trial:old,NULL,NULL,1);
    }
    if(answer==1)answer=wide_loop_cost_decide(&costs[0],&costs[1]);
    for(int t=0;t<2;++t)dbuf_destroy(&parts[t]);
  }
  if(answer==1)for(int t=0;t<2;++t) {
    struct dbuf_s *code=t?after:before;
    dbuf_append(&rest[t],dbuf_get_buf(code)+consumed[t],dbuf_get_length(code)-consumed[t]);
    wide_cursor_normalize(&normalized[t],dbuf_get_buf(&rest[t]),dbuf_get_length(&rest[t]));
  }
  if(answer==1 && (dbuf_get_length(&normalized[0])!=dbuf_get_length(&normalized[1]) ||
     memcmp(dbuf_get_buf(&normalized[0]),dbuf_get_buf(&normalized[1]),dbuf_get_length(&normalized[0]))))answer=-1;
  for(int t=0;t<2;++t) {dbuf_destroy(&rest[t]);dbuf_destroy(&normalized[t]);}
  return answer;
}
