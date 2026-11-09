"""Compile pointer transports and check relocatable PUSHW costs/effects."""
import argparse,json,os,re,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser()
for name in ('sdcc','support','gcc','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
tests=Path(__file__).resolve().parent;rows=[]
env=os.environ.copy();env['SDCC_HOME']=str(a.support.resolve());env['PATH']=str(a.support.resolve()/'bin')+';'+env['PATH']
extra='''#include "symbol-address.h"
u16 native_cast(const u8 *p) __sdcccall(4) {
    return address_code((const u8 __code *)p,4,17);
}
const u8 *flat_roundtrip(const u8 __code *p) { return (const u8 *)p; }
const u8 __code *code_roundtrip(const u8 *p) { return (const u8 __code *)p; }
u16 volatile_sample(const u8 * volatile p) { return address_first(p,4,19); }
'''
for model in ('small','large'):
    for opt in ('speed','size'):
        d=a.output/(model+'-'+opt);d.mkdir()
        for name in ('machine-board.h','symbol-address.h','symbol-address-callers.c','symbol-address-callees.c'):shutil.copy2(tests/name,d/name)
        (d/'extra.c').write_text(extra)
        for source in ('symbol-address-callers','symbol-address-callees','extra'):
            cmd=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--opt-code-'+opt,'--stack-auto','--mcs251-wide-ordinary','--mcs251-machine-ir','--mcs251-function-metadata','-c',source+'.c']
            q=subprocess.run(cmd,cwd=d,capture_output=True,env=env)
            (d/(source+'.log')).write_bytes(q.stdout+q.stderr)
            assert q.returncode==0,q.stderr
            rows.append(dict(command=cmd,output=str(d),exitCode=q.returncode))
        text=(d/'symbol-address-callers.asm').read_text()
        assert '; wide symbolic call addresses=' in text
        assert '; wide symbolic register argument' in text and '; wide symbolic stack argument' in text
        # No constant substitution for the loop-mutated pointer or table loads:
        # these calls still transport their actual allocated pointer value.
        assert re.search(r'wide ordinary argument direct (?:reg|words|stack).*physical=3',text)
        text=(d/'extra.asm').read_text()
        assert 'wide-mir accepted: native_cast' in text
        assert 'mx251-backend-choice function=_flat_roundtrip' in text
        assert 'mx251-backend-choice function=_code_roundtrip' in text
# Same emitted instruction, two consumers: register effects and encoded bytes.
driver=r'''#include <assert.h>
#include "wide_cost.h"
#include "instruction_effects.h"
int main(void) {
 const char *good[]={"pushw #(_table+0)","pushw #(_Case.2-3) ; note","pushw #(external)","pushw #0x1234"};
 const char *bad[]={"pushw #(_a +0)","pushw #(_a+)","pushw #(_a+0),r1","pushw #(_a+0)garbage","pushw #(_a*2)"};
 for(unsigned i=0;i<sizeof good/sizeof *good;++i) {
  const char *s=good[i];mx251_effects e=mx251InstructionEffects(s,strlen(s),s,strlen(s));
  assert(!e.unknown && !e.reads && !e.writes && !e.flagsWrites);
  assert(e.specialReads==MX251_SPECIAL_SPX && e.specialWrites==MX251_SPECIAL_SPX && e.memory==MX251_MEMORY_WRITE);
  assert(wide_instruction_bytes(s,strlen(s))==4);
 }
 for(unsigned i=0;i<sizeof bad/sizeof *bad;++i) {
  const char *s=bad[i];mx251_effects e=mx251InstructionEffects(s,strlen(s),s,strlen(s));
  assert(e.unknown && !wide_instruction_bytes(s,strlen(s)));
 }
 return 0;
}
'''
(a.output/'effects.c').write_text(driver)
cmd=[str(a.gcc.resolve()),'-std=c11','-O2','-I'+str(tests.parent),str(a.output/'effects.c'),'-o',str(a.output/'effects.exe')]
q=subprocess.run(cmd,capture_output=True);(a.output/'effects.log').write_bytes(q.stdout+q.stderr);assert q.returncode==0,q.stderr
subprocess.run([str((a.output/'effects.exe').resolve())],check=True)
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=4,commands=rows,effectsCommand=cmd),indent=2)+'\n')
print('PASS: 4 compiler configurations, ABI2/ABI4 casts, mutable pointers and symbolic PUSHW effects/costs')
