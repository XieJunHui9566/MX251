/* SPDX-License-Identifier: GPL-2.0-or-later
 * 重复宽参数装载的编码成本抽取。默认关闭；不改参数 ABI 或寄存器分配。
 * 只接受 WR <- 16 位常量 / 已有 SPX 有符号位移字读取，每条至少 4 B。
 * ISR 只开放纯寄存器常量，绝不把现场保存、SFR 或栈读取移入 helper。
 */
static bool
m251ShortWideLine (lineNode *p, bool interrupt, char *key, char *body)
{
  unsigned reg; int n = 0; char operand[128];
  if (!p->ic || !m251OutlineLine (p, key, body) ||
      sscanf (key, "mov wr%u,%127s%n", &reg, operand, &n) != 2 ||
      key[n] || reg > 14 || (reg & 1) || reg == 10)
    return FALSE;
  /* strtoul 的范围与末尾检查拒绝符号重定位和溢出；不猜编码宽度。 */
  if (operand[0] == '#')
    {
      char *end; const char *start = operand + 1;
      if (!isdigit ((unsigned char)*start)) return FALSE;
      unsigned long value = strtoul (start, &end, 0);
      return *end == 0 && value <= 65535UL;
    }
  if (interrupt || strncmp (operand, "@spx-", 5)) return FALSE;
  /* m251OutlineLine 已检查位移界限，并按新增的 3 B 返回地址重定基。
   * 不开放 @SPX 无位移短编码，避免低估 helper 增长。 */
  return TRUE;
}

static void
mcs251OutlineShortWideLoads (lineNode *head, struct dbuf_s *helpers)
{
  const char *enabled = getenv ("MX251_SHORT_WIDE_OUTLINE");
  const char *disabled = getenv ("MX251_DISABLE_OUTLINING");
  if (!enabled || strcmp (enabled, "1") || (disabled && !strcmp (disabled, "1")) ||
      !currFunc || options.useXstack || IFFUNC_ISNAKED (currFunc->type) ||
      IFFUNC_CALLEESAVES (currFunc->type) || FUNC_REGBANK (currFunc->type)) return;
  bool interrupt = IFFUNC_ISISR (currFunc->type);
  unsigned capacity = 0, count = 0, run = 1;
  lineNode *last = NULL;
  for (lineNode *p = head; p; p = p->next)
    {
      if (p->isInline || (p->ic && (p->ic->op == CRITICAL || p->ic->op == ENDCRITICAL))) return;
      if (!p->isLabel && !p->isComment && !p->isDebug) last = p;
      ++capacity;
    }
  if (!last || (!lineIsInst (last, "eret") && !lineIsInst (last, "ret") &&
      !lineIsInst (last, "reti") && !lineIsInst (last, "ejmp") &&
      !lineIsInst (last, "ljmp") && !lineIsInst (last, "sjmp"))) return;
  m251_outline_line *lines = Safe_alloc (capacity * sizeof (*lines));
  for (lineNode *p = head; p; p = p->next)
    {
      char key[M251_OUTLINE_TEXT], body[M251_OUTLINE_TEXT];
      if (p->isComment || p->isDebug) continue;
      if (p->isLabel || !m251ShortWideLine (p, interrupt, key, body)) { ++run; continue; }
      lines[count].node = p; lines[count].run = run; lines[count].used = FALSE;
      strcpy (lines[count].key, key); strcpy (lines[count].body, body); ++count;
    }
  for (unsigned round = 0; round < 32; ++round)
    {
      unsigned bestStart = 0, bestLength = 0, bestCopies = 0, bestGain = 63;
      for (unsigned i = 0; i < count; ++i)
        for (unsigned length = 4; length <= 8 && i + length <= count; ++length)
          {
            bool valid = TRUE;
            for (unsigned k = 0; k < length; ++k)
              if (lines[i+k].used || lines[i+k].run != lines[i].run) valid = FALSE;
            if (!valid) break;
            unsigned copies = 0;
            for (unsigned j = i; j + length <= count;)
              {
                unsigned k = 0;
                while (k < length && !lines[j+k].used && lines[j+k].run == lines[j].run &&
                       !strcmp (lines[i+k].key, lines[j+k].key)) ++k;
                if (k == length) { ++copies; j += length; } else ++j;
              }
            unsigned gain = copies >= 3 ? (copies-1)*4*length - copies*4 - 1 : 0;
            if (gain > bestGain) { bestStart=i; bestLength=length; bestCopies=copies; bestGain=gain; }
          }
      if (!bestLength) break;
      symbol *label = newiTempLabel (NULL); char target[64], call[96];
      SNPRINTF (target, sizeof target, "%05d$", labelKey2num (label->key));
      SNPRINTF (call, sizeof call, "ecall\t%s", target);
      dbuf_printf (helpers, "; MX251 short-wide outline: %u word loads, %u copies, lower saving %u; ISR=%u; extra stack 3\n%s:\n",
                   bestLength, bestCopies, bestGain, interrupt, target);
      for (unsigned k = 0; k < bestLength; ++k) dbuf_printf (helpers, "\t%s\n", lines[bestStart+k].body);
      dbuf_append_str (helpers, "\teret\n");
      for (unsigned j = bestStart; j + bestLength <= count;)
        {
          unsigned k = 0;
          while (k < bestLength && !lines[j+k].used && lines[j+k].run == lines[j].run &&
                 !strcmp (lines[bestStart+k].key, lines[j+k].key)) ++k;
          if (k != bestLength) { ++j; continue; }
          for (k = 0; k < bestLength; ++k)
            {
              lineNode *p = lines[j+k].node;
              p->line = Safe_strdup (k ? "; MX251 short-wide outlined" : call);
              p->isComment = k != 0; p->aln = NULL; lines[j+k].used = TRUE;
            }
          j += bestLength;
        }
    }
  Safe_free (lines);
}
