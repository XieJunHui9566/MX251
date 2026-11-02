/*
 * Audit-only return-contract evidence; this module never changes ABI or IR.
 * Copyright (C) 2026.  Distributed under GNU GPL version 2 or later.
 */
#include "common.h"
#include "return_audit.h"
#include "internal_return.h"
#include "constant_arg.h"
#include "wide.h"
#include <errno.h>

#define AUDIT_SCHEMA "mx251.return-audit.v1"
#define AUDIT_NODE_LIMIT 1000000UL
#define AUDIT_TYPE_DEPTH 16
#define AUDIT_PARAM_LIMIT 256

typedef struct audit_counts
{
  unsigned long declarations, bodies, calls, pcalls, references, opaque;
} audit_counts;

static FILE *audit_file;
static const char *audit_path, *audit_invocation, *audit_unit;
static unsigned long audit_seq, audit_body_serial, audit_site_serial, audit_nodes;
static audit_counts audit_total, audit_body;
static char audit_body_id[48];
static symbol *audit_owner;
static bool audit_failed, audit_snapshot_done, audit_snapshot_complete, audit_ir_complete;
static bool audit_body_complete;
static symbol **audit_seen;
static size_t audit_seen_count, audit_seen_capacity;

static const char *
audit_bool (bool value)
{
  return value ? "true" : "false";
}

static void
audit_error (const char *operation)
{
  if (!audit_failed)
    fprintf (stderr, "MX251 return audit: %s failed for %s: %s\n",
             operation, audit_path ? audit_path : "(no path)", strerror (errno));
  audit_failed = true;
  fatalError = 1;
}

static void
audit_string (const char *text)
{
  const unsigned char *p;
  if (!text)
    { fputs ("null", audit_file); return; }
  fputc ('"', audit_file);
  for (p = (const unsigned char *)text; *p; ++p)
    switch (*p)
      {
      case '"': fputs ("\\\"", audit_file); break;
      case '\\': fputs ("\\\\", audit_file); break;
      case '\b': fputs ("\\b", audit_file); break;
      case '\f': fputs ("\\f", audit_file); break;
      case '\n': fputs ("\\n", audit_file); break;
      case '\r': fputs ("\\r", audit_file); break;
      case '\t': fputs ("\\t", audit_file); break;
      default:
        if (*p < 0x20)
          fprintf (audit_file, "\\u%04x", (unsigned)*p);
        else
          fputc (*p, audit_file);
      }
  fputc ('"', audit_file);
}

static void
audit_start (const char *kind)
{
  fprintf (audit_file, "{\"kind\":");
  audit_string (kind);
  fprintf (audit_file, ",\"seq\":%lu,\"unit_id\":", audit_seq++);
  audit_string (audit_unit);
  fputs (",\"invocation_id\":", audit_file);
  audit_string (audit_invocation);
}

static void
audit_end (void)
{
  fputs ("}\n", audit_file);
  if (ferror (audit_file))
    audit_error ("write");
}

static void
audit_source (const char *filename, int line)
{
  fputs ("{\"path\":", audit_file);
  audit_string (filename);
  fprintf (audit_file, ",\"line\":%d}", line);
}

static void
audit_symbol_id (symbol *sym)
{
  char id[2 * SDCC_NAME_MAX + 96];
  if (!sym)
    { fputs ("null", audit_file); return; }
  /* 地址、临时 key 会变；只把作用域和链接名作为本 TU 的符号事实。 */
  snprintf (id, sizeof (id), "%s|L%ld|B%d|%s", sym->name, sym->level,
            sym->block, sym->rname);
  audit_string (id);
}

static void
audit_link_name (symbol *sym)
{
  audit_string (sym && sym->rname[0] ? sym->rname : NULL);
}

static void
audit_site_id (void)
{
  char id[80];
  snprintf (id, sizeof (id), "%s:s%lu",
            audit_body_id[0] ? audit_body_id : "unit", ++audit_site_serial);
  audit_string (id);
}

static void
audit_opaque (const char *category, const char *reason,
              const char *filename, int line, const char *raw)
{
  if (!audit_file || audit_failed)
    return;
  audit_start ("opaque");
  fputs (",\"body_id\":", audit_file);
  audit_string (audit_body_id[0] ? audit_body_id : NULL);
  fputs (",\"source\":", audit_file);
  audit_source (filename, line);
  fputs (",\"category\":", audit_file); audit_string (category);
  fputs (",\"reason\":", audit_file); audit_string (reason);
  if (raw)
    { fputs (",\"raw_text\":", audit_file); audit_string (raw); }
  audit_end ();
  ++audit_total.opaque;
  if (audit_body_id[0]) ++audit_body.opaque;
}

static bool audit_type (sym_link *type, unsigned depth);
static bool audit_signature (sym_link *type, unsigned depth);

static const char *
audit_scalar_kind (sym_link *type)
{
  switch (type->select.s.noun)
    {
    case V_VOID: return "void";
    case V_BOOL: return "bool";
    case V_CHAR: return "char";
    case V_INT: return type->select.s.b_isenum ? "enum" : "int";
    case V_FLOAT: case V_DOUBLE: return "float";
    case V_STRUCT: return "aggregate";
    default: return "unknown";
    }
}

