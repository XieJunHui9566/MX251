"""Independent binary32, CODE indexing, pointer objects and bit-update workload."""
import argparse
from pathlib import Path
import machine_pipeline_fixture as board
import native_isr_fixture as irq
import isr_saves_fixture


def expected(kind, seed):
    if kind:
        return 0x600d0000 | seed
    x = seed - 31
    value = int((x*x + seed*257 + 3)/4) + (19 if x < 0 else 7)
    return (value ^ ((seed*509+17) << 8) ^ (seed*313+29)) & 0xffffffff


def create(output, model, interrupt=False):
    if interrupt:
        irq.create(output)
        p = output/'call-boundary-isr.c'
        source = p.read_text()
        source = source.replace('void context_irq(void)',
            'volatile __bit batch_bit;\nvoid context_irq(void)')
        source = source.replace('    ++context_count;',
            '    batch_bit=!batch_bit;\n'
            '    context_fixed=context_value;\n'
            '    ++context_count;')
        source = source.replace('context_fixed = 0x93;', 'context_fixed = 0x93; batch_bit=0;')
        source = source.replace('    if(context_total !=',
            '    if(!batch_bit || context_fixed!=context_value)++context_errors;\n'
            '    if(context_total !=')
        p.write_text(source, newline='\n')
        import json
        files=json.loads((output/'mx251.json').read_text())['files']
        board.write(output/'source.json',dict(files={name:board.sha(output/name) for name in files}))
        return
    output.mkdir(parents=True, exist_ok=False)
    driver = (board.REPO/'core/sources/compiler/src/mcs251/tests/machine-board.c').read_text()
    begin, end = driver.index('        x0 ='), driver.index('        TR0 = 0; ticks')
    driver = driver[:begin]+'''        TH0 = 0; TL0 = 0; TF0 = 0; TR0 = 1;
        result = batch_case(kind,input);
'''+driver[end:]
    driver = driver.replace('u16 ticks, x0, y0, x1, y1, x2, y2;', 'u16 ticks;')
    (output/'machine-board.c').write_text(driver, newline='\n')
    (output/'machine-board.h').write_text('typedef unsigned char u8;\ntypedef unsigned int u16;\ntypedef unsigned long u32;\nu32 batch_case(u8,u8);\n', newline='\n')
    source = '''#include "machine-board.h"
const __code u16 first[80]={FIRST};
const __code u16 second[80]={SECOND};
struct packet {u8 before; const u16 *cursor; u8 after;};
static __edata struct packet packet;
static volatile __edata struct packet observed;
volatile __bit flag;
void toggle(void) {flag=!flag;}
long calculate(u8 seed) {
    float x=(long)seed-31L;
    float y=(unsigned long)seed*257UL+3UL;
    return (long)((x*x+y)/4.0f)+(x<0.0f?19L:7L);
}
u32 table_mix(u8 seed) {
    return ((u32)first[seed]<<8)^second[seed];
}
void select_pointers(u8 seed) {
    if(seed&1) {packet.cursor=first; observed.cursor=second;}
    else {packet.cursor=second; observed.cursor=first;}
}
u32 batch_case(u8 kind,u8 seed) {
    u16 a,b;
    if(!kind) {
        return (u32)calculate(seed)^table_mix(seed);
    }
    packet.before=0x5a; packet.after=0xa5;
    select_pointers(seed);
    a=packet.cursor[seed]; b=observed.cursor[seed];
    if(a!=(seed&1?first[seed]:second[seed]))return 0xbad00001UL;
    if(b!=(seed&1?second[seed]:first[seed]))return 0xbad00002UL;
    if(packet.before!=0x5a || packet.after!=0xa5)return 0xbad00003UL;
    flag=seed&1; toggle();
    if(flag==(seed&1))return 0xbad00004UL;
    toggle(); if(flag!=(seed&1))return 0xbad00005UL;
    return 0x600d0000UL|seed;
}
'''.replace('FIRST', ','.join(str(i*509+17) for i in range(80))).replace('SECOND', ','.join(str(i*313+29) for i in range(80)))
    (output/'cases.c').write_text(source, newline='\n')
    board.write(output/'mx251.json', dict(schema=2, target='AI8051U-34K64-32',
        model=model, optimization='speed', backend='ordinary-wide', explicitAbi=False,
        standard='sdcc11', runtime='standard', defines=[]))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(shared_cost_batch NONE)\ninclude("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE machine-board.c cases.c machine-board.h)\nmx251_firmware(app)\n', newline='\n')
    board.write(output/'source.json', {p.name: board.sha(p) for p in output.iterdir() if p.is_file()})


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('create'); c.add_argument('--output', type=Path, required=True)
    c.add_argument('--model', choices=['small', 'large'], default='small'); c.add_argument('--interrupt', action='store_true')
    c = sub.add_parser('capture'); c.add_argument('--hex', type=Path, required=True); c.add_argument('--output', type=Path, required=True)
    c.add_argument('--interrupt', action='store_true')
    a = p.parse_args()
    if a.command == 'create': create(a.output, a.model, a.interrupt)
    else: board.capture(a.hex, a.output, lambda data: board.check(data, isr_saves_fixture.expected if a.interrupt else expected))
