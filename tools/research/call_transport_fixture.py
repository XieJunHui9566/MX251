"""Independent scoped static functions, CODE/data pointers and a callback."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


def expected(kind,seed):
    mask=0xffffffff
    if not kind:
        values=[(seed*29+i*17)&255 for i in range(8)]
        table=[3,19,71,129,251,17,96,203]
        result=seed
        for k in range(8):
            result=((result<<3)^(values[(seed+k)&7]^0x5a))&mask
            values[k]^=(k*11+7)&255
            result^=table[k]
        return (result+sum(values))&mask
    words=[(0x8100+seed*67+i*293)&65535 for i in range(8)]
    result=seed^0x12345678
    for k in range(8):
        result=((result<<5)^((words[(seed+k)&7]+0x1357)&65535))&mask
        result=(result+(((seed+k)&255)^0xa7))&mask
    return result


def create(output):
    output.mkdir(parents=True,exist_ok=False)
    tests=board.REPO/'core/sources/compiler/src/mcs251/tests'
    files=['call-transport.h','call-transport-left.c','call-transport-right.c',
           'call-transport-cases.c','transport-board.c']
    for name in files[:-1]:shutil.copy2(tests/name,output/name)
    source=(tests/'machine-board.c').read_text().replace('machine-board.h','call-transport.h')
    begin,end=source.index('        x0 = '),source.index('        TH0 = ')
    source=source[:begin]+source[end:]
    begin,end=source.index('        if (kind == 0)'),source.index('        TR0 = 0; ticks')
    source=source[:begin]+'        result = transport_case(kind,input);\n'+source[end:]
    source=source.replace('u16 ticks, x0, y0, x1, y1, x2, y2;','u16 ticks;')
    (output/'transport-board.c').write_text(source,newline='\n')
    units=[dict(name='u'+str(i),file=f,backend='ordinary-wide',stackAuto=True,defines=[])
           for i,f in enumerate(files) if f.endswith('.c')]
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[],
        files=files,units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(transport NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+
        ' '.join(files)+')\nmx251_firmware(app)\n',newline='\n')
    board.write(output/'source.json',dict(files={f:board.sha(output/f) for f in files}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