static void
audit_qualifiers (bool c, bool v, bool r, bool a, bool optional)
{
  const char *names[] = {"const", "volatile", "restrict", "atomic", "optional"};
  bool values[] = {c, v, r, a, optional};
  bool first = true;
  unsigned n;
  fputc ('[', audit_file);
  for (n = 0; n < sizeof (values) / sizeof (values[0]); ++n)
    if (values[n])
      {
        if (!first) fputc (',', audit_file);
        audit_string (names[n]);
        first = false;
      }
  fputc (']', audit_file);
}

static bool
audit_flat_aggregate (sym_link *type)
{
  char token[2048] = "";
  if (!mcs251FlatAggregateToken (type, token, sizeof token)) return false;
  structdef *def = SPEC_STRUCT (type);
  fputs ("{\"kind\":\"aggregate\",\"size_bytes\":", audit_file);
  fprintf (audit_file, "%u,\"signed\":null,\"address_space\":null,\"qualifiers\":", def->size);
  audit_qualifiers (isConst(type), isVolatile(type), isRestrict(type), false, false);
  fputs (",\"detail\":{\"flat_pointee_schema\":1,\"category\":\"struct\",\"tag\":", audit_file);
  audit_string (def->tagsym ? def->tag : NULL);
  fputs (",\"layout_token\":", audit_file); audit_string(token);
  fputs (",\"fields\":[", audit_file);
  bool first=true;
  for (symbol *field=def->fields; field; field=field->next)
    {
      if (!first) fputc(',',audit_file); first=false;
      fputs ("{\"name\":",audit_file); audit_string(field->name);
      fprintf (audit_file,",\"offset\":%u,\"source\":",field->offset);
      audit_source(field->fileDef,field->lineDef);
      fputs (",\"type\":",audit_file); audit_type(field->type,1);
      fputc('}',audit_file);
    }
  fputs (" ]},\"classification\":\"flat-pointee-layout-v1\",\"unknown_reason\":null}",audit_file);
  return true;
}

static bool
audit_type (sym_link *type, unsigned depth)
{
  bool known = false;
  if (!type || depth > AUDIT_TYPE_DEPTH)
    {
      fputs ("{\"kind\":\"unknown\",\"size_bytes\":null,\"signed\":null,"
             "\"address_space\":null,\"qualifiers\":[],\"classification\":\"unknown\","
             "\"unknown_reason\":\"missing type or bounded type recursion\"}", audit_file);
      return false;
    }
  if (IS_STRUCT (type) && audit_flat_aggregate (type)) return true;
  if (IS_SPEC (type))
    {
      const char *kind = audit_scalar_kind (type);
      known = strcmp (kind, "unknown") && strcmp (kind, "aggregate");
      fprintf (audit_file, "{\"kind\":");
      audit_string (kind);
      fputs (",\"size_bytes\":", audit_file);
      if (known) fprintf (audit_file, "%u", (unsigned)getSize (type));
      else fputs ("null", audit_file);
      fputs (",\"signed\":", audit_file);
      if (type->select.s.noun == V_INT || type->select.s.noun == V_CHAR)
        fputs (audit_bool (!type->select.s.b_unsigned), audit_file);
      else fputs ("null", audit_file);
      fputs (",\"address_space\":", audit_file);
      audit_string (type->select.s.addrspace ? type->select.s.addrspace->name : NULL);
      fputs (",\"qualifiers\":", audit_file);
      audit_qualifiers (type->select.s.b_const, type->select.s.b_volatile,
                        type->select.s.b_restrict, type->select.s.b_atomic,
                        type->select.s.b_optional);
      fprintf (audit_file, ",\"detail\":{\"noun\":%d,\"storage_class\":%d,"
               "\"long\":%s,\"long_long\":%s,\"short\":%s,\"implicit_sign\":%s,"
               "\"bitint_width\":%u},\"classification\":",
               (int)type->select.s.noun, (int)type->select.s.sclass,
               audit_bool (type->select.s.b_long), audit_bool (type->select.s.b_longlong),
               audit_bool (type->select.s.b_short), audit_bool (type->select.s.b_implicit_sign),
               type->select.s.bitintwidth);
      audit_string (known ? "supported" : "unknown");
      fputs (",\"unknown_reason\":", audit_file);
      audit_string (known ? NULL : "non-scalar or unsupported specifier");
      fputc ('}', audit_file);
      return known;
    }
  fputs ("{\"kind\":", audit_file);
  audit_string (IS_FUNC (type) ? "function" : IS_PTR (type) ? "pointer" : "unknown");
  fputs (",\"size_bytes\":", audit_file);
  if (IS_PTR (type)) fprintf (audit_file, "%u", (unsigned)getSize (type));
  else fputs ("null", audit_file);
  fputs (",\"signed\":null,\"address_space\":", audit_file);
  audit_string (IS_PTR (type) && type->select.d.ptr_addrspace
                ? type->select.d.ptr_addrspace->name : NULL);
  fputs (",\"qualifiers\":", audit_file);
  audit_qualifiers (type->select.d.ptr_const, type->select.d.ptr_volatile,
                    type->select.d.ptr_restrict, type->select.d.ptr_atomic,
                    type->select.d.ptr_optional);
  fprintf (audit_file, ",\"detail\":{\"declarator\":%d", (int)type->select.d.dcl_type);
  if (IS_ARRAY (type))
    fprintf (audit_file, ",\"elements\":%lu,\"length_type\":%d",
             (unsigned long)type->select.d.num_elem, (int)type->select.d.array_length_type);
  if (IS_FUNC (type))
    { fputs (",\"signature\":", audit_file); audit_signature (type, depth + 1); }
  else
    { fputs (",\"next\":", audit_file); audit_type (type->next, depth + 1); }
  fputs ("},\"classification\":\"unknown\",\"unknown_reason\":"
         "\"non-scalar type is outside direct-scalar-v1 proof\"}", audit_file);
  return false;
}

