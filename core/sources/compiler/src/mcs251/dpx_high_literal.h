/* SPDX-License-Identifier: GPL-2.0-or-later
 * 仅证明编译器内部寻址装载的 DPXL 字面量相同。用户 SFR 写入不会被删除。
 * 小型白名单内 MOV @DPX 不改地址寄存器；所有未知效果/别名/控制流均回退。
 */
#ifndef MX251_DPX_HIGH_LITERAL_H
#define MX251_DPX_HIGH_LITERAL_H
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static inline int mx251DpxLiteral(const char *text, unsigned limit, unsigned *value)
{
  char *end;
  unsigned long parsed;
  if (!text || text[0] != '#' || !isdigit((unsigned char)text[1])) return 0;
  errno = 0;
  parsed = strtoul(text + 1, &end, 0);
  if (errno || *end || parsed > limit) return 0;
  *value = (unsigned)parsed;
  return 1;
}

/* ar0..7 是普通后端定义的当前寄存器组 RAM 别名，均不与 S:84H 重叠。
 * 使用方只接受编译器发射行，内联汇编和标签先终止回溯。 */
static inline int mx251DpxLowByte(const char *text)
{
  if (!strcmp(text, "a") || !strcmp(text, "b")) return 1;
  if (text[0] == 'r' && text[1] >= '0' && text[1] <= '9') {
    if (!text[2]) return 1;
    return text[1] == '1' && text[2] >= '0' && text[2] <= '5' && !text[3];
  }
  return text[0] == 'a' && text[1] == 'r' && text[2] >= '0' && text[2] <= '7' && !text[3];
}

/* 1=建立 DPXL 常量，0=确定不改变 DPXL，-1=不能证明。 */
static inline int mx251DpxHighEffect(const char *line, unsigned *value)
{
  char op[16], args[128], *right;
  size_t at = 0, n = 0;
  unsigned ignored;
  while (isspace((unsigned char)line[at])) ++at;
  if (!line[at] || line[at] == ';') return 0;
  while (line[at] && !isspace((unsigned char)line[at]) && line[at] != ';') {
    if (n + 1 >= sizeof(op)) return -1;
    op[n++] = (char)tolower((unsigned char)line[at++]);
  }
  op[n] = 0; n = 0;
  while (line[at] && line[at] != ';') {
    if (!isspace((unsigned char)line[at])) {
      if (n + 1 >= sizeof(args)) return -1;
      args[n++] = (char)tolower((unsigned char)line[at]);
    }
    ++at;
  }
  args[n] = 0;
  right = strchr(args, ',');
  if (right) *right++ = 0;
  if (right && strchr(right, ',')) return -1;
  if (!strcmp(op, "mov") && right) {
    if (!strcmp(args, "dpxl"))
      return mx251DpxLiteral(right, 255, value) ? 1 : -1;
    if (!strcmp(args, "dptr") && mx251DpxLiteral(right, 65535, &ignored)) return 0;
    if (!strcmp(args, "a") && (!strcmp(right, "@dpx") || mx251DpxLiteral(right, 255, &ignored))) return 0;
    if (mx251DpxLowByte(args) && (mx251DpxLowByte(right) || mx251DpxLiteral(right, 255, &ignored))) return 0;
    if (!strcmp(args, "@dpx") && !strcmp(right, "a")) return 0;
  }
  if (!strcmp(op, "clr") && !strcmp(args, "a") && !right) return 0;
  if ((!strcmp(op, "anl") || !strcmp(op, "orl") || !strcmp(op, "xrl")) &&
      mx251DpxLowByte(args) && right && mx251DpxLiteral(right, 255, &ignored)) return 0;
  return -1;
}
#endif
