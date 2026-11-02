/* GPL-2.0-or-later. Per-build contract generated from bound whole-image proof. */
#include "internal_return.h"
extern bool mcs251ValidateNativeAbi (sym_link *functionType);

static bool loaded;
static char names[256][256];
static unsigned count;

static void fail(const char *why)
{
  fprintf(stderr,"MX251 internal return contract: %s\n",why);
  fatalError=1;
}

bool mcs251InternalReturn(symbol *s)
{
  if(!loaded) {
    const char *path=getenv("MX251_INTERNAL_RETURN_CONTRACT");
    loaded=true;
    if(path && *path) {
      FILE *f=fopen(path,"r");char line[256];
      if(!f) {fail("cannot open contract");return false;}
      if(!fgets(line,sizeof line,f) || strcmp(line,"MX251-INTERNAL-RETURN-DR4-V1\n")) {
        fclose(f);fail("invalid contract header");return false;
      }
      while(fgets(line,sizeof line,f)) {
        size_t n=strcspn(line,"\r\n");line[n]=0;
        if(!n || n>=sizeof line-1 || count==256 || line[0]!='_') {fail("invalid target record");break;}
        for(size_t i=0;i<n;++i)if(!isalnum((unsigned char)line[i]) && line[i]!='_' && line[i]!='$') {fail("invalid target character");break;}
        for(unsigned i=0;i<count;++i)if(!strcmp(names[i],line)) {fail("duplicate target");break;}
        if(fatalError)break;
        strcpy(names[count++],line);
      }
      if(ferror(f))fail("contract read failed");
      fclose(f);
    }
  }
  if(fatalError || !s || !IS_FUNC(s->type))return false;
  const char *name=s->rname[0]?s->rname:NULL;
  bool selected=false;
  for(unsigned i=0;i<count;++i)
    if((name && !strcmp(names[i],name)) || (names[i][0]=='_' && !strcmp(names[i]+1,s->name)))selected=true;
  if(!selected)return false;
  sym_link *t=s->type;
  /* 参数仍为 ABI2；只接受已证明的普通 32 位整数返回。不能静默退化。 */
  if(FUNC_SDCCCALL(t)!=2 || !IFFUNC_ISREENT(t) || IFFUNC_ISISR(t) ||
     IFFUNC_ISBANKEDCALL(t) || IFFUNC_ISNAKED(t) || IFFUNC_ISCRITICAL(t) ||
     IFFUNC_CALLEESAVES(t) || FUNC_REGBANK(t) || IFFUNC_HASVARARGS(t) ||
     IFFUNC_ISNORETURN(t) || !IS_INTEGRAL(t->next) || getSize(t->next)!=4) {
    fail("selected target outside scalar ABI2 profile");return false;
  }
  return true;
}

/* 仅同一闭合镜像的独立试验开关；保留 ABI2 栈参数位置与副本语义。 */
bool mcs251InternalArgument(symbol *s)
{
  const char *enabled=getenv("MX251_INTERNAL_ARG1_DR4");
  if(!enabled || strcmp(enabled,"1") || !mcs251InternalReturn(s))return false;
  value *arg=FUNC_ARGS(s->type);
  if(!arg || !IS_INTEGRAL(arg->type) || IS_BITINT(arg->type) ||
     getSize(arg->type)!=4 || isAtomic(arg->type)) {
    fail("selected first argument outside 32-bit integral profile");return false;
  }
  return true;
}

/* 不引入第三套发射 ABI。此契约只把已闭合的普通标量函数及它的所有
 * 声明早期标为既有 ABI4；原型、SEND/RECEIVE 和栈布局因此来自同一规则。
 * 名单与完整语义签名/对象的对应关系由 host 闭合证明绑定；编译器再次
 * 检查 transport 签名和特殊属性，不能把任意库符号当成可迁移目标。 */
#define INTERNAL_ABI_LIMIT 256
#define INTERNAL_ABI_SIGNATURE 1024
typedef struct {
  char name[256];
  char signature[INTERNAL_ABI_SIGNATURE];
} internal_abi_target;
static internal_abi_target internalAbiTargets[INTERNAL_ABI_LIMIT];
static unsigned internalAbiCount;
static bool internalAbiLoaded;