static bool
audit_signature (sym_link *type, unsigned depth)
{
  value *arg;
  unsigned ordinal = 0;
  bool known, first = true;
  if (IS_FUNCPTR (type)) type = type->next;
  if (!IS_FUNC (type) || depth > AUDIT_TYPE_DEPTH)
    {
      fputs ("{\"prototype\":\"unknown\",\"abi\":null,\"return_type\":null,"
             "\"parameters\":[],\"varargs\":null,\"attributes\":{},"
             "\"classification\":\"unknown\",\"unknown_reason\":"
             "\"missing function type or bounded signature recursion\"}", audit_file);
      return false;
    }
  known = !type->funcAttrs.oldStyle && !type->funcAttrs.noprototype;
  fputs ("{\"prototype\":", audit_file);
  audit_string (type->funcAttrs.oldStyle ? "old_style" :
                type->funcAttrs.noprototype ? "unknown" : "complete");
  fprintf (audit_file, ",\"abi\":%d,\"return_type\":", type->funcAttrs.sdcccall);
  known = audit_type (type->next, depth + 1) && known;
  fputs (",\"parameters\":[", audit_file);
  for (arg = type->funcAttrs.args; arg && ordinal < AUDIT_PARAM_LIMIT; arg = arg->next)
    {
      if (!first) fputc (',', audit_file);
      fprintf (audit_file, "{\"ordinal\":%u,\"type\":", ++ordinal);
      known = audit_type (arg->type, depth + 1) && known;
      fputs (",\"argreg\":", audit_file);
      if (IS_SPEC (arg->etype)) fprintf (audit_file, "%d", arg->etype->select.s.argreg);
      else fputs ("null", audit_file);
      fputc ('}', audit_file);
      first = false;
    }
  if (arg) known = false;
  fprintf (audit_file, "],\"varargs\":%s,\"attributes\":{"
           "\"reentrant\":%s,\"isr\":%s,\"interrupt_number\":%u,"
           "\"banked\":%s,\"nonbanked\":%s,\"register_bank\":%d,"
           "\"has_register_bank\":%s,\"naked\":%s,\"critical\":%s,"
           "\"callee_saves\":%s,\"noreturn\":%s,\"builtin\":%s,"
           "\"stack_parameters\":%u,\"overlay\":%u,"
           "\"smallc\":%s,\"raisonance\":%s,\"iar\":%s,\"cosmic\":%s,"
           "\"dynamicc\":%s,\"z88dk_fastcall\":%s,\"z88dk_callee\":%s},"
           "\"classification\":",
           audit_bool (type->funcAttrs.hasVargs), audit_bool (type->funcAttrs.reent),
           audit_bool (type->funcAttrs.intrtn), type->funcAttrs.intno,
           audit_bool (type->funcAttrs.banked), audit_bool (type->funcAttrs.nonbanked),
           (int)type->funcAttrs.regbank, audit_bool (type->funcAttrs.rbank),
           audit_bool (type->funcAttrs.naked), audit_bool (type->funcAttrs.critical),
           audit_bool (type->funcAttrs.calleeSaves), audit_bool (type->funcAttrs.noreturn),
           audit_bool (type->funcAttrs.builtin), type->funcAttrs.hasStackParms,
           type->funcAttrs.overlay, audit_bool (type->funcAttrs.smallc),
           audit_bool (type->funcAttrs.raisonance), audit_bool (type->funcAttrs.iar),
           audit_bool (type->funcAttrs.cosmic), audit_bool (type->funcAttrs.dynamicc),
           audit_bool (type->funcAttrs.z88dk_fastcall), audit_bool (type->funcAttrs.z88dk_callee));
  audit_string (known ? "supported" : "unknown");
  fputs (",\"unknown_reason\":", audit_file);
  audit_string (known ? NULL : "incomplete prototype, unsupported type or parameter bound");
  fputc ('}', audit_file);
  return known;
}

