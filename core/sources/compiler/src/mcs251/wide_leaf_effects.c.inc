/* SPDX-License-Identifier: GPL-2.0-or-later
 * 实验性构建反馈：来源/输入/对象一致性由两遍构建驱动验证。
 * 裸命令行参数是显式编译契约，不能把用户给的掩码当成已验证事实。 */
extern char *mcs251LeafEffectsFile;
extern char *mcs251PreserveFile;
typedef struct wide_leaf_effect {
  char name[SDCC_NAME_MAX+1];unsigned abi,dpxWrites,bitsWrites;uint32_t writes;bool local;
  struct wide_leaf_effect *next;
} wide_leaf_effect;
static wide_leaf_effect *wideLeafEffects;
static wide_leaf_effect *widePreserves;
static int wide_effect_line(FILE *file,char *line,size_t capacity) {
  size_t n=0;int ch;
  while((ch=fgetc(file))!=EOF && ch!='\n') {
    if(ch==0 || ch>127 || n+1>=capacity)return -1;
    line[n++]=(char)ch;
  }
  if(ferror(file))return -1;
  if(ch==EOF && !n)return 0;
  if(n && line[n-1]=='\r')--n;
  line[n]=0;return 1;
}
static void wide_load_effects(const char *path,bool preserve) {
  if(!path)return;
  FILE *file=fopen(path,"rb");char line[SDCC_NAME_MAX+64];unsigned count=0;
  wide_leaf_effect **list=preserve?&widePreserves:&wideLeafEffects;
  if(!file)goto invalid;
  if(wide_effect_line(file,line,sizeof line)!=1 ||
     (preserve?strcmp(line,"MX251-CALLEE-PRESERVE-1"):
     (strcmp(line,"MX251-LEAF-EFFECTS-1") && strcmp(line,"MX251-CALL-CLOBBERS-1") && strcmp(line,"MX251-CALL-CLOBBERS-2") && strcmp(line,"MX251-CALL-CLOBBERS-3") && strcmp(line,"MX251-CALL-CLOBBERS-4"))))goto invalid;
  bool bits=!preserve && !strcmp(line,"MX251-CALL-CLOBBERS-4");
  bool dpx=bits || (!preserve && !strcmp(line,"MX251-CALL-CLOBBERS-3"));
  bool scoped=preserve || dpx || !strcmp(line,"MX251-CALL-CLOBBERS-2");
  int status;
  while((status=wide_effect_line(file,line,sizeof line))==1) {
    char *scope=scoped?strtok(line," \t"):NULL;
    char *name=strtok(scoped?NULL:line," \t"),*abi=strtok(NULL," \t"),*mask=strtok(NULL," \t");
    char *dpxMask=dpx?strtok(NULL," \t"):NULL,*bitsMask=bits?strtok(NULL," \t"):NULL,*extra=strtok(NULL," \t");
    if(dpx && (!dpxMask || strlen(dpxMask)!=1 || *dpxMask<'0' || *dpxMask>'3'))goto invalid;
    if(bits && (!bitsMask || strlen(bitsMask)!=1 || *bitsMask<'0' || *bitsMask>'1'))goto invalid;
    if(scoped && (!scope || (strcmp(scope,"local") && strcmp(scope,"global"))))goto invalid;
    bool local=scoped && !strcmp(scope,"local");
    if(!name || !abi || !mask || extra || strlen(name)>SDCC_NAME_MAX || strlen(mask)!=8 ||
       (strcmp(abi,"2") && strcmp(abi,"4")) || ++count>4096)goto invalid;
    if(!isalpha((unsigned char)*name) && *name!='_' && *name!='$' && *name!='.')goto invalid;
    for(char *p=name;*p;++p)if(!isalnum((unsigned char)*p) && *p!='_' && *p!='$' && *p!='.')goto invalid;
    for(char *p=mask;*p;++p)if(!isxdigit((unsigned char)*p))goto invalid;
    for(wide_leaf_effect *p=*list;p;p=p->next)if(p->local==local && !strcmp(p->name,name))goto invalid;
    wide_leaf_effect *entry=Safe_alloc(sizeof *entry);strcpy(entry->name,name);
    entry->abi=(unsigned)(*abi-'0');entry->writes=(uint32_t)strtoul(mask,NULL,16);entry->local=local;
    entry->dpxWrites=dpx?(unsigned)(*dpxMask-'0'):3u;
    entry->bitsWrites=bits?(unsigned)(*bitsMask-'0'):1u;
    if(preserve) {
      uint32_t bits=entry->writes;
      if(!bits || (bits&~UINT32_C(0xff00f300)))goto invalid;
      const uint32_t groups[]={UINT32_C(0x00000300),UINT32_C(0x0000f000),UINT32_C(0x0f000000),UINT32_C(0xf0000000)};
      for(unsigned i=0;i<4;++i)if((bits&groups[i]) && (bits&groups[i])!=groups[i])goto invalid;
    }
    entry->next=*list;*list=entry;
  }
  if(status<0)goto invalid;
  fclose(file);return;
invalid:
  if(file)fclose(file);
  werror(E_BAD_INT_ARGUMENT,preserve?"--mcs251-callee-preserve: invalid register groups":"--mcs251-leaf-effects: invalid feedback contract");exit(EXIT_FAILURE);
}
void mcs251LoadLeafEffects(void) {
  wide_load_effects(mcs251LeafEffectsFile,false);
  wide_load_effects(mcs251PreserveFile,true);
}
static uint32_t wide_preserve_registers(symbol *s) {
  for(wide_leaf_effect *p=widePreserves;p;p=p->next)
    if(p->local==!!IS_STATIC(s->etype) && p->abi==FUNC_SDCCCALL(s->type) && !strcmp(p->name,s->rname))return p->writes;
  return 0;
}
static bool wide_leaf_writes(symbol *callee,uint32_t *writes) {
  /* v2 local facts are supplied in a translation-unit-specific feedback file. */
  if(!callee)return false;
  for(wide_leaf_effect *p=wideLeafEffects;p;p=p->next)
    if(p->local==!!IS_STATIC(callee->etype) && p->abi==FUNC_SDCCCALL(callee->type) && !strcmp(p->name,callee->rname)) {*writes=p->writes;return true;}
  return false;
}

