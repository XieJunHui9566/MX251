/* SPDX-License-Identifier: GPL-2.0-or-later
 * 实验性构建反馈：来源/输入/对象一致性由两遍构建驱动验证。
 * 裸命令行参数是显式编译契约，不能把用户给的掩码当成已验证事实。 */
extern char *mcs251LeafEffectsFile;
typedef struct wide_leaf_effect {
  char name[SDCC_NAME_MAX+1];unsigned abi;uint32_t writes;
  struct wide_leaf_effect *next;
} wide_leaf_effect;
static wide_leaf_effect *wideLeafEffects;
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
void mcs251LoadLeafEffects(void) {
  if(!mcs251LeafEffectsFile)return;
  FILE *file=fopen(mcs251LeafEffectsFile,"rb");char line[SDCC_NAME_MAX+64];unsigned count=0;
  if(!file)goto invalid;
  if(wide_effect_line(file,line,sizeof line)!=1 ||
     (strcmp(line,"MX251-LEAF-EFFECTS-1") && strcmp(line,"MX251-CALL-CLOBBERS-1")))goto invalid;
  int status;
  while((status=wide_effect_line(file,line,sizeof line))==1) {
    char *name=strtok(line," \t"),*abi=strtok(NULL," \t"),*mask=strtok(NULL," \t"),*extra=strtok(NULL," \t");
    if(!name || !abi || !mask || extra || strlen(name)>SDCC_NAME_MAX || strlen(mask)!=8 ||
       (strcmp(abi,"2") && strcmp(abi,"4")) || ++count>4096)goto invalid;
    if(!isalpha((unsigned char)*name) && *name!='_' && *name!='$' && *name!='.')goto invalid;
    for(char *p=name;*p;++p)if(!isalnum((unsigned char)*p) && *p!='_' && *p!='$' && *p!='.')goto invalid;
    for(char *p=mask;*p;++p)if(!isxdigit((unsigned char)*p))goto invalid;
    for(wide_leaf_effect *p=wideLeafEffects;p;p=p->next)if(!strcmp(p->name,name))goto invalid;
    wide_leaf_effect *entry=Safe_alloc(sizeof *entry);strcpy(entry->name,name);
    entry->abi=(unsigned)(*abi-'0');entry->writes=(uint32_t)strtoul(mask,NULL,16);
    entry->next=wideLeafEffects;wideLeafEffects=entry;
  }
  if(status<0)goto invalid;
  fclose(file);return;
invalid:
  if(file)fclose(file);
  werror(E_BAD_INT_ARGUMENT,"--mcs251-leaf-effects: invalid feedback contract");exit(EXIT_FAILURE);
}
static bool wide_leaf_writes(symbol *callee,uint32_t *writes) {
  /* 同名 static 函数不能借用其它翻译单元的导出定义。 */
  if(!callee || IS_STATIC(callee->etype))return false;
  for(wide_leaf_effect *p=wideLeafEffects;p;p=p->next)
    if(p->abi==FUNC_SDCCCALL(callee->type) && !strcmp(p->name,callee->rname)) {*writes=p->writes;return true;}
  return false;
}

/* 普通后端与宽后端消费相同的显式反馈；绑定和最终上界由构建入口核对。 */
bool mcs251CallClobberWrites(symbol *callee, unsigned long *writes) {
  uint32_t mask;
  if(!wide_leaf_writes(callee,&mask))return false;
  *writes=(unsigned long)mask;return true;
}