static void
audit_counts_json (const audit_counts *counts, bool unit)
{
  fputc ('{', audit_file);
  if (unit)
    fprintf (audit_file, "\"declarations\":%lu,\"bodies\":%lu,",
             counts->declarations, counts->bodies);
  fprintf (audit_file, "\"calls\":%lu,\"pcalls\":%lu,\"references\":%lu,\"opaque\":%lu}",
           counts->calls, counts->pcalls, counts->references, counts->opaque);
}

void
mcs251ReturnAuditInit (const char *path, const char *invocation_id)
{
  if (!path || !*path) return;
  if (audit_file)
    { errno = EINVAL; audit_error ("duplicate initialization"); return; }
  audit_path = path;
  audit_invocation = invocation_id ? invocation_id : "";
  audit_unit = dstFileName ? dstFileName : fullSrcFileName;
  audit_seq = audit_body_serial = audit_site_serial = audit_nodes = 0;
  memset (&audit_total, 0, sizeof (audit_total));
  memset (&audit_body, 0, sizeof (audit_body));
  audit_body_id[0] = 0;
  audit_failed = audit_snapshot_done = audit_snapshot_complete = false;
  audit_ir_complete = true;
  audit_file = fopen (path, "wb");
  if (!audit_file) { audit_error ("open"); return; }
  audit_start ("unit_begin");
  fputs (",\"schema\":\"" AUDIT_SCHEMA "\",\"source\":", audit_file);
  audit_string (fullSrcFileName);
  fputs (",\"target\":\"mcs251\",\"model\":", audit_file);
  audit_string (options.model == MODEL_SMALL ? "small" :
                options.model == MODEL_LARGE ? "large" : "other-explicit");
  fprintf (audit_file, ",\"model_value\":%d,\"default_abi\":%d,"
           "\"compiler_version\":", options.model, options.sdcccall);
#ifdef SDCC_VERSION_STR
  audit_string (SDCC_VERSION_STR);
#else
  audit_string ("unknown");
#endif
  fprintf (audit_file, ",\"options\":{\"stack_auto\":%s,\"use_xstack\":%s,"
           "\"no_reg_params\":%s,\"wide\":%s,\"wide_ordinary\":%s},"
           "\"coverage_details\":{\"capture_stage\":\"post-common-optimization-pre-backend-dispatch\","
           "\"declaration_history\":\"final_snapshot_only\","
           "\"purpose\":\"audit-only; never modifies generated ABI\"}",
           audit_bool (options.stackAuto != 0), audit_bool (options.useXstack != 0),
           audit_bool (options.noRegParams != 0), audit_bool (mcs251WideEnabled != 0),
           audit_bool (mcs251WideOrdinaryEnabled != 0));
  audit_end ();
}

static void
audit_reference (symbol *target, const char *kind, const char *owner_kind,
                 symbol *owner, const char *filename, int line, const char *evidence,
                 const iCode *ic, const char *operand_role)
{
  audit_start ("reference");
  fputs (",\"body_id\":", audit_file);
  audit_string (audit_body_id[0] ? audit_body_id : NULL);
  fputs (",\"owner_kind\":", audit_file); audit_string (owner_kind);
  fputs (",\"owner_symbol_id\":", audit_file); audit_symbol_id (owner);
  fputs (",\"site_id\":", audit_file); audit_site_id ();
  fputs (",\"source\":", audit_file); audit_source (filename, line);
  fputs (",\"reference_kind\":", audit_file); audit_string (kind);
  fputs (",\"target_symbol_id\":", audit_file); audit_symbol_id (target);
  fputs (",\"target_link_name\":", audit_file); audit_link_name (target);
  fputs (",\"evidence\":", audit_file); audit_string (evidence);
  fputs (",\"icode_op\":", audit_file);
  if (ic) fprintf (audit_file, "%u", ic->op); else fputs ("null", audit_file);
  fputs (",\"operand_role\":", audit_file); audit_string (operand_role);
  fputs (",\"unknown_reason\":null", audit_file);
  audit_end ();
  ++audit_total.references;
  if (audit_body_id[0]) ++audit_body.references;
}


#include "audit_arguments.c.inc"
#include "audit_field_objects.c.inc"

static void
audit_first_argument (const iCode *call, symbol *target)
{
  const iCode *p, *send = NULL;
  value *formal = target && IS_FUNC (target->type) ? FUNC_ARGS (target->type) : NULL;
  unsigned walked = 0;
  bool duplicate = false;
  operand *arg = NULL;
  sym_link *type = NULL;
  double bits = -1;
  fputs (",\"actual_first_argument\":{\"ordinal\":1", audit_file);
  if (call->op == CALL && formal && FUNC_SDCCCALL (target->type) == 4 &&
      IS_CHAR (formal->type) && getSize (formal->type) == 1 &&
      SPEC_USIGN (getSpec (formal->type)) && !IS_VOLATILE (formal->type) && !isAtomic (formal->type))
    for (p = call->prev; p && ++walked <= 1024; p = p->prev)
      {
        if (p->eBBlockNum != call->eBBlockNum || p->op == CALL || p->op == PCALL ||
            p->op == INLINEASM || p->op == LABEL || p->op == GOTO || p->op == IFX ||
            p->op == FUNCTION || p->op == RETURN || p->op == JUMPTABLE) break;
        if (p->op == SEND && p->argreg == 1)
          { if (send) { duplicate = true; break; } send = p; }
      }
  if (send && !duplicate && walked <= 1024)
    {
      arg = IC_LEFT (send);
      type = arg ? operandType (arg) : NULL;
      if (IS_OP_LITERAL (arg) && type && IS_CHAR (type) && getSize (type) == 1 &&
          SPEC_USIGN (getSpec (type)) && !IS_VOLATILE (type) && !isAtomic (type))
        bits = operandLitValue (arg);
    }
  if (bits < 0 || bits > 255 || bits != (unsigned) bits)
    fputs (",\"status\":\"unknown\",\"reason\":\"no-unique-same-block-typed-u8-literal-send\"}", audit_file);
  else
    fprintf (audit_file, ",\"status\":\"typed-literal\",\"type\":\"u8\",\"bits\":%u,"
             "\"send_icode_key\":%d,\"send_argreg\":1,\"block\":%d,\"walked_nodes\":%u}",
             (unsigned) bits, send->key, call->eBBlockNum, walked);
}