static void
internalAbiFail (const char *why, const char *name)
{
  fprintf (stderr, "MX251 internal ABI4 contract: %s%s%s\n", why,
           name ? ": " : "", name ? name : "");
  fatalError = 1;
}

static void
internalAbiLoad (void)
{
  if (internalAbiLoaded) return;
  internalAbiLoaded = true;
  const char *path = getenv ("MX251_INTERNAL_ABI4_CONTRACT");
  if (!path || !*path) return;
  FILE *f = fopen (path, "r");
  char line[1536];
  if (!f) {internalAbiFail ("cannot open contract", NULL); return;}
  if (!fgets (line, sizeof line, f) || strcmp (line, "MX251-INTERNAL-ABI4-V2\n"))
    {internalAbiFail ("invalid contract header", NULL); fclose (f); return;}
  while (fgets (line, sizeof line, f))
    {
      size_t n = strlen (line);
      if (!n || line[n - 1] != '\n' || internalAbiCount == INTERNAL_ABI_LIMIT)
        {internalAbiFail ("truncated or excessive target records", NULL); break;}
      line[--n] = 0;
      char *tab = strchr (line, '\t');
      if (!tab || tab == line || tab - line >= 256 || strchr (tab + 1, '\t') ||
          !tab[1] || strlen (tab + 1) >= INTERNAL_ABI_SIGNATURE || line[0] != '_')
        {internalAbiFail ("invalid target record", NULL); break;}
      *tab++ = 0;
      for (const char *p = line; *p; ++p)
        if (!isalnum ((unsigned char)*p) && *p != '_' && *p != '$')
          {internalAbiFail ("invalid target name", line); break;}
      for (const char *p = tab; *p; ++p)
        if (!isalnum ((unsigned char)*p) && *p != '(' && *p != ')' && *p != ',' && *p != '_')
          {internalAbiFail ("invalid signature character", line); break;}
      for (unsigned i = 0; i < internalAbiCount; ++i)
        if (!strcmp (internalAbiTargets[i].name, line))
          {internalAbiFail ("duplicate target", line); break;}
      if (fatalError) break;
      strcpy (internalAbiTargets[internalAbiCount].name, line);
      strcpy (internalAbiTargets[internalAbiCount].signature, tab);
      ++internalAbiCount;
    }
  if (ferror (f)) internalAbiFail ("contract read failed", NULL);
  fclose (f);
  if (!fatalError && !internalAbiCount) internalAbiFail ("empty contract", NULL);
}

static bool
internalAbiAppend (char *text, size_t size, const char *suffix)
{
  if (strlen (text) + strlen (suffix) >= size) return false;
  strcat (text, suffix);
  return true;
}

/* 只描述被运输指针的完整平坦布局，不改变对象表示、限定或访存。
 * 字段及偏移进入显式契约；不完整、位域、数组、嵌套、union、枚举和
 * 非整数布局保守拒绝。独立宿主仍须绑定同源声明与完整机器引用闭包。 */
