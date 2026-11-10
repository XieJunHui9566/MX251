/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdio.h>
#include "dbuf_string.h"
#include "wide_loop_cost.h"
#define SNPRINTF snprintf
typedef struct { int cursorCount,cursorLabels[32]; } wide_function;
static struct { const char *rname; } testFunction={"_test"},*currFunc=&testFunction;
static wide_cost_operand wide_resolve_cost_operand(const char *s,const void *context)
{ (void)s;(void)context;return (wide_cost_operand){0}; }
#include "wide_cursor_cost.c.inc"

#define PREFIX "__wm__test_8:\necall _unknown\njnb 0x20.0,__wm__test_1000000\nejmp __wm__test_8\n__wm__test_1000000:\n"
#define OLD "__wm__test_1:\nmov dr0,dr4\nmovz wr22,r9\nsub wr20,wr20\nadd dr0,dr20\nanl wr0,#0x00ff\nmov r8,@dr0\ninc r9\ncmp r9,#0x10\n"
#define NEW "__wm__test_1:\nmov r8,@dr4\ninc dr4,#1\nanl wr4,#0x00ff\ninc r9\ncmp r9,#0x10\n"
#define END "jc __wm__test_1\n"
#define TAIL "ejmp __wm__test_8\n"
static int check(const char *name,const char *a,const char *b,int expected)
{
  struct dbuf_s x,y;dbuf_init(&x,16);dbuf_init(&y,16);
  /* Same raw-byte append used by the production candidate selector. */
  dbuf_append(&x,a,strlen(a));dbuf_append(&y,b,strlen(b));
  wide_function old={0},trial={1,{1}};
  int actual=wide_cursor_cost_compare(&old,&trial,&x,&y);
  dbuf_destroy(&x);dbuf_destroy(&y);
  if(actual!=expected)fprintf(stderr,"%s: got %d, expected %d\n",name,actual,expected);
  return actual==expected;
}
int main(void)
{
  int passed=0;
  passed+=check("nested-event-loop",PREFIX OLD END TAIL,PREFIX NEW END TAIL,1);
  passed+=check("inner-regression",PREFIX OLD END TAIL,PREFIX NEW "push a\npop a\n" END TAIL,0);
  passed+=check("outer-change",PREFIX OLD END TAIL,PREFIX NEW END "inc r0\n" TAIL,-1);
  passed+=check("long-backedge",PREFIX OLD "jnc __wm__test_1000002\nejmp __wm__test_1\n__wm__test_1000002:\n" TAIL,
                PREFIX NEW END TAIL,1);
  passed+=check("missing-region",PREFIX OLD END TAIL,"_test:\neret\n",-1);
  passed+=check("other-branch-target",PREFIX OLD END TAIL,PREFIX NEW END "ejmp __wm__test_1000000\n",-1);
  printf("{\"status\":\"%s\",\"cases\":%d}\n",passed==6?"PASS":"FAIL",passed);
  return passed!=6;
}