static void
audit_call (iCode *ic,ebbIndex *ebbi)
{
  operand *left = IC_LEFT (ic), *result = IC_RESULT (ic);
  symbol *target = IS_SYMOP (left) ? OP_SYMBOL (left) : NULL;
  bool direct = ic->op == CALL && target && !target->isitmp && IS_FUNC (target->type);
  bool known;
  audit_start ("call");
  fputs (",\"body_id\":", audit_file); audit_string (audit_body_id);
  fputs (",\"site_id\":", audit_file); audit_site_id ();
  fprintf (audit_file, ",\"icode_key\":%d,\"source\":", ic->key);
  audit_source (ic->filename, ic->lineno);
  fputs (",\"op\":", audit_file); audit_string (ic->op == CALL ? "CALL" : "PCALL");
  fputs (",\"target_kind\":", audit_file);
  audit_string (direct ? "direct_symbol" : ic->op == PCALL ? "indirect" : "unknown");
  fputs (",\"target_symbol_id\":", audit_file); audit_symbol_id (direct ? target : NULL);
  fputs (",\"target_link_name\":", audit_file); audit_link_name (direct ? target : NULL);
  fputs (",\"effective_signature\":", audit_file);
  known = audit_signature (left ? operandType (left) : NULL, 0);
  audit_first_argument (ic, direct ? target : NULL);
  audit_actual_arguments (ic, direct ? target : NULL);
  audit_field_objects (ic, direct ? target : NULL,ebbi);
  fprintf (audit_file, ",\"stack_argument_bytes\":%d,\"result_type\":", ic->parmBytes);
  if (result) audit_type (operandType (result), 0); else fputs ("null", audit_file);
  fputs (",\"result_symbol_id\":", audit_file);
  audit_symbol_id (IS_SYMOP (result) ? OP_SYMBOL (result) : NULL);
  fprintf (audit_file, ",\"support_routine\":%s,\"inlined\":%s",
           audit_bool (ic->supportRtn), audit_bool (ic->inlined));
  audit_end ();
  ++audit_total.calls; ++audit_body.calls;
  if (ic->op == PCALL) { ++audit_total.pcalls; ++audit_body.pcalls; }
  if (!known) audit_opaque ("unsupported_type", "call effective signature is not supported",
                            ic->filename, ic->lineno, NULL);
  if (!direct) audit_opaque ("unknown_symbol", "call target is not a proven direct function symbol",
                             ic->filename, ic->lineno, NULL);
}

