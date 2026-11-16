/* SPDX-License-Identifier: GPL-2.0-or-later
 * Byte identities of compiler-private stack slots held in physical registers.
 * Observe every emitted instruction, including scratch operations. Real C
 * memory accesses are never replaced; only explicit virtual spill loads use
 * this cache. Labels, calls and SPX changes start a new straight-line state.
 */
static void
wspill_clear(wide_function *f)
{
  for(int r=0;r<32;++r)f->spillRegisterSlots[r]=-1;
}

static void
wspill_observe(wide_function *f,const char *line,size_t length)
{
  if(!f->reuseSpillRegisters || !length || line[0]==';')return;
  char op[24],args[192],*comma;size_t at=0,k=0;
  while(at<length && isspace((unsigned char)line[at]))++at;
  if(at==length || line[at]==';')return;
  while(at<length && !isspace((unsigned char)line[at]) && k+1<sizeof op)op[k++]=line[at++];
  op[k]=0;k=0;
  if(strchr(op,':') || op[0]=='j' || strstr(op,"jmp") || !strcmp(op,"cjne") ||
     !strcmp(op,"djnz") || strstr(op,"ret")) {wspill_clear(f);return;}
  mx251_effects e=mx251InstructionEffects(line,length,line,length);
  if(e.unknown || e.calls || (e.specialWrites&MX251_SPECIAL_SPX)) {wspill_clear(f);return;}
  while(at<length && line[at]!=';' && k+1<sizeof args) {
    if(!isspace((unsigned char)line[at]))args[k++]=line[at];
    ++at;
  }
  args[k]=0;comma=strchr(args,',');
  mx251_effect_operand dest={0},source={0};bool move=false;int saved[4]={-1,-1,-1,-1},first=-1;
  if(!strcmp(op,"mov") && comma) {
    *comma++=0;
    if(mx251EffectRegister(args,&dest) && mx251EffectRegister(comma,&source) &&
       dest.gpr && source.gpr && dest.width==source.width) {
      int from=0;while(!(source.gpr&(UINT32_C(1)<<from)))++from;
      first=0;while(!(dest.gpr&(UINT32_C(1)<<first)))++first;
      for(unsigned j=0;j<source.width;++j)saved[j]=f->spillRegisterSlots[from+j];
      move=true;
    }
  }
  for(int r=0;r<32;++r)if(e.writes&(UINT32_C(1)<<r))f->spillRegisterSlots[r]=-1;
  if(e.memory&MX251_MEMORY_WRITE) {
    /* Only a numeric SPX store has a known overlap with private slots. */
    if(!strcmp(op,"mov") && comma && !strncmp(args,"@spx",4) &&
       mx251EffectRegister(comma,&source) && source.gpr) {
      char *end;long offset=args[4]?strtol(args+4,&end,0):0;
      if(!args[4])end=args+4;
      if(*end){wspill_clear(f);return;}
      long slot=offset+f->frame+f->stackDepth-1;
      for(int r=0;r<32;++r)if(f->spillRegisterSlots[r]>=slot &&
         f->spillRegisterSlots[r]<slot+(long)source.width)f->spillRegisterSlots[r]=-1;
    } else {wspill_clear(f);return;}
  }
  if(move)for(unsigned j=0;j<dest.width;++j)f->spillRegisterSlots[first+j]=saved[j];
}

static void
wspill_remember(wide_function *f,int slot,int reg,int width)
{
  if(!f->reuseSpillRegisters || slot<0 || slot+width>f->frame ||
     !mcs251RegisterEncodable(reg,width))return;
  for(int j=0;j<width;++j)f->spillRegisterSlots[reg+j]=slot+j;
}

static int
wspill_find(wide_function *f,int slot,int width)
{
  if(!f->reuseSpillRegisters || slot<0 || slot+width>f->frame)return -1;
  for(int reg=0;reg<32;reg+=width) {
    if(!mcs251RegisterEncodable(reg,width))continue;
    bool equal=true;
    for(int j=0;j<width;++j)if(f->spillRegisterSlots[reg+j]!=slot+j)equal=false;
    if(equal){++f->spillRegisterReuses;return reg;}
  }
  return -1;
}
