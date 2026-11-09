"""Check existing all-stack varargs from the wide caller, including sprintf."""
import argparse,shutil
from pathlib import Path
import machine_pipeline_fixture as board

def expected(kind,seed):
    if kind:return board.expected(kind,seed)
    def mix(result,value):return ((result<<5)^result^value)&0xffffffff
    word=0x8000+seed
    result=0x12345678+seed
    for value in ((seed-32)&65535,word,0x31415926+seed,b'VARARGS251'[seed&7],seed+6,0x3fc00000):
        result=mix(result,value)
    sample=0x6000+seed;alias=(0x2300+seed+word)&65535
    values=[(sample<<16)^sample^seed,0x1111,(word+105)&65535,alias,alias,word^0xa55a,
            (0x4567+seed)^((seed+7)<<8)^seed,b'VARARGS251'[seed&7]]
    text=f'{seed+16:02x}:{seed+64}:V'.encode()
    values += [len(text),*text,0]
    for value in values:result=mix(result,value)
    return result^0xc0200000

def create(output):
    output.mkdir(parents=True,exist_ok=False)
    tests=board.REPO/'core/sources/compiler/src/mcs251/tests'
    files=['machine-board.h','variadic-call.h','variadic-callers.c','variadic-callees.c','machine-control.c','machine-board.c']
    for name in files:shutil.copy2(tests/name,output/name)
    p=output/'machine-board.c';text=p.read_text().replace('#include "machine-board.h"','#include "variadic-call.h"\nu16 POINT_COLOR; u32 trace;')
    text=text.replace('u32 result;','u32 result;\n    union { float value; u32 bits; } floating;')
    text=text.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;','result = variadic_case(input);\n            floating.value = variadic_float_caller(input);\n            result ^= floating.bits;')
    p.write_text(text)
    units=[dict(name='u'+str(i),file=f,backend='legacy' if f in ('machine-board.c','variadic-callees.c') else 'ordinary-wide',stackAuto=True,defines=[]) for i,f in enumerate(files[2:])]
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[],files=files,units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(variadic NONE)\ninclude("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+' '.join(files)+')\nmx251_firmware(app)\n')
    board.write(output/'source.json',dict(files={f:board.sha(output/f) for f in files}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