void
mcs251ReturnAuditObserveIR (ebbIndex *ebbi)
{
  unsigned entries = 0, ends = 0;
  symbol *entry = NULL, *end = NULL;
  bool bounds_ok = true, known_signature = true;
  unsigned long walked = 0;
  int b;
  if (!audit_file || audit_failed) return;
  memset (&audit_body, 0, sizeof (audit_body));
  snprintf (audit_body_id, sizeof (audit_body_id), "body-%lu", ++audit_body_serial);
  audit_body_complete = true;
  if (!ebbi || ebbi->count < 0 || !ebbi->bbOrder) bounds_ok = false;
  if (bounds_ok)
    for (b = 0; b < ebbi->count; ++b)
      {
        eBBlock *bb = ebbi->bbOrder[b];
        iCode *ic;
        bool reached_end;
        if (!bb || (!bb->sch != !bb->ech)) { bounds_ok = false; continue; }
        reached_end = !bb->sch;
        for (ic = bb->sch; ic; ic = ic == bb->ech ? NULL : ic->next)
          {
            if (++walked > AUDIT_NODE_LIMIT) { bounds_ok = false; break; }
            if (ic == bb->ech) reached_end = true;
            if (ic->op == FUNCTION)
              { ++entries; entry = IS_SYMOP (IC_LEFT (ic)) ? OP_SYMBOL (IC_LEFT (ic)) : NULL; }
            if (ic->op == ENDFUNCTION)
              { ++ends; end = IS_SYMOP (IC_LEFT (ic)) ? OP_SYMBOL (IC_LEFT (ic)) : NULL; }
          }
        if (!reached_end) bounds_ok = false;
        if (walked > AUDIT_NODE_LIMIT) break;
      }
  audit_owner = bounds_ok && entries == 1 && ends == 1 && entry && entry == end &&
                IS_FUNC (entry->type) ? entry : NULL;
  audit_start ("body_begin");
  fputs (",\"body_id\":", audit_file); audit_string (audit_body_id);
  fputs (",\"owner_kind\":", audit_file);
  audit_string (audit_owner ? "function" : !entries && !ends ? "initializer" : "unknown");
  fputs (",\"owner_symbol_id\":", audit_file); audit_symbol_id (audit_owner);
  fputs (",\"owner_link_name\":", audit_file); audit_link_name (audit_owner);
  fputs (",\"context_function_hint\":", audit_file); audit_symbol_id (currFunc);
  fprintf (audit_file, ",\"function_boundary\":{\"matching_function_count\":%u,"
           "\"matching_endfunction_count\":%u},\"signature\":", entries, ends);
  if (audit_owner) known_signature = audit_signature (audit_owner->type, 0);
  else fputs ("null", audit_file);
  {
    unsigned bits;
    if (audit_owner && mcs251ConstantParameterApplied (audit_owner, &bits))
      fprintf (audit_file, ",\"constant_first_parameter\":{\"applied\":true,\"ordinal\":1,\"type\":\"u8\",\"bits\":%u}", bits);
  }
  fputs (",\"constant_parameters\":[", audit_file);
  {
    unsigned count=0;
    for(unsigned ordinal=1;ordinal<=2;++ordinal) {
      unsigned bits;
      if(audit_owner && mcs251ConstantParameterAppliedAt(audit_owner,ordinal,&bits)) {
        if(count++)fputc(',',audit_file);
        fprintf(audit_file,"{\"applied\":true,\"ordinal\":%u,\"type\":\"u8\",\"bits\":%u}",ordinal,bits);
      }
    }
  }
  fputc(']',audit_file);
  fputs (",\"source\":", audit_file);
  audit_source (audit_owner ? audit_owner->fileDef : NULL, audit_owner ? audit_owner->lineDef : 0);
  audit_end ();
  ++audit_total.bodies;
  if (!known_signature)
    audit_opaque ("unsupported_type", "body signature is not supported",
                   audit_owner->fileDef, audit_owner->lineDef, NULL);
  if (!audit_owner)
    audit_opaque ("unknown_owner", "IR has no unique matching function boundaries; currFunc is only a hint",
                   NULL, 0, NULL);
  if (audit_owner && audit_owner->type->funcAttrs.intrtn)
    audit_reference (audit_owner, "isr_vector", "vector", NULL,
                      audit_owner->fileDef, audit_owner->lineDef, "function ISR attribute", NULL, NULL);
  walked = 0;
  if (bounds_ok)
    for (b = 0; b < ebbi->count; ++b)
      {
        eBBlock *bb = ebbi->bbOrder[b];
        iCode *ic;
        if (!bb) { audit_body_complete = false; continue; }
        for (ic = bb->sch; ic; ic = ic == bb->ech ? NULL : ic->next)
          {
            operand *ops[3];
            unsigned n;
            if (++walked > AUDIT_NODE_LIMIT || ++audit_nodes > AUDIT_NODE_LIMIT)
              { audit_body_complete = false; break; }
            if (ic->op == CALL || ic->op == PCALL) audit_call (ic,ebbi);
            if (ic->op == INLINEASM)
              audit_opaque ("inline_asm", "inline assembly needs independent final-code reconciliation",
                             ic->filename, ic->lineno, ic->inlineAsm);
            ops[0] = IC_LEFT (ic); ops[1] = IC_RIGHT (ic); ops[2] = IC_RESULT (ic);
            for (n = 0; n < 3; ++n)
              if (IS_SYMOP (ops[n]) && OP_SYMBOL (ops[n]) && IS_FUNC (OP_SYMBOL (ops[n])->type))
                {
                  if (n == 0 && (ic->op == FUNCTION || ic->op == ENDFUNCTION || ic->op == CALL || ic->op == RECEIVE))
                    continue;
                  audit_reference (OP_SYMBOL (ops[n]), ic->op == SEND ? "argument" :
                                    ic->op == RETURN ? "return_value" : "function_value_operand",
                                    audit_owner ? "function" : "initializer", audit_owner,
                                    ic->filename, ic->lineno, "optimized iCode function-valued operand",
                                    ic, n == 0 ? "left" : n == 1 ? "right" : "result");
                }
          }
        if (!audit_body_complete) break;
      }
  else audit_body_complete = false;
  if (!audit_body_complete)
    {
      audit_ir_complete = false;
      audit_opaque ("incomplete_capture", "IR traversal malformed or audit node bound reached", NULL, 0, NULL);
    }
  audit_start ("body_end");
  fputs (",\"body_id\":", audit_file); audit_string (audit_body_id);
  fputs (",\"counts\":", audit_file); audit_counts_json (&audit_body, false);
  fprintf (audit_file, ",\"coverage\":{\"optimized_ir_nodes\":%s,"
           "\"body_ownership\":%s,\"function_value_operands\":%s},"
           "\"coverage_details\":\"pre-backend IR only; generated helper calls require object reconciliation\"",
           audit_body_complete ? "true" : "\"unknown\"",
           audit_owner ? "true" : "\"unknown\"",
           audit_body_complete ? "true" : "\"unknown\"");
  audit_end ();
  audit_body_id[0] = 0;
  audit_owner = NULL;
}

