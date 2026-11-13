"""Compare indexed addressing with independent integer-width reference values."""
import argparse
import json
from pathlib import Path
import shutil
import pointer_stride_fixture as stride
import machine_pipeline_fixture as board

BYTES=[3,19,41,67,89,113,137,163,181,199,211,223,229,239,241,251]
WORDS=[0,1,127,128,255,256,32767,32768,65535,0x5555,0xaaaa,0x1234,0x8001,0x7ffe,0xff00,0x00ff]

def expected(kind,seed):
    if not kind:
        return stride.expected(kind,seed)
    result=0x13579bdf
    cell=lambda i:((i*29)^(i>>3)^0x96)&255
    for i in range(8):
        offset=seed+i*8
        values=[cell(2*offset),cell(4*offset),BYTES[2*((seed+i)&7)],
                WORDS[(seed+i)&15],cell(2*offset)^(2*offset),(seed+i)&255]
        for value in values:
            result=((result<<5)^result^value)&0xffffffff
    return result

def create(output):
    stride.create(output)
    tests=board.REPO/'core/sources/compiler/src/mcs251/tests'
    names=['indexed-address.h','indexed-address-access.c','indexed-address-cases.c']
    for name in names:shutil.copy2(tests/name,output/name)
    p=output/'machine-board.c'
    text=p.read_text().replace('#include "pointer-stride.h"','#include "pointer-stride.h"\nvoid indexed_init(void);\nu32 indexed_case(u8);')
    text=text.replace('stride_init();','stride_init();\n    indexed_init();').replace('else result = control(input);','else result = indexed_case(input);')
    p.write_text(text)
    config=json.loads((output/'mx251.json').read_text())
    config['files']+=names
    for name in names[1:]:
        unit=dict(name='u'+str(len(config['units'])),file=name,backend='ordinary-wide',stackAuto=True,defines=[])
        config['units'].append(unit);config['link'].insert(-1,unit['name'])
    board.write(output/'mx251.json',config)
    cmake=output/'CMakeLists.txt'
    cmake.write_text(cmake.read_text().replace(')\nmx251_firmware(app)',' '+' '.join(names)+')\nmx251_firmware(app)'))
    board.write(output/'source.json',dict(files={name:board.sha(output/name) for name in config['files']}))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='command',required=True)
    cmd=sub.add_parser('create');cmd.add_argument('--output',type=Path,required=True)
    cmd=sub.add_parser('capture');cmd.add_argument('--hex',type=Path,required=True);cmd.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.command=='create':create(args.output)
    else:board.capture(args.hex,args.output,lambda data:board.check(data,expected))
