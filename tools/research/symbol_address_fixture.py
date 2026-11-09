"""Exercise relocatable pointer arguments, casts and mutable pointer paths."""
import argparse,shutil
from pathlib import Path
import machine_pipeline_fixture as board

ALPHA=b'Symbol-address-251';BETA=b'STACK-register-CODE'
def expected(kind,seed):
    if kind:return board.expected(kind,seed)
    def digest(data,count,salt):
        for octet in data[:count]:salt=((salt<<3)^salt^octet)&65535
        return salt
    values=[digest(ALPHA,8,seed),digest(BETA,6,seed+29)^digest(ALPHA[3:],6,seed+46)]
    table=[ALPHA,BETA,ALPHA[3:],BETA[4:]]
    values += [digest(table[(seed+i)&3],5,seed+i) for i in range(4)]
    data=ALPHA if seed&1 else BETA
    values += [digest(data[i:],3,seed+i) for i in range(4)]
    values += [digest(data[4:],3,seed+71),digest(bytes((seed+13*i)&255 for i in range(4)),4,seed+101)]
    result=0x13579bdf
    for value in values:result=((result<<5)^result^value)&0xffffffff
    return result

def create(output):
    output.mkdir(parents=True,exist_ok=False)
    tests=board.REPO/'core/sources/compiler/src/mcs251/tests'
    files=['machine-board.h','symbol-address.h','symbol-address-callers.c','symbol-address-callees.c','machine-control.c','machine-board.c']
    for name in files:shutil.copy2(tests/name,output/name)
    p=output/'machine-board.c';text=p.read_text().replace('#include "machine-board.h"','#include "symbol-address.h"\nu16 POINT_COLOR; u32 trace;')
    text=text.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;','result = address_case(input);')
    p.write_text(text)
    units=[dict(name='u'+str(i),file=f,backend='legacy' if f=='machine-board.c' else 'ordinary-wide',stackAuto=True,defines=[]) for i,f in enumerate(files[2:])]
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[],files=files,units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(addresses NONE)\ninclude("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+' '.join(files)+')\nmx251_firmware(app)\n')
    board.write(output/'source.json',dict(files={f:board.sha(output/f) for f in files}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