static bool
audit_mark_symbol (symbol *sym)
{
  size_t n;
  if (!sym) return false;
  for (n = 0; n < audit_seen_count; ++n) if (audit_seen[n] == sym) return false;
  if (audit_seen_count == audit_seen_capacity)
    {
      size_t capacity = audit_seen_capacity ? audit_seen_capacity * 2 : 128;
      symbol **items = (symbol **)realloc (audit_seen, capacity * sizeof (*items));
      if (!items) { audit_error ("symbol snapshot allocation"); return false; }
      audit_seen = items; audit_seen_capacity = capacity;
    }
  audit_seen[audit_seen_count++] = sym;
  return true;
}

static void
audit_initializer_ast (ast *tree, symbol *owner, unsigned depth)
{
  if (!tree || audit_failed) return;
  if (depth > 128 || ++audit_nodes > AUDIT_NODE_LIMIT)
    { audit_opaque ("unsupported_initializer", "bounded initializer AST traversal", NULL, 0, NULL); return; }
  if (IS_AST_SYM_VALUE (tree) && IS_FUNC (AST_SYMBOL (tree)->type))
    audit_reference (AST_SYMBOL (tree), "initializer", "global", owner,
                      tree->filename, tree->lineno, "initializer AST function symbol, including implicit decay", NULL, NULL);
  if (tree->type == EX_OPERAND)
    {
      operand *op = tree->opval.oprnd;
      if (IS_SYMOP (op) && IS_FUNC (OP_SYMBOL (op)->type))
        audit_reference (OP_SYMBOL (op), "initializer", "global", owner,
                          tree->filename, tree->lineno, "initializer AST operand function symbol", NULL, NULL);
      else
        audit_opaque ("unsupported_initializer", "initializer contains non-function EX_OPERAND",
                       tree->filename, tree->lineno, NULL);
    }
  if (IS_AST_OP (tree) && tree->opval.op == INLINEASM)
    audit_opaque ("inline_asm", "initializer AST contains opaque inline assembly",
                   tree->filename, tree->lineno, tree->values.inlineasm);
  audit_initializer_ast (tree->left, owner, depth + 1);
  audit_initializer_ast (tree->right, owner, depth + 1);
  if (IS_FOR_STMT (tree))
    {
      audit_initializer_ast (tree->values.forVals.initExpr, owner, depth + 1);
      audit_initializer_ast (tree->values.forVals.condExpr, owner, depth + 1);
      audit_initializer_ast (tree->values.forVals.loopExpr, owner, depth + 1);
    }
}

static void
audit_initializer_list (initList *list, symbol *owner, unsigned depth)
{
  unsigned long count = 0;
  for (; list && !audit_failed; list = list->next)
    {
      if (depth > 128 || ++count > AUDIT_NODE_LIMIT || audit_nodes > AUDIT_NODE_LIMIT)
        { audit_opaque ("unsupported_initializer", "bounded initializer list traversal",
                         list->filename, list->lineno, NULL); break; }
      if (list->type == INIT_NODE) audit_initializer_ast (list->init.node, owner, depth + 1);
      else if (list->type == INIT_DEEP) audit_initializer_list (list->init.deep, owner, depth + 1);
      else if (list->type != INIT_HOLE)
        audit_opaque ("unsupported_initializer", "unknown initializer list kind",
                       list->filename, list->lineno, NULL);
    }
}

static void
audit_snapshot_symbol (symbol *sym, const char *origin)
{
  bool known;
  if (!audit_mark_symbol (sym) || audit_failed) return;
  if (IS_FUNC (sym->type))
    {
      audit_start ("declaration");
      fputs (",\"symbol_id\":", audit_file); audit_symbol_id (sym);
      fputs (",\"name\":", audit_file); audit_string (sym->name);
      fputs (",\"link_name\":", audit_file); audit_link_name (sym);
      fputs (",\"linkage\":", audit_file);
      audit_string (!IS_SPEC (sym->etype) ? "unknown" : sym->etype->select.s.b_absadr ? "absolute" :
                    sym->etype->select.s.b_static ? "internal" : "external");
      fprintf (audit_file, ",\"level\":%ld,\"block\":%d,\"origin\":", sym->level, sym->block);
      audit_string (origin);
      fputs (",\"source\":", audit_file); audit_source (sym->fileDef, sym->lineDef);
      fprintf (audit_file, ",\"has_body\":%s,\"generated\":%s,\"compiler_addrtaken_flag\":%s,"
               "\"signature\":", audit_bool (sym->type->funcAttrs.hasbody),
               audit_bool (sym->generated), audit_bool (sym->addrtaken));
      known = audit_signature (sym->type, 0);
      audit_end ();
      ++audit_total.declarations;
      if (!known) audit_opaque ("unsupported_type", "final declaration signature is not supported",
                                sym->fileDef, sym->lineDef, NULL);
    }
  if (sym->ival) audit_initializer_list (sym->ival, sym, 0);
}

