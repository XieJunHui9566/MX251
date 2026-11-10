"""Execute ABI4 snapshots and native division with independent Python results."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board

DIVISORS=[1,2,3,7,255,256,257,65535,65536,65537,
          0x7fffffff,0x80000000,0xffffffff,300,115200,460800]

def expected(kind,seed):
    if kind:
        return board.expected(kind,seed)
    divisor=DIVISORS[seed&15]
    rate=(0xffffffff-0x01010101*seed)&0xffffffff
    value=0x01234567+seed
    word=(0xff00+seed*17)&65535
    keep=rate^divisor
    first=((value^keep)+(word<<16)+3)&0xffffffff
    value=(value+0x01020304)&0xffffffff
    word^=keep&65535
    second=((value^first)+(word<<16)+3)&0xffffffff
    value=(value+0x01020304)&0xffffffff
    word^=first&65535
    obj=(-(((rate//divisor+2)&0xffffffff)//4))&65535
    constant={1:0x1357,256:0x2468,65537:0xabcd,115200:0x9876}.get(divisor)
    if constant is None:
        constant=(-((30000000//divisor+2)//4))&65535
    byte=((seed^0xa5)+seed+7)&255
    return (second^keep^((obj<<16)|constant)^byte^value^word)&0xffffffff

def create(output):
    output.mkdir(parents=True,exist_ok=False)
    tests=board.REPO/'core/sources/compiler/src/mcs251/tests'
    files=['machine-board.h','native-call-composition.h','native-call-composition.c',
           'native-call-callees.c','machine-control.c','machine-board.c']
    for name in files:
        shutil.copy2(tests/name,output/name)
    p=output/'machine-board.c'
    text=p.read_text().replace('#include "machine-board.h"',
        '#include "machine-board.h"\nu32 native_work(u8);\nu16 POINT_COLOR;u32 trace;')
    text=text.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;',
                      'result = native_work(input);')
    p.write_text(text)
    units=[dict(name='u'+str(i),file=f,backend='legacy' if f=='machine-board.c' else 'ordinary-wide',
                stackAuto=True,defines=[]) for i,f in enumerate(files[2:])]
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',
        optimization='speed',backend='ordinary-wide',explicitAbi=False,standard='sdcc11',
        runtime='standard',defines=[],files=files,units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    board.write(output/'source.json',dict(files={f:board.sha(output/f) for f in files}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    s=p.add_subparsers(dest='command',required=True)
    c=s.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=s.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    c=s.add_parser('check');c.add_argument('file',type=Path)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    elif a.command=='capture':board.capture(a.hex,a.output,lambda data:board.check(data,expected))
    else:print(board.check(a.file.read_bytes(),expected))
