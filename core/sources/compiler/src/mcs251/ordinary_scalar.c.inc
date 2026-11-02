/* Explicit whole-image facts only. The host owns reference closure and cost. */
#include "ordinary_scalar.h"
char *mcs251ScalarParameterFile;
typedef struct {char name[256],signature[128];unsigned ordinal;unsigned long bits;bool applied;} scalar_parameter;
static scalar_parameter scalarParameters[256];
static unsigned scalarParameterCount;
static bool scalarParametersLoaded;
static void scalarParameterLoad(void)
{
  if(scalarParametersLoaded)return;scalarParametersLoaded=true;
  if(!mcs251ScalarParameterFile)return;
  FILE *f=fopen(mcs251ScalarParameterFile,"r");char line[512];
  if(!f){constantFail("cannot open scalar parameter contract",NULL);return;}
  if(!fgets(line,sizeof line,f) || strcmp(line,"MX251-ORDINARY-SCALAR-V1\n"))
    {constantFail("invalid scalar contract header",NULL);fclose(f);return;}
  while(fgets(line,sizeof line,f)) {
    size_t n=strlen(line);char name[256],sig[128],extra;unsigned ordinal;unsigned long bits;
    if(!n || line[n-1]!='\n' || scalarParameterCount==256 ||
       sscanf(line,"%255s\t%127s\t%u\t%lx %c",name,sig,&ordinal,&bits,&extra)!=4 ||
       name[0]!='_' || ordinal<1 || ordinal>8 || bits>0xffffffffUL)
      {constantFail("invalid scalar contract row",NULL);break;}
    for(const char *p=name;*p;++p)if(!isalnum((unsigned char)*p) && *p!='_' && *p!='$')constantFail("invalid scalar name",name);
    for(unsigned i=0;i<scalarParameterCount;++i)if(!strcmp(scalarParameters[i].name,name) && scalarParameters[i].ordinal==ordinal)constantFail("duplicate scalar parameter",name);
    if(fatalError)break;
    scalar_parameter *p=&scalarParameters[scalarParameterCount++];strcpy(p->name,name);strcpy(p->signature,sig);p->ordinal=ordinal;p->bits=bits;
  }
  if(ferror(f))constantFail("scalar contract read failed",NULL);
  fclose(f);
  if(!fatalError && !scalarParameterCount)constantFail("empty scalar contract",NULL);
}
static operand *ordinaryScalarParameterValue(symbol *fn,value *formal,unsigned ordinal)
{
  scalarParameterLoad();if(fatalError || !fn || !formal)return NULL;
  scalar_parameter *record=NULL;
  for(unsigned i=0;i<scalarParameterCount;++i)
    if(scalarParameters[i].ordinal==ordinal && !strcmp(scalarParameters[i].name+1,fn->name)){record=&scalarParameters[i];break;}
  if(!record)return NULL;
  char signature[128];unsigned width=ordinaryScalarType(formal->type);
  value *arg=IS_FUNC(fn->type)?FUNC_ARGS(fn->type):NULL;
  for(unsigned i=1;i<ordinal && arg;++i)arg=arg->next;
  if(record->applied || !ordinaryScalarSignature(fn->type,signature,sizeof signature) ||
     strcmp(signature,record->signature) || arg!=formal || !width ||
     !formal->sym || formal->sym->addrtaken || fn->addrtaken || fn->cdef ||
     (width<4 && record->bits>=(1UL<<(width*8)))) {
    constantFail("scalar parameter outside bound ordinary signature",fn->name);return NULL;
  }
  /* 只替换入口值。signed 的位型先恢复为精确的有符号值，后续赋值仍为原 IR。 */
  double value=(double)record->bits;
  if(!SPEC_USIGN(getSpec(formal->type)) && (record->bits&(1UL<<(width*8-1))))value-=width==4?4294967296.0:(double)(1UL<<(width*8));
  operand *literal=operandFromValue(valCastLiteral(formal->type,value,record->bits),false);
  if(!literal || !IS_OP_LITERAL(literal) || getSize(operandType(literal))!=width || operandLitValue(literal)!=value)
    {constantFail("scalar literal reconstruction failed",fn->name);return NULL;}
  record->applied=true;
  fprintf(stderr,"MX251 ordinary scalar applied symbol=_%s signature=%s ordinal=%u bits=%08lx abi=2\n",fn->name,signature,ordinal,record->bits);
  return literal;
}