bool
mcs251FlatAggregateToken (sym_link *type, char *text, size_t size)
{
  if (!type || !IS_STRUCT (type) || type->next || getAddrspace (type) ||
      isAtomic (type) || isOptional (type)) return false;
  structdef *def = SPEC_STRUCT (type);
  if (!def || def->type != STRUCT || !def->fields || def->b_flexArrayMember ||
      !def->size || def->size > 128 || getSize (type) != def->size) return false;
  char token[2048], part[256];
  SNPRINTF (token, sizeof token, "s(%u", def->size);
  unsigned count = 0, previousEnd = 0;
  for (symbol *field = def->fields; field; field = field->next)
    {
      sym_link *t = field->type;
      if (++count > 32 || !field->name[0] || strlen (field->name) > 127 ||
          !t || !IS_SPEC (t) || t->next || (!IS_INT (t) && !IS_CHAR (t)) ||
          SPEC_ENUM (t) || IS_BITFIELD (t) || field->bitVar || field->bitUnnamed ||
          getAddrspace (t) || isAtomic (t) || isOptional (t) || isRestrict (t)) return false;
      for (const char *c=field->name; *c; ++c)
        if (!isalnum ((unsigned char)*c) && *c!='_') return false;
      unsigned bytes = getSize (t);
      if ((bytes!=1 && bytes!=2 && bytes!=4) || field->offset < previousEnd ||
          field->offset > def->size || bytes > def->size-field->offset) return false;
      SNPRINTF (part, sizeof part, ",%s(%u,%s%s%c%u)", field->name,
                field->offset, isConst(t)?"c":"", isVolatile(t)?"v":"",
                SPEC_USIGN(t)?'u':'i', bytes*8);
      if (!internalAbiAppend (token, sizeof token, part)) return false;
      previousEnd=field->offset+bytes;
    }
  if (!internalAbiAppend (token, sizeof token, ")")) return false;
  return internalAbiAppend (text, size, token);
}

static bool
internalAbiType (sym_link *type, char *text, size_t size, unsigned depth)
{
  if (!type || isAtomic (type) || isOptional (type) || getAddrspace (type) || depth > 1) return false;
  /* 顶层参数 cv 与对象语义不同，但仍入签名防止无声放宽。
   * 指针所指对象的 volatile 必须保留，不改变实际读写事务。 */
  if (isConst (type) && !internalAbiAppend (text, size, "c")) return false;
  if (isVolatile (type) && !internalAbiAppend (text, size, "v")) return false;
  if (isRestrict (type) && !internalAbiAppend (text, size, "r")) return false;
  if (IS_PTR (type))
    {
      if (depth || getSize (type) != 3 || !type->next ||
          (DCL_TYPE (type) != FPOINTER && DCL_TYPE (type) != GPOINTER)) return false;
      return internalAbiAppend (text, size, DCL_TYPE (type) == FPOINTER ? "pX(" : "pG(") &&
        internalAbiType (type->next, text, size, depth + 1) && internalAbiAppend (text, size, ")");
    }
  if (depth == 1 && IS_STRUCT (type)) return mcs251FlatAggregateToken (type, text, size);
  if (!IS_SPEC (type) || type->next) return false;
  if (IS_VOID (type)) return internalAbiAppend (text, size, "void");
  if (IS_FLOAT (type)) return getSize (type) == 4 && internalAbiAppend (text, size, "f32");
  if ((!IS_INT (type) && !IS_CHAR (type)) || IS_BITINT (type)) return false;
  int bytes = getSize (type);
  if (bytes != 1 && bytes != 2 && bytes != 4) return false;
  char scalar[8];
  SNPRINTF (scalar, sizeof scalar, "%c%d", SPEC_USIGN (type) ? 'u' : 'i', bytes * 8);
  return internalAbiAppend (text, size, scalar);
}

static bool
internalAbiSignature (sym_link *type, char *text, size_t size)
{
  text[0] = 0;
  if (!internalAbiType (type->next, text, size, 0) ||
      !internalAbiAppend (text, size, "(")) return false;
  unsigned index = 0;
  for (value *arg = FUNC_ARGS (type); arg; arg = arg->next)
    {
      if (!index && !arg->next && IS_VOID (arg->type)) break;
      if (index == 32 || IS_VOID (arg->type) ||
          (index && !internalAbiAppend (text, size, ",")) ||
          !internalAbiType (arg->type, text, size, 0)) return false;
      ++index;
    }
  return internalAbiAppend (text, size, ")");
}

bool
mcs251InternalTransportMatches (symbol *function, const char *expected)
{
  char signature[INTERNAL_ABI_SIGNATURE];
  return function && expected && IS_FUNC(function->type) &&
    internalAbiSignature(function->type,signature,sizeof signature) && !strcmp(signature,expected);
}

