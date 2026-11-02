/* GPL-2.0-or-later. Explicit, bounded parameter-domain container.
 * A contract is a whole-image proof input, never an inference from a name. */
char *mcs251ParameterDomainFile;
typedef struct {
  char name[256], signature[1024];
  unsigned ordinal, count, values[4];
} parameter_domain;
static parameter_domain domains[256];
static unsigned domainCount;
static bool domainsLoaded;

void
mcs251LoadParameterDomains (void)
{
  if(domainsLoaded)return;
  domainsLoaded=true;
  if(!mcs251ParameterDomainFile)return;
  FILE *file=fopen(mcs251ParameterDomainFile,"r");
  char line[1536];
  if(!file){constantFail("cannot open parameter-domain contract",NULL);return;}
  if(!fgets(line,sizeof line,file) || strcmp(line,"MX251-PARAMETER-DOMAIN-V1\n"))
    {constantFail("invalid parameter-domain header",NULL);fclose(file);return;}
  while(fgets(line,sizeof line,file)) {
    size_t length=strlen(line);
    if(!length || line[length-1]!='\n' || domainCount==256)
      {constantFail("truncated or excessive parameter-domain records",NULL);break;}
    line[--length]=0;
    char *fields[5]={line};unsigned fieldsCount=1;
    for(char *p=line;*p;++p)if(*p=='\t') {
      if(fieldsCount==5){constantFail("excess parameter-domain fields",NULL);break;}
      *p=0;fields[fieldsCount++]=p+1;
    }
    if(fatalError)break;
    if(fieldsCount!=5 || strlen(fields[0])>=256 || strlen(fields[1])>=1024 ||
       strncmp(fields[0],"___m251r4_",10) || !fields[0][10] || !fields[1][0] ||
       (strcmp(fields[2],"1") && strcmp(fields[2],"2")) || strcmp(fields[3],"u8"))
      {constantFail("unsupported parameter-domain name/signature/ordinal/type",line);break;}
    for(const char *p=fields[0];*p;++p)
      if(!isalnum((unsigned char)*p) && *p!='_' && *p!='$')constantFail("invalid domain symbol",fields[0]);
    for(const char *p=fields[1];*p;++p)
      if(!isalnum((unsigned char)*p) && *p!='_' && *p!='(' && *p!=')' && *p!=',')constantFail("invalid domain signature",fields[0]);
    parameter_domain t={0};t.ordinal=(unsigned)(fields[2][0]-'0');
    for(char *p=fields[4];*p;) {
      if(t.count==4 || !isxdigit((unsigned char)p[0]) || !p[1] ||
         !isxdigit((unsigned char)p[1]) || (p[2] && p[2]!=','))
        {constantFail("invalid or excessive domain values",fields[0]);break;}
      char bits[3]={p[0],p[1],0};unsigned v=(unsigned)strtoul(bits,NULL,16);
      if(t.count && v<=t.values[t.count-1]){constantFail("domain must be sorted and unique",fields[0]);break;}
      t.values[t.count++]=v;p+=2;
      if(*p==','){++p;if(!*p)constantFail("trailing domain delimiter",fields[0]);}
    }
    if(!t.count){constantFail("domain requires one to four values",fields[0]);break;}
    for(unsigned i=0;i<domainCount;++i)
      if(domains[i].ordinal==t.ordinal && !strcmp(domains[i].name,fields[0]))constantFail("duplicate parameter-domain record",fields[0]);
    if(fatalError)break;
    strcpy(t.name,fields[0]);strcpy(t.signature,fields[1]);domains[domainCount++]=t;
  }
  if(ferror(file))constantFail("parameter-domain read failed",NULL);
  fclose(file);
  if(!fatalError && !domainCount)constantFail("empty parameter-domain contract",NULL);
}

unsigned
mcs251ParameterDomainValues (symbol *function,value *formal,unsigned ordinal,unsigned values[4])
{
  mcs251LoadParameterDomains();
  if(fatalError || !function || !formal || !domainCount || ordinal<1 || ordinal>2)return 0;
  char name[256];
  if(snprintf(name,sizeof name,"___m251r4_%s",function->name)>=(int)sizeof name)return 0;
  parameter_domain *d=NULL;
  for(unsigned i=0;i<domainCount;++i)if(domains[i].ordinal==ordinal && !strcmp(domains[i].name,name)){d=&domains[i];break;}
  if(!d)return 0;
  sym_link *type=function->type,*arg=formal->type;
  value *declared=IS_FUNC(type)?FUNC_ARGS(type):NULL;
  for(unsigned k=1;k<ordinal && declared;++k)declared=declared->next;
  /* 与既有常量初值接口复用相同的完整类型/ABI 边界，但不替换 RECEIVE。
   * 后端另证唯一入口定义和支配关系；取址/volatile 形参绝不能借域合同缓存。 */
  if(!declared || declared!=formal || FUNC_SDCCCALL(type)!=4 || !mcs251InternalNative(function) ||
     !mcs251InternalTransportMatches(function,d->signature) ||
     function->cdef || function->addrtaken || IS_STATIC(function->etype) ||
     IFFUNC_ISISR(type) || !IFFUNC_ISREENT(type) || IFFUNC_HASVARARGS(type) ||
     IFFUNC_ISNAKED(type) || IFFUNC_ISBANKEDCALL(type) || IFFUNC_ISCRITICAL(type) ||
     IFFUNC_CALLEESAVES(type) || FUNC_REGBANK(type) || FUNC_NOPROTOTYPE(type) ||
     !formal->sym || formal->sym->addrtaken || isVolatile(arg) || isAtomic(arg) ||
     isOptional(arg) || getAddrspace(arg) || !IS_CHAR(arg) || arg->next ||
     getSize(arg)!=1 || !SPEC_USIGN(getSpec(arg)) || !IS_REGPARM(formal->etype) ||
     SPEC_ARGREG(formal->etype)!=ordinal)
    {constantFail("selected parameter outside proven first-u8 domain profile",name);return 0;}
  memcpy(values,d->values,d->count*sizeof *values);return d->count;
}
