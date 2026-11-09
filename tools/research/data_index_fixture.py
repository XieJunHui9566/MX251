"""Exercise DATA indexing, address snapshots, byte/word/dword and volatile I/O."""
import argparse,shutil
from pathlib import Path
import machine_pipeline_fixture as board

def expected(kind,seed):
    if kind:return board.expected(kind,seed)
    octets=[(seed+13*i)&255 for i in range(8)]
    words=[0x1234+seed+257*i for i in range(8)]
    longs=[0x10203040+seed+0x01010101*i for i in range(4)]
    sampled=[seed+19*i for i in range(4)]
    values=[words[(seed+i)&7]^octets[(seed+i)&7] for i in range(8)]
    for i in range(4):
        index=(seed+i)&3
        sampled[index]+=seed+i
        values += [longs[index],sampled[index]]
    index=seed&3
    words[index+1]^=0x5a00+seed
    values += [words[index+1],words[index]]
    values += [words[(seed&3) if seed&1 else 2],words[5 if seed&2 else (seed&3)]]
    values += [words[seed&7],words[7-(seed&3)]]
    for i in range(3):
        words[i+1]+=seed+i
        values.append(words[i+1])
    result=0x13579bdf
    for value in values:result=((result<<5)^result^value)&0xffffffff
    return result

def create(output):
    output.mkdir(parents=True,exist_ok=False)
    tests=board.REPO/'core/sources/compiler/src/mcs251/tests'
    files=['machine-board.h','data-index.h','data-index-cases.c','data-index-objects.c','machine-control.c','machine-board.c']
    for name in files:shutil.copy2(tests/name,output/name)
    p=output/'machine-board.c';text=p.read_text().replace('#include "machine-board.h"','#include "data-index.h"\nu16 POINT_COLOR; u32 trace;')
    text=text.replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);\n            result = trace;','result = data_index_case(input);')
    p.write_text(text)
    units=[dict(name='u'+str(i),file=f,backend='legacy' if f=='machine-board.c' else 'ordinary-wide',stackAuto=True,defines=[]) for i,f in enumerate(files[2:])]
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[],files=files,units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(indexing NONE)\ninclude("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+' '.join(files)+')\nmx251_firmware(app)\n')
    board.write(output/'source.json',dict(files={f:board.sha(output/f) for f in files}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
