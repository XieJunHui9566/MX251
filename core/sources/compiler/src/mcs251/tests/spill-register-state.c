/* Exercise physical byte overlap and private-slot identity transitions. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
unsigned mcs251SphAddress=0xbe;
#include "../instruction_effects.h"
typedef struct {
  bool reuseSpillRegisters;int spillRegisterSlots[32];unsigned spillRegisterReuses;
  int frame,stackDepth;
} wide_function;
#include "../wide_spill_registers.c.inc"
static void observe(wide_function *f,const char *s) {wspill_observe(f,s,strlen(s));}
int main(void)
{
  wide_function f={.reuseSpillRegisters=true,.frame=32};wspill_clear(&f);
  wspill_remember(&f,0,16,2);wspill_remember(&f,2,18,2);
  assert(wspill_find(&f,0,4)==16);
  observe(&f,"\tmov\tdr24,dr16");
  observe(&f,"\tadd\twr16,#1");
  assert(wspill_find(&f,0,4)==24 && wspill_find(&f,2,2)==18);
  observe(&f,"\tmov\t@spx-0x001d,wr20"); /* frame slot 2 */
  assert(wspill_find(&f,0,2)==24 && wspill_find(&f,2,2)<0);
  wspill_remember(&f,4,6,2);observe(&f,"\tmov\tr7,#1");
  assert(wspill_find(&f,4,2)<0);
  wspill_remember(&f,8,12,4);observe(&f,"\tmov\t@spx-0x0017,a"); /* slot 8 */
  assert(wspill_find(&f,8,4)<0 && wspill_find(&f,10,2)==14);
  const char *barriers[]={"join:","\tjc\tjoin","\tajmp\tjoin","\tdjnz\tr0,join",
    "\tecall\tcallee","\tpush\tdr0","\tpop\tdr0","\tadd\tspx,#2","\tmov\t@dr20,wr16","\tunknown\tr0"};
  for(unsigned i=0;i<sizeof barriers/sizeof *barriers;++i) {
    wspill_remember(&f,0,24,2);observe(&f,barriers[i]);assert(wspill_find(&f,0,2)<0);
  }
  f.stackDepth=6;wspill_remember(&f,0,24,2);
  observe(&f,"\tmov\t@spx-0x0025,wr16");assert(wspill_find(&f,0,2)<0);
  wspill_remember(&f,-1,0,2);wspill_remember(&f,31,0,2);
  assert(wspill_find(&f,31,2)<0);
  puts("PASS physical overlaps, copy propagation, private-slot stores and control/call boundaries");
}
