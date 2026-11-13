"""Generic union transport and basic math, checked on the STC-Link1D board."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import machine_pipeline_fixture as board

MASK = 0xffffffff
SPECIAL = [0, 0x80000000, 1, 0x80000001, 0x7fffff, 0x807fffff,
           0x800000, 0x80800000, 0x3effffff, 0xbeffffff, 0x3f000000, 0xbf000000,
           0x3f7fffff, 0xbf7fffff, 0x3f800000, 0xbf800000, 0x3f800001, 0xbf800001,
           0x3fc00000, 0xbfc00000, 0x4affffff, 0xcaffffff, 0x4b000000, 0xcb000000,
           0x4f000000, 0xcf000000, 0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000,
           0x7fc12345, 0xff812345]
VECTORS = SPECIAL + [((i*0x10203041) ^ 0x13579bdf) & MASK for i in range(32)]


def bits(x):
    return int.from_bytes(struct.pack('>f', x), 'big')


def number(x):
    return struct.unpack('>f', x.to_bytes(4, 'big'))[0]


def mix(h, value):
    return ((h << 5) ^ h ^ (value & MASK)) & MASK


def expected(kind, seed):
    raw = VECTORS[seed]
    if not kind:
        value = raw
        for i in range(5):
            if (seed+i) & 1:
                value ^= 0x00234567
            value = (value+0x101+i) & MASK
        # True volatile and escaped unions remain observable memory accesses.
        escaped = raw ^ 0x13579bdf
        partial = (raw & 0x00ffffff) | ((((raw >> 24)+seed) & 255) << 24)
        return mix(mix(mix(mix(raw, value), raw ^ 0x80000000), escaped), partial)
    sign, magnitude = raw & 0x80000000, raw & 0x7fffffff
    x = number(raw)
    if magnitude >= 0x7f800000:
        floor = ceil = integer = raw
        fraction = sign if magnitude == 0x7f800000 else raw
        mantissa, exponent, scaled = raw, 0, raw
    else:
        floor = bits(float(math.floor(x))) if x else raw
        ceil = bits(float(math.ceil(x))) if x else raw
        if not (ceil & 0x7fffffff):
            ceil = sign
        f, n = math.modf(x)
        fraction, integer = bits(f), bits(n)
        m, exponent = math.frexp(x)
        mantissa = bits(m)
        try:
            scaled = bits(math.ldexp(x, (seed & 15)-7))
        except OverflowError:
            # Existing HUGE_VALF is max finite; preserve this library policy.
            scaled = sign | 0x7f7fffff
    result = 0x2468ace0
    for value in [floor, ceil, fraction, integer, magnitude, mantissa, exponent, scaled]:
        result = mix(result, value)
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO/'core/sources/compiler/src/mcs251/tests'
    driver = (tests/'machine-board.c').read_text()
    start = driver.index('        x0 =')
    end = driver.index('        TR0 = 0; ticks', start)
    driver = driver[:start] + '''        TH0 = 0; TL0 = 0; TF0 = 0; TR0 = 1;
        result = kind ? mt_math(input) : mt_transport(input);
''' + driver[end:]
    driver = driver.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (output/'machine-board.c').write_text(driver)
    (output/'machine-board.h').write_text('''typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
typedef union { float f; unsigned long u; long s; } mt_bits;
float mt_identity(float);
void mt_escape(u32 *);
u32 mt_partial(u32,u8);
u32 mt_volatile(u32);
u32 mt_transport(u8);
u32 mt_math(u8);
''')
    (output/'mt-sink.c').write_text('''#include "machine-board.h"
float mt_identity(float x) { return x; }
void mt_escape(u32 *x) { *x ^= 0x13579bdfUL; }
u32 mt_volatile(u32 raw) {
    volatile mt_bits observed;
    observed.u=raw; observed.u^=0x80000000UL; return observed.u;
}
u32 mt_partial(u32 raw,u8 seed) {
    union { u32 u; unsigned char bytes[4]; } partial;
    partial.u=raw; partial.bytes[0]+=seed; return partial.u;
}
''')
    (output/'mt-case.c').write_text('''#include "machine-board.h"
#include <math.h>
static const u32 inputs[64] = {
''' + ','.join('0x%08xUL' % x for x in VECTORS) + '''};
static u32 mix(u32 h, u32 x) { return (h << 5) ^ h ^ x; }
u32 mt_transport(u8 seed)
{
    mt_bits a, b, live, escaped;
    u32 raw = inputs[seed], result;
    u8 i;
    a.u = raw;
    b.f = mt_identity(a.f);
    result = b.u;
    live.u = raw;
    for (i=0;i<5;++i) {
        if ((seed+i)&1) live.u ^= 0x00234567UL;
        else live.f = mt_identity(live.f);
        live.u += 0x101UL+i;
    }
    result = mix(result, live.u);
    result = mix(result, mt_volatile(raw));
    escaped.u = raw;
    mt_escape(&escaped.u);
    result = mix(result, escaped.u);
    return mix(result, mt_partial(raw,seed));
}
u32 mt_math(u8 seed)
{
    mt_bits x, v, integer;
    u32 result = 0x2468ace0UL;
    int exponent;
    x.u = inputs[seed];
    v.f = floorf(x.f); result = mix(result,v.u);
    v.f = ceilf(x.f); result = mix(result,v.u);
    v.f = modff(x.f,&integer.f); result = mix(result,v.u);
    result = mix(result,integer.u);
    v.f = fabsf(x.f); result = mix(result,v.u);
    v.f = frexpf(x.f,&exponent); result = mix(result,v.u);
    result = mix(result,(long)exponent);
    v.f = ldexpf(x.f,(int)(seed&15)-7); result = mix(result,v.u);
    return result;
}
''')
    board.write(output/'expected.json', {f'{kind}:{seed}': expected(kind,seed)
                for kind in range(2) for seed in range(64)})


def build(project, core, effects=False):
    """Direct compiler fixture; does not extend the released target profiles."""
    core = core.resolve(); project = project.resolve()
    toolchain = core/'toolchain'; profile = core/'profiles/AI8051U-34K64-32'
    compiler = toolchain/'bin/sdcc.exe'
    env = os.environ.copy()
    env['SDCC_HOME'] = str(toolchain)
    env['PATH'] = str(toolchain/'bin')+os.pathsep+env['PATH']
    commands = []
    if effects:
        sidecar=profile/'runtime/libnativefloat.effects.json'
        lock=json.loads((profile/'profile.lock.json').read_text())
        assert any(e['path']=='runtime/libnativefloat.effects.json' and e['sha256']==board.sha(sidecar)
                   for e in lock['profile'])
        archive=json.loads(sidecar.read_text())['archives'][0]
        assert archive['sha256']==board.sha(profile/archive['path'])
        facts=['MX251-CALL-CLOBBERS-4']
        facts += ['global {symbol} {abi} {localWrites} 3 1'.format(**f) for f in archive['functions']]
        (project/'runtime-effects.txt').write_text('\n'.join(facts)+'\n')
    for model in ('small', 'large'):
        for opt in ('speed', 'size'):
            out = project/(model+'-'+opt); out.mkdir(exist_ok=False)
            for name in ('machine-board.h', 'machine-board.c', 'mt-case.c', 'mt-sink.c'):
                shutil.copy2(project/name, out/name)

            def run(label, args):
                args = list(map(str,args))
                q = subprocess.run(args,cwd=out,env=env,capture_output=True)
                (out/(label+'.log')).write_bytes(q.stdout+q.stderr)
                commands.append(dict(arguments=args,cwd=str(out),exitCode=q.returncode))
                board.write(project/'commands.json', commands)
                assert q.returncode==0,(label,q.stderr)

            flags = ['-mmcs251','--model-'+model,'--opt-code-'+opt,'--stack-auto',
                     '--mcs251-wide-ordinary','--mcs251-machine-ir','--mcs251-function-metadata',
                     '--mcs251-native-runtime','--mcs251-native-int-runtime','--mcs251-sfr-layout','sph85-v1']
            if effects:
                flags += ['--mcs251-leaf-effects',str(project/'runtime-effects.txt')]
            for name in ('machine-board','mt-case','mt-sink'):
                run(name,[compiler,*flags,'-c',name+'.c'])
            assembly=(out/'mt-case.asm').read_text()
            for name in ('mt_transport','mt_math'):
                assert re.search(r'wide private-values choice function='+name+r' .*selected=1 ',assembly)
            # Large-model compilation exercises transport; the archive is small-model.
            if model=='small' and opt=='speed':
                shutil.copy2(profile/'startup.asm',out/'startup.asm')
                run('startup',[toolchain/'bin/sdas251.exe','-plosgff','startup.rel','startup.asm'])
                libs = [profile/'runtime'/n for n in ('libnativefloat.lib','libnativeint.lib')]
                libs += [toolchain/'lib/mcs251-small-stack-auto'/(n+'.lib')
                         for n in ('mcs251','libsdcc','libint','liblong','liblonglong')]
                run('link',[compiler,*flags,'--nostdlib','--code-loc','0xff0000','--code-size','0x10000',
                     '--xram-loc','0x10000','--xram-size','0x7000','--iram-size','0x100',
                     '--mcs251-stack','0x400,0x400,0x1000','machine-board.rel','mt-case.rel','mt-sink.rel',
                     'startup.rel',*libs,'-o','firmware.hex'])
    board.write(project/'build.json',dict(status='PASS',compilerSha256=board.sha(compiler),
        configurations=4,callEffects=effects,hardwareExecuted=False))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--output', type=Path, required=True)
    c = sub.add_parser('build'); c.add_argument('--project', type=Path, required=True); c.add_argument('--core', type=Path, required=True)
    c.add_argument('--effects',action='store_true')
    c = sub.add_parser('capture'); c.add_argument('--hex', type=Path, required=True); c.add_argument('--output', type=Path, required=True)
    c = sub.add_parser('check'); c.add_argument('capture', type=Path)
    a = p.parse_args()
    if a.command == 'create': create(a.output)
    elif a.command == 'build': build(a.project, a.core,a.effects)
    elif a.command == 'capture': board.capture(a.hex, a.output, lambda data: board.check(data, expected))
    else: print(json.dumps(board.check(a.capture.read_bytes(), expected), indent=2))
