/* GPL-2.0-or-later. Consumer of the shared typed parameter-domain API.
 * This file owns no parser: singleton and finite-domain facts use one format. */
typedef struct {
  symbol *function;
  unsigned ordinal, bits;
} singleton_application;
static singleton_application singletonApplications[512];
static unsigned singletonApplicationCount;

static operand *
singletonParameterValue (symbol *function, value *formal, unsigned ordinal)
{
  unsigned values[4];
  unsigned count = mcs251ParameterDomainValues (function, formal, ordinal, values);
  if (fatalError || !count) return NULL;
  /* 新旧合同同一参数不能竞争；即使数值相同也要求宿主消除歧义。
   * 多值域仍保留 RECEIVE，绝不从域里任选一个值代替实参。 */
  if (ordinal == 1)
    for (unsigned i = 0; i < targetCount; ++i)
      {
        char name[256];
        int length = snprintf (name, sizeof name, "___m251r4_%s", function->name);
        if (length >= 0 && length < (int) sizeof name && !strcmp (targets[i].name, name))
          { constantFail ("legacy and domain contracts select the same parameter", name); return NULL; }
      }
  if (count != 1) return NULL;
  if (ordinal < 1 || ordinal > 2 || singletonApplicationCount == 512)
    { constantFail ("unsupported singleton ordinal or excessive applications", function->name); return NULL; }
  for (unsigned i = 0; i < singletonApplicationCount; ++i)
    if (singletonApplications[i].function == function && singletonApplications[i].ordinal == ordinal)
      { constantFail ("duplicate singleton definition application", function->name); return NULL; }
  /* 共享 API 已复核完整函数类型、私有 ABI、argreg、cv 和取址约束。
   * 只替换初始 RECEIVE；形参之后的赋值继续参与正常 IR。 */
  sym_link *arg = formal->type;
  operand *literal = operandFromValue (valCastLiteral (arg, (double) values[0], values[0]), false);
  if (!literal || !IS_OP_LITERAL (literal) || getSize (operandType (literal)) != 1 ||
      operandLitValue (literal) != (double) values[0])
    { constantFail ("singleton literal reconstruction failed", function->name); return NULL; }
  singletonApplications[singletonApplicationCount++] = (singleton_application) {function, ordinal, values[0]};
  fprintf (stderr, "MX251 positional singleton applied symbol=___m251r4_%s ordinal=%u type=u8 bits=%02x abi=4\n",
           function->name, ordinal, values[0]);
  return literal;
}

bool
mcs251ConstantParameterAppliedAt (symbol *function, unsigned ordinal, unsigned *bits)
{
  if (!function || ordinal < 1 || ordinal > 2) return false;
  for (unsigned i = 0; i < singletonApplicationCount; ++i)
    if (singletonApplications[i].function == function && singletonApplications[i].ordinal == ordinal)
      { if (bits) *bits = singletonApplications[i].bits; return true; }
  if (ordinal == 1)
    for (unsigned i = 0; i < targetCount; ++i)
      if (targets[i].applied == function)
        { if (bits) *bits = targets[i].bits; return true; }
  return false;
}