bool
mcs251InternalNative (symbol *function)
{
  if (!function || !IS_FUNC (function->type) || FUNC_SDCCCALL (function->type) != 4)
    return false;
  internalAbiLoad ();
  if (fatalError) return false;
  char signature[INTERNAL_ABI_SIGNATURE];
  if (!internalAbiSignature (function->type, signature, sizeof signature)) return false;
  for (unsigned i = 0; i < internalAbiCount; ++i)
    if (!strcmp (internalAbiTargets[i].name + 1, function->name) &&
        !strcmp (internalAbiTargets[i].signature, signature)) return true;
  return false;
}

void
mcs251ConfigureInternalAbi (symbol *function, sym_link *type)
{
  /* processFunc 也处理 typedef/函数指针及尚未加入基本返回类型的属性片段；
   * 这些不是已完成的可迁移函数声明。只处理 direct function 的完整链。 */
  if (!function || type != function->type || !IS_FUNC (type)) return;
  sym_link *spec = type->next ? getSpec (type->next) : NULL;
  if (!spec || !SPEC_NOUN (spec)) return;
  internalAbiLoad ();
  if (fatalError) return;
  const internal_abi_target *target = NULL;
  for (unsigned i = 0; i < internalAbiCount; ++i)
    if (!strcmp (internalAbiTargets[i].name + 1, function->name))
      {target = &internalAbiTargets[i]; break;}
  if (!target) return;

  bool customPreserves = false;
  for (unsigned i = 0; i < sizeof type->funcAttrs.preserved_regs / sizeof type->funcAttrs.preserved_regs[0]; ++i)
    customPreserves |= type->funcAttrs.preserved_regs[i];
  if (function->cdef || function->addrtaken || IS_STATIC (function->etype) ||
      IS_TYPEDEF (function->etype) || options.useXstack || options.noRegParams ||
      options.sdcccall != 2 || (FUNC_SDCCCALL (type) != 2 && FUNC_SDCCCALL (type) != 4) ||
      !IFFUNC_ISREENT (type) || FUNC_NOPROTOTYPE (type) || type->funcAttrs.oldStyle ||
      IFFUNC_ISISR (type) || IFFUNC_ISBANKEDCALL (type) || IFFUNC_ISNAKED (type) ||
      IFFUNC_ISCRITICAL (type) || IFFUNC_CALLEESAVES (type) || FUNC_REGBANK (type) ||
      type->funcAttrs.rbank || IFFUNC_NONBANKED (type) || FUNC_ISSHADOWREGS (type) ||
      FUNC_ISWPARAM (type) || FUNC_ISOVERLAY (type) || type->funcAttrs.javaNative ||
      type->funcAttrs.z88dk_has_params_offset || customPreserves ||
      IFFUNC_HASVARARGS (type) || IFFUNC_ISNORETURN (type) || FUNC_ISINLINE (type) ||
      FUNC_ISBUILTIN (type) || IFFUNC_ISSMALLC (type) || IFFUNC_ISDYNAMICC (type) ||
      IFFUNC_ISRAISONANCE (type) || IFFUNC_ISIAR (type) || IFFUNC_ISCOSMIC (type) ||
      IFFUNC_ISZ88DK_FASTCALL (type) || IFFUNC_ISZ88DK_CALLEE (type) || IFFUNC_ISZ88DK_SHORTCALL (type))
    {internalAbiFail ("selected declaration outside closed ordinary scalar profile", function->name); return;}
  char signature[INTERNAL_ABI_SIGNATURE];
  if (!internalAbiSignature (type, signature, sizeof signature) || strcmp (signature, target->signature))
    {internalAbiFail ("selected declaration transport signature mismatch", function->name); return;}
  /* 幂等：先前原型与后续定义都在 reg_parm 之前走此入口。ABI4 发射器及
   * 参数地址物化保持原规则；不修改 iCode、源码或调用参数求值次序。 */
  FUNC_SDCCCALL (type) = 4;
  /* 该声明在加入完整返回类型前可能按 ABI2 处理过参数。重建摘要，
   * 避免第二参数已进寄存器而仍携带旧 hasStackParm 状态。 */
  FUNC_HASSTACKPARM (type) = 0;
  if (!mcs251ValidateNativeAbi (type))
    internalAbiFail ("existing ABI4 validator rejected selected declaration", function->name);
}
