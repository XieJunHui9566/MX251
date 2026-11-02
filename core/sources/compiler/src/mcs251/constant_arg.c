/* GPL-2.0-or-later. Private experiment; never an implicit source assumption. */
#include "constant_arg.h"
#include "internal_return.h"

typedef struct {
  char name[256], signature[1024];
  unsigned bits;
  symbol *applied;
} constant_target;
static constant_target targets[256];
static unsigned targetCount;
static bool loaded;

static void
constantFail (const char *reason, const char *name)
{
  fprintf (stderr, "MX251 constant parameter contract: %s%s%s\n", reason,
           name ? ": " : "", name ? name : "");
  fatalError = 1;
}

static void
loadContract (void)
{
  if (loaded) return;
  loaded = true;
  const char *path = getenv ("MX251_CONSTANT_PARAMETER_CONTRACT");
  if (!path || !*path) return;
  FILE *file = fopen (path, "r");
  char line[1536];
  if (!file) {constantFail ("cannot open contract", NULL); return;}
  if (!fgets (line, sizeof line, file) || strcmp (line, "MX251-CONSTANT-FIRST-U8-V1\n"))
    {constantFail ("invalid header", NULL); fclose (file); return;}
  while (fgets (line, sizeof line, file))
    {
      size_t n = strlen (line);
      if (!n || line[n-1] != '\n' || targetCount == 256)
        {constantFail ("truncated or excessive records", NULL); break;}
      line[--n] = 0;
      char *sig = strchr (line, '\t');
      char *tail = sig ? strchr (sig+1, '\t') : NULL;
      if (!sig || !tail || sig-line >= 256 || tail-sig-1 >= 1024 || sig==line || tail==sig+1)
        {constantFail ("invalid target fields", NULL); break;}
      *sig++=0; *tail++=0;
      if (strncmp (line,"___m251r4_",10) || !line[10] || strlen(tail)!=7 ||
          strncmp(tail,"1\tu8\t",5) || !isxdigit((unsigned char)tail[5]) || !isxdigit((unsigned char)tail[6]))
        {constantFail ("invalid private name, ordinal, type or bits",line); break;}
      for (const char *p=line; *p; ++p)
        if (!isalnum((unsigned char)*p) && *p!='_' && *p!='$')
          {constantFail("invalid target character",line);break;}
      for (const char *p=sig; *p; ++p)
        if (!isalnum((unsigned char)*p) && *p!='_' && *p!='(' && *p!=')' && *p!=',')
          {constantFail("invalid signature character",line);break;}
      for(unsigned i=0;i<targetCount;++i)
        if(!strcmp(targets[i].name,line)){constantFail("duplicate target",line);break;}
      if(fatalError)break;
      constant_target *t=&targets[targetCount++];
      strcpy(t->name,line);strcpy(t->signature,sig);t->bits=(unsigned)strtoul(tail+5,NULL,16);
    }
  if(ferror(file))constantFail("read failed",NULL);
  fclose(file);
  if(!fatalError && !targetCount)constantFail("empty contract",NULL);
}


#include "singleton_consumer.c.inc"
#include "ordinary_scalar.c.inc"

operand *
mcs251ConstantParameterValue (symbol *function, value *formal, unsigned ordinal)
{
  loadContract();
  if(fatalError || !function || !formal)return NULL;
  operand *ordinary=ordinaryScalarParameterValue(function,formal,ordinal);
  if(fatalError || ordinary)return ordinary;
  operand *singleton=singletonParameterValue(function,formal,ordinal);
  if(fatalError || singleton)return singleton;
  if(ordinal!=1)return NULL;
  char name[256];
  if(snprintf(name,sizeof name,"___m251r4_%s",function->name)>=(int)sizeof name)
    return NULL;
  constant_target *target=NULL;
  for(unsigned i=0;i<targetCount;++i)if(!strcmp(targets[i].name,name)){target=&targets[i];break;}
  if(!target)return NULL;
  sym_link *type=function->type,*arg=formal->type;
  /* 闭包由宿主独立证明；这里仍重复核对原 ABI、完整签名和入口限制。
   * 只替换初值，绝不把后续对形参的赋值当作不可变 SSA 常量。 */
  if(!IS_FUNC(type) || FUNC_SDCCCALL(type)!=4 || !mcs251InternalNative(function) ||
     !mcs251InternalTransportMatches(function,target->signature) ||
     function->cdef || function->addrtaken || IS_STATIC(function->etype) ||
     IFFUNC_ISISR(type) || !IFFUNC_ISREENT(type) || IFFUNC_HASVARARGS(type) ||
     IFFUNC_ISNAKED(type) || IFFUNC_ISBANKEDCALL(type) || IFFUNC_ISCRITICAL(type) ||
     IFFUNC_CALLEESAVES(type) || FUNC_REGBANK(type) || FUNC_NOPROTOTYPE(type) ||
     !formal->sym || formal->sym->addrtaken || isVolatile(arg) || isAtomic(arg) ||
     isOptional(arg) || getAddrspace(arg) || !IS_CHAR(arg) || arg->next ||
     getSize(arg)!=1 || !SPEC_USIGN(getSpec(arg)) || !IS_REGPARM(formal->etype) ||
     SPEC_ARGREG(formal->etype)!=1)
    {constantFail("selected parameter outside proven first-u8 profile",name);return NULL;}
  if(target->applied){constantFail("duplicate definition application",name);return NULL;}
  /* valCastLiteral 拷贝完整 formal 类型，只把存储类别改为 literal。 */
  operand *literal=operandFromValue(valCastLiteral(arg,(double)target->bits,target->bits),false);
  if(!literal || !IS_OP_LITERAL(literal) || getSize(operandType(literal))!=1 ||
     operandLitValue(literal)!=(double)target->bits)
    {constantFail("literal reconstruction failed",name);return NULL;}
  target->applied=function;
  fprintf(stderr,"MX251 constant parameter applied symbol=%s ordinal=1 type=u8 bits=%02x abi=4\n",name,target->bits);
  return literal;
}

bool
mcs251ConstantParameterApplied (symbol *function, unsigned *bits)
{
  return mcs251ConstantParameterAppliedAt(function,1,bits);
}

#include "domain_contract.c.inc"
