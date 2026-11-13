"""ABI4 CODE pointer transport across wide and legacy translation units."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import machine_pipeline_fixture as board

BYTES = [(i*73+(i >> 3)*11+29) & 255 for i in range(520)]


def expected(kind, seed):
    result = 0x13579bdf
    for k in range(4):
        i = (seed*37+k*19) & 255
        raw = 0x123400+seed*257+k
        values = [BYTES[i+3], BYTES[i+1]+0x8123,
                  BYTES[i]+BYTES[i+7]+0x1234+5+9,
                  BYTES[i+4], BYTES[i+2], BYTES[i+3],
                  BYTES[i+1]+BYTES[i+7], BYTES[i+2],
                  raw, raw+(0x1234 ^ 0xabcd ^ 0x5678)]
        for value in values:
            result = ((result << 3)^result^value) & 0xffffffff
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    header = '''typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
typedef const u8 __code *cp;
#define ABI __reentrant __sdcccall(4)
extern const u8 __code bytes[520];
u8 first(cp p,u8 i) ABI;
u16 second(u16 bias,cp p) ABI;
u16 stacked(u8 a,u16 b,cp p,cp q,u8 tail) ABI;
cp advance(cp p,u16 i) ABI;
cp address(u32 n) ABI;
cp echo(u16 a,u16 b,cp p,u16 tail) ABI;
u16 forward(cp p,cp q) ABI;
u8 homed(cp p) ABI;
u8 legacy_native(cp p,u8 i) ABI;
u8 legacy_read(cp p);
void bump(cp *p);
u32 wide_case(u8 seed);
u32 legacy_case(u8 seed);
'''
    (output/'abi.h').write_text(header)
    (output/'native.c').write_text('''#include "abi.h"
u8 first(cp p,u8 i) ABI {return p[i];}
u16 second(u16 bias,cp p) ABI {return bias+p[1];}
u16 stacked(u8 a,u16 b,cp p,cp q,u8 tail) ABI {return *p+*q+a+b+tail;}
cp advance(cp p,u16 i) ABI {return p+i;}
cp address(u32 n) ABI {return (cp)n;}
cp echo(u16 a,u16 b,cp p,u16 tail) ABI {return p+(a^b^tail);}
u16 forward(cp p,cp q) ABI {return first(p,1)+legacy_read(q);}
u8 homed(cp p) ABI {bump(&p);return *p;}
''')
    (output/'legacy.c').write_text('''#include "abi.h"
u8 legacy_native(cp p,u8 i) ABI {return p[i];}
u8 legacy_read(cp p) {return *p;}
void bump(cp *p) {++*p; ++*p;}
''')
    body = '''#include "abi.h"
static u8 (*volatile reader)(cp,u8) ABI=first;
#define MIX(v) result=(result<<3)^result^(v)
u32 CASE(u8 seed) {
    u8 k,i;u32 raw,result=0x13579bdfUL;
    for(k=0;k<4;++k) {
        i=seed*37+k*19;raw=0x123400UL+(u32)seed*257+k;
        MIX(first(bytes+i,3)); MIX(second(0x8123,bytes+i));
        MIX(stacked(5,0x1234,bytes+i,bytes+i+7,9));
        MIX(*advance(bytes+i,4)); MIX(legacy_native(bytes+i,2));
        MIX(reader(bytes+i,3)); MIX(forward(bytes+i,bytes+i+7));
        MIX(homed(bytes+i)); MIX((u32)address(raw));
        MIX((u32)echo(0x1234,0xabcd,address(raw),0x5678));
    }
    return result;
}
'''
    for kind in ['wide', 'legacy']:
        (output/(kind+'-case.c')).write_text(body.replace('CASE', kind+'_case'))
    (output/'tables.c').write_text('#include "abi.h"\nconst u8 __code bytes[520]={' +
                                 ','.join(str(v) for v in BYTES)+'};\n')
    driver = (board.REPO/'core/sources/compiler/src/mcs251/tests/machine-board.c').read_text()
    begin = driver.index('        x0 ='); end = driver.index('        TR0 = 0; ticks', begin)
    driver = driver[:begin]+'''        TH0 = 0; TL0 = 0; TF0 = 0; TR0 = 1;
        result=kind?legacy_case(input):wide_case(input);
'''+driver[end:]
    driver = driver.replace('#include "machine-board.h"', '#include "abi.h"')
    driver = driver.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (output/'machine-board.c').write_text(driver)
    names = ['native.c','legacy.c','wide-case.c','legacy-case.c','tables.c','machine-board.c']
    units = [dict(name='u'+str(i), file=f, stackAuto=True, defines=[],
                  backend='legacy' if f in ['legacy.c','legacy-case.c','machine-board.c'] else 'ordinary-wide')
             for i,f in enumerate(names)]
    board.write(output/'mx251.json', dict(schema=2,target='AI8051U-34K64-32',model='small',
        optimization='speed',backend='ordinary-wide',explicitAbi=True,standard='sdcc11',
        runtime='standard',defines=[],files=['abi.h']+names,units=units,archives=[],
        link=[u['name'] for u in units]+['@startup']))
    board.write(output/'source.json', {f:board.sha(output/f) for f in ['abi.h']+names})


def check_compiler(compiler, source, output):
    output.mkdir(parents=True, exist_ok=False)
    rows=[]
    for model in ['small','large']:
        for opt in ['speed','size']:
            for wide in [False,True]:
                d=output/(model+'-'+opt+'-'+str(wide)); d.mkdir()
                shutil.copy2(source/'abi.h',d/'abi.h')
                for unit in ['native','legacy','wide-case']:
                    shutil.copy2(source/(unit+'.c'),d/(unit+'.c'))
                    args=[str(compiler.resolve()),'-mmcs251','--model-'+model,
                          '--opt-code-'+opt,'--stack-auto','--mcs251-native-abi']
                    if wide: args+=['--mcs251-wide-ordinary']
                    run=subprocess.run(args+['-c',unit+'.c'],cwd=d,capture_output=True)
                    (d/(unit+'.log')).write_bytes(run.stdout+run.stderr)
                    assert run.returncode==0,(d,unit,run.stderr)
                    asm=(d/(unit+'.asm')).read_text()
                    accepted=re.findall(r'wide-mir accepted: (\w+)',asm)
                    if wide and unit=='native':
                        assert set(accepted)=={'first','second','stacked','advance','address','echo','forward','homed'}
                        assert asm.count('movc\ta,@a+dptr')>=5
                        assert 'physical=3 logical=4 entry-offset=-5' in asm
                        assert 'physical=3 logical=4 entry-offset=-8' in asm
                    rows.append(dict(model=model,opt=opt,wide=wide,unit=unit,accepted=accepted))
    # CODE pointers remain an ABI4 extension; ABI3 retains scalar transport.
    for name,body in {
        'parameter':'u8 f(cp p) __reentrant __sdcccall(3) {return *p;}',
        'return':'cp f(u32 n) __reentrant __sdcccall(3) {return (cp)n;}',
    }.items():
        d=output/('abi3-'+name);d.mkdir();shutil.copy2(source/'abi.h',d/'abi.h')
        (d/'bad.c').write_text('#include "abi.h"\n'+body)
        run=subprocess.run([str(compiler.resolve()),'-mmcs251','--stack-auto',
            '--mcs251-native-abi','--mcs251-wide-ordinary','-c','bad.c'],cwd=d,capture_output=True)
        (d/'compile.log').write_bytes(run.stdout+run.stderr)
        assert run.returncode==1 and b'ABI' in run.stderr
    board.write(output/'result.json',dict(status='PASS',configurations=rows,abi3Rejections=2))
    print(json.dumps(dict(status='PASS',configurations=len(rows),abi3Rejections=2)))


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__); sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create'); c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture'); c.add_argument('--hex',type=Path,required=True)
    c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('check'); c.add_argument('--compiler',type=Path,required=True)
    c.add_argument('--source',type=Path,required=True); c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create': create(a.output)
    elif a.command=='check': check_compiler(a.compiler,a.source,a.output)
    else: board.capture(a.hex,a.output,lambda data:board.check(data,expected))
