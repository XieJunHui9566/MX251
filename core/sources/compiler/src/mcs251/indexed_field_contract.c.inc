/* GPL-2.0-or-later. 显式整镜像事实；名称仅为绑定身份，不是优化策略。 */
enum { WIF_RECORDS=256, WIF_EXTENT=128 };
typedef struct {char name[256],signature[1024];unsigned ordinal,extent;uint32_t minimum,maximum;} wif_contract;
static wif_contract wif_contracts[WIF_RECORDS];
static unsigned wif_contract_count;
static bool wif_contract_loaded;

static bool
wif_unsigned(const char *p,uint32_t limit,uint32_t *out)
{
  if(!p || !*p)return false;uint32_t n=0;
  for(;*p;++p) {if(!isdigit((unsigned char)*p))return false;unsigned digit=*p-'0';if(n>limit/10u || (n==limit/10u && digit>limit%10u))return false;n=n*10u+digit;}
  *out=n;return true;
}

static void
wif_load_contract(void)
{
  if(wif_contract_loaded)return;wif_contract_loaded=true;
  if(!mcs251IndexedFieldContract)return;
  FILE *file=fopen(mcs251IndexedFieldContract,"r");char line[1536];
  const char *error=NULL;
  if(!file)error="cannot open field-object contract";
  else if(!fgets(line,sizeof line,file) || strcmp(line,"MX251-INDEXED-FIELD-OBJECTS-V1\n"))error="invalid field-object contract header";
  while(!error && fgets(line,sizeof line,file)) {
    size_t length=strlen(line);char *v[6]={line};unsigned fields=1;
    if(!length || line[length-1]!='\n' || wif_contract_count==WIF_RECORDS){error="truncated/excess field-object records";break;}
    line[--length]=0;
    for(char *p=line;*p;++p)if(*p=='\t') {if(fields==6){error="excess field-object fields";break;}*p=0;v[fields++]=p+1;}
    if(error)break;
    wif_contract row={0};uint32_t ordinal,extent;
    if(fields!=6 || !v[0][0] || strlen(v[0])>=sizeof row.name || !v[1][0] || strlen(v[1])>=sizeof row.signature ||
       !wif_unsigned(v[2],2,&ordinal) || !ordinal || !wif_unsigned(v[3],WIF_EXTENT,&extent) || !extent ||
       !wif_unsigned(v[4],0xffffff,&row.minimum) || !wif_unsigned(v[5],0xffffff,&row.maximum) ||
       row.minimum<32 || row.minimum>row.maximum || row.maximum>0xffffffu-(extent-1u)) {error="invalid field-object bounds";break;}
    for(char *p=v[0];*p;++p)if(!isalnum((unsigned char)*p) && *p!='_' && *p!='$')error="invalid field-object symbol";
    for(char *p=v[1];*p;++p)if(!isalnum((unsigned char)*p) && *p!='_' && *p!='(' && *p!=')' && *p!=',')error="invalid field-object signature";
    for(unsigned k=0;k<wif_contract_count;++k)
      if(wif_contracts[k].ordinal==ordinal && !strcmp(wif_contracts[k].name,v[0]))error="duplicate field-object record";
    if(error)break;
    strcpy(row.name,v[0]);strcpy(row.signature,v[1]);row.ordinal=ordinal;row.extent=extent;
    wif_contracts[wif_contract_count++]=row;
  }
  if(file) {if(ferror(file))error="field-object read failed";fclose(file);}
  if(error) {fprintf(stderr,"MX251 indexed field contract: %s\n",error);fatalError=1;}
}

static const wif_contract *
wif_parameter_contract(unsigned ordinal,sym_link **pointee)
{
  if(!mcs251IndexedFieldEnabled || !mcs251IndexedFieldCapability ||
     strcmp(mcs251IndexedFieldCapability,"source24-byte-v1") || !mcs251IndexedFieldContract)return NULL;
  wif_load_contract();if(fatalError)return NULL;
  const wif_contract *found=NULL;
  for(unsigned k=0;k<wif_contract_count;++k)
    if(wif_contracts[k].ordinal==ordinal && !strcmp(wif_contracts[k].name,currFunc->rname))found=&wif_contracts[k];
  if(!found)return NULL;
  sym_link *ft=currFunc->type;value *arg=FUNC_ARGS(ft);
  for(unsigned k=1;k<ordinal && arg;++k)arg=arg->next;
  char token[2048]="";
  if(!arg || FUNC_SDCCCALL(ft)!=4 || !IFFUNC_ISREENT(ft) || currFunc->addrtaken || currFunc->cdef ||
     IFFUNC_ISISR(ft) || IFFUNC_ISNAKED(ft) || IFFUNC_ISCRITICAL(ft) || IFFUNC_CALLEESAVES(ft) ||
     IFFUNC_ISBANKEDCALL(ft) || FUNC_REGBANK(ft) || IFFUNC_HASVARARGS(ft) || FUNC_NOPROTOTYPE(ft) ||
     !mcs251InternalTransportMatches(currFunc,found->signature) || !IS_PTR(arg->type) || getSize(arg->type)!=3 ||
     (DCL_TYPE(arg->type)!=FPOINTER && DCL_TYPE(arg->type)!=GPOINTER) || isAtomic(arg->type) || isVolatile(arg->type) ||
     isAtomic(arg->type->next) || isVolatile(arg->type->next) || !arg->sym || arg->sym->addrtaken ||
     !IS_REGPARM(arg->etype) || SPEC_ARGREG(arg->etype)!=ordinal ||
     !mcs251FlatAggregateToken(arg->type->next,token,sizeof token) || getSize(arg->type->next)!=found->extent) {
    fprintf(stderr,"MX251 indexed field contract: current typed parameter disagrees with bound record\n");fatalError=1;return NULL;
  }
  *pointee=arg->type->next;return found;
}