/* 普通后端与宽后端消费相同的显式反馈；绑定和最终上界由构建入口核对。 */
bool mcs251CallClobberWrites(symbol *callee, unsigned long *writes) {
  uint32_t mask;
  if(!wide_leaf_writes(callee,&mask))return false;
  *writes=(unsigned long)mask;return true;
}
/* Low DPTR and high DPXL are separate may-write bits in v3 feedback. */
unsigned mcs251CallDpxWrites(symbol *callee) {
  const char *enable=getenv("MX251_DPX_CALL_EFFECTS");
  if(!callee || (enable && !strcmp(enable,"0")))return 3u;
  for(wide_leaf_effect *p=wideLeafEffects;p;p=p->next)
    if(p->local==!!IS_STATIC(callee->etype) && p->abi==FUNC_SDCCCALL(callee->type) && !strcmp(p->name,callee->rname))return p->dpxWrites;
  return 3u;
}
/* Private temporary bits are distinct from allocated C bit objects. */
unsigned mcs251CallBitsWrites(symbol *callee) {
  const char *enable=getenv("MX251_BITS_CALL_EFFECTS");
  if(!callee || (enable && !strcmp(enable,"0")))return 1u;
  for(wide_leaf_effect *p=wideLeafEffects;p;p=p->next)
    if(p->local==!!IS_STATIC(callee->etype) && p->abi==FUNC_SDCCCALL(callee->type) && !strcmp(p->name,callee->rname))return p->bitsWrites;
  return 1u;
}
