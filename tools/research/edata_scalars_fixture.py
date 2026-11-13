"""EDATA across ABI2/ABI4, volatile globals, float bits and pointer aliases."""
import argparse
import json
from pathlib import Path
import argument_object_loads_fixture as arguments
import machine_pipeline_fixture as board


def expected(kind, seed):
    alias = ((~(0x10203040+seed) & 0xffffffff)+seed) & 0xffffffff
    return arguments.expected(kind, seed)^alias


def create(output):
    arguments.create(output)
    for p in output.iterdir():
        if p.suffix not in ('.c','.h'):continue
        text=p.read_text().replace('__data','__edata')
        for declaration in ['u32 source_rate, sample_a;', 'u16 sample_word;', 'u8 sample_byte;']:
            text=text.replace('__edata '+declaration,'volatile __edata '+declaration)
        p.write_text(text)
    p=output/'machine-board.c'
    p.write_text(p.read_text().replace('u32 native_work(u8);','u32 native_work(u8); u32 edata_alias(u8);')
        .replace('        TR0 = 0; ticks','        result ^= edata_alias(input);\n        TR0 = 0; ticks'))
    (output/'edata-alias.c').write_text('''#include "native-call-composition.h"
extern __edata u32 alias_object;
__edata __at(0x300) u32 absolute_object;
u32 alias_mutate(u32 __edata *p,u32 value) __sdcccall(4);
u32 edata_alias(u8 seed) {
    absolute_object=alias_mutate(&alias_object,0x10203040UL+seed);
    return absolute_object+seed;
}
''')
    (output/'edata-mutate.c').write_text('''#include "native-call-composition.h"
__edata u32 alias_object;
u32 alias_mutate(u32 __edata *p,u32 value) __sdcccall(4) {
    alias_object=value;
    *p=~value;
    return alias_object;
}
''')
    config=json.loads((output/'mx251.json').read_text())
    for index, name in enumerate(['edata-alias.c','edata-mutate.c']):
        config['files'].append(name)
        config['units'].append(dict(name='edata'+str(index),file=name,backend='ordinary-wide',stackAuto=True,defines=[]))
    config['link']=[u['name'] for u in config['units']]+['@startup']
    board.write(output/'mx251.json',config)
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(edata_scalars NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+
        ' '.join(config['files'])+')\nmx251_firmware(app)\n')
    board.write(output/'source.json',dict(files={f:board.sha(output/f) for f in config['files']}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
