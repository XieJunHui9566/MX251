"""Board checks for complete ABI4 backend choices and mixed ABI2/ABI4 calls."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board

VALUES = [0, 1, 2, 255, 256, 32767, 32768, 65534, 65535]
while len(VALUES) < 64:
    VALUES.append((len(VALUES)*0x1537+0x8765) & 65535)
DIVISORS = [1, 2, 3, 7, 9600, 115200, 65535, 65536]
while len(DIVISORS) < 64:
    DIVISORS.append(len(DIVISORS)*312437+1)


def expected(kind, seed):
    value = VALUES[seed]
    count = (seed+kind*3) & 7
    keep = (seed*0x1357+0x8123) & 65535
    neg_loop = sum((-(value+i)) & 65535 for i in range(count)) & 65535
    byte = value & 255
    for _ in range(count):
        byte = ((byte << 1) | (byte >> 7)) & 255
    mixed = value
    for _ in range(count):
        mixed = (-(mixed*5+0x9137)+keep) & 65535
    results = [(-value) & 65535, (-value) & 255, neg_loop, byte, mixed,
               (-((40000000//DIVISORS[seed]+2)//4)) & 65535]
    result = 0x13579bdf
    for value in results:
        result = ((result << 3)^result^value) & 0xffffffff
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO/'core/sources/compiler/src/mcs251/tests'
    for name in ('abi4-backend-choice.c', 'abi4-backend-choice.h'):
        shutil.copy2(tests/name, output/name)
    (output/'ordinary.c').write_text('''#include "abi4-backend-choice.h"
u16 ordinary_step(u16 value) {return value*5u+0x9137u;}
''')
    (output/'cases.c').write_text('''#include "abi4-backend-choice.h"
static const u16 __code values[64]={VALUES};
static const u32 __code divisors[64]={DIVISORS};
#define MIX(v) result=(result<<3)^result^(v)
u32 choice_case(u8 kind,u8 seed)
{
    u16 value=values[seed],keep=(u16)seed*0x1357u+0x8123u;
    u8 count=(seed+kind*3)&7;
    u32 result=0x13579bdfUL;
    MIX(negate_word(value)); MIX(negate_byte((u8)value));
    MIX(negate_loop(value,count)); MIX(byte_loop((u8)value,count));
    MIX(mixed_calls(value,count,keep)); MIX(divide_negate(divisors[seed]));
    return result;
}
'''.replace('VALUES', ','.join(str(v) for v in VALUES))
       .replace('DIVISORS', ','.join(str(v)+'UL' for v in DIVISORS)))
    driver = (tests/'machine-board.c').read_text()
    begin = driver.index('        x0 =')
    end = driver.index('        TR0 = 0; ticks', begin)
    driver = driver[:begin]+'''        TH0 = 0; TL0 = 0; TF0 = 0; TR0 = 1;
        result=choice_case(kind,input);
'''+driver[end:]
    driver = driver.replace('#include "machine-board.h"', '#include "abi4-backend-choice.h"')
    driver = driver.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (output/'machine-board.c').write_text(driver)
    names = ['abi4-backend-choice.c', 'ordinary.c', 'cases.c', 'machine-board.c']
    units = [dict(name='u'+str(i), file=f, stackAuto=True, defines=[],
                  backend='legacy' if f in ['ordinary.c', 'machine-board.c'] else 'ordinary-wide')
             for i, f in enumerate(names)]
    board.write(output/'mx251.json', dict(schema=2, target='AI8051U-34K64-32', model='small',
        optimization='speed', backend='ordinary-wide', explicitAbi=True, standard='sdcc11',
        runtime='standard', defines=[], files=['abi4-backend-choice.h']+names, units=units,
        archives=[], link=[u['name'] for u in units]+['@startup']))
    board.write(output/'source.json', {f: board.sha(output/f) for f in ['abi4-backend-choice.h']+names})


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--output', type=Path, required=True)
    c = sub.add_parser('capture'); c.add_argument('--hex', type=Path, required=True)
    c.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.command == 'create':
        create(a.output)
    else:
        board.capture(a.hex, a.output, lambda data: board.check(data, expected))