static void
audit_snapshot_set (set *symbols, const char *origin)
{
  set *item;
  unsigned long count = 0;
  /* 不调用 setFirstItem/setNextItem，避免改动编译器集合自带的迭代游标。 */
  for (item = symbols; item && !audit_failed; item = item->next)
    {
      if (++count > AUDIT_NODE_LIMIT)
        { audit_snapshot_complete = false;
          audit_opaque ("incomplete_capture", "bounded symbol set traversal", NULL, 0, NULL); return; }
      audit_snapshot_symbol ((symbol *)item->item, origin);
    }
}

void
mcs251ReturnAuditScanSymbolsBeforeGlue (void)
{
  memmap *maps[] = {xstack, istack, code, data, initialized, initializer, pdata,
    xdata, xconst, xidata, xinit, idata, bit, statsg, c_abs, x_abs, i_abs, d_abs,
    sfr, sfrbit, reg, generic, overlay, eeprom, home};
  unsigned i;
  unsigned long chain_nodes = 0;
  bucket *entry;
  namedspacemap *named;
  set *overlay_set;
  if (!audit_file || audit_failed) return;
  audit_snapshot_complete = true;
  for (i = 0; i < HASHTAB_SIZE && audit_snapshot_complete && !audit_failed; ++i)
    {
      for (entry = SymbolTab[i]; entry && !audit_failed; entry = entry->next)
        {
          if (++chain_nodes > AUDIT_NODE_LIMIT) { audit_snapshot_complete = false; break; }
          audit_snapshot_symbol ((symbol *)entry->sym, "SymbolTab");
        }
      for (entry = SymbolTabE[i]; entry && audit_snapshot_complete && !audit_failed; entry = entry->next)
        {
          if (++chain_nodes > AUDIT_NODE_LIMIT) { audit_snapshot_complete = false; break; }
          audit_snapshot_symbol ((symbol *)entry->sym, "SymbolTabE");
        }
    }
  for (i = 0; i < sizeof (maps) / sizeof (maps[0]); ++i)
    if (maps[i]) audit_snapshot_set (maps[i]->syms, "memmap");
  for (named = namedspacemaps; named && audit_snapshot_complete && !audit_failed; named = named->next)
    {
      if (++chain_nodes > AUDIT_NODE_LIMIT) { audit_snapshot_complete = false; break; }
      if (named->map) audit_snapshot_set (named->map->syms, "memmap");
    }
  for (overlay_set = ovrSetSets; overlay_set && audit_snapshot_complete && !audit_failed; overlay_set = overlay_set->next)
    {
      if (++chain_nodes > AUDIT_NODE_LIMIT) { audit_snapshot_complete = false; break; }
      audit_snapshot_set ((set *)overlay_set->item, "overlay");
    }
  if (!audit_snapshot_complete)
    audit_opaque ("incomplete_capture", "symbol snapshot traversal was incomplete", NULL, 0, NULL);
  audit_snapshot_done = !audit_failed && audit_snapshot_complete;
  /* 已发射并被 flushStatics 清除的 AST 不在快照内，不能声称没有逃逸。 */
}

void
mcs251ReturnAuditFinishAfterGlue (bool success)
{
  FILE *file;
  if (!audit_file) return;
  if (!audit_failed)
    {
      audit_start ("unit_end");
      fprintf (audit_file, ",\"success\":%s,\"counts\":", audit_bool (success && !fatalError));
      audit_counts_json (&audit_total, true);
      fprintf (audit_file, ",\"coverage\":{\"common_ir_capture\":%s,"
               "\"ordinary_and_wide_dispatch\":%s,\"final_symbol_snapshot\":%s,"
               "\"declaration_history\":\"unknown\",\"initializer_reference_coverage\":\"unknown\","
               "\"source_history_complete\":\"unknown\",\"final_codegen_references\":\"unknown\"},"
               "\"coverage_details\":{\"declaration_history\":\"final_snapshot_only\","
               "\"initializer_reference_coverage\":\"flushStatics may emit and clear symbols before final glue\","
               "\"source_history_complete\":\"not inferred from final snapshot\","
               "\"final_codegen_references\":\"emitter/outliner additions require host object reconciliation\","
               "\"calls_count\":\"CALL plus PCALL; pcalls is a subset\"}",
               audit_ir_complete ? "true" : "\"unknown\"",
               audit_ir_complete ? "true" : "\"unknown\"",
               audit_snapshot_done ? "true" : "\"unknown\"");
      audit_end ();
    }
  if (fflush (audit_file)) audit_error ("flush");
  file = audit_file;
  audit_file = NULL;
  if (fclose (file)) audit_error ("close");
  free (audit_seen);
  audit_seen = NULL;
  audit_seen_count = audit_seen_capacity = 0;
}
