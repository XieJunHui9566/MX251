"""Address rematerialization on independent user-style structures and loops."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


def expected(kind,seed):
    b=[(seed*73+i*29+0x81)&255 for i in range(32)]
    count=3+(seed&3)
    u32=lambda x:x&0xffffffff
    if not kind:
        a=(seed+3)&255;v=(seed*7)&255;c=seed^0x81;d=(seed+17)&255
        x=(0xf100+seed*137)&65535;y=(0x1234+seed*61)&65535
        tag=u32(0x81020300+seed*0x01030507);result=0
        for remaining in range(count-1,-1,-1):
            result=u32(result+((a+v+x+y)&65535))
            c^=(remaining*13+7)&255
            result^=u32(tag+c*257)
            x=(x+d)&65535;y^=x
        subtotal=0
        for i in range(count):
            subtotal=u32(subtotal+b[i]);b[31]^=(i*13+7)&255;b[i]^=i+3
            subtotal=u32(subtotal<<1)^b[i]
        return u32(result^subtotal^(x<<16|y))
    result=0;at=0
    for remaining in range(count-1,-1,-1):
        at+=1;b[5]^=(remaining*13+7)&255
        result=u32((result<<1)^b[5]);result=u32(result+b[at+2])
    before=b[4];b[4]^=(seed*13+7)&255
    target=[7,11,3,5][seed&3]
    result^=before<<16|b[4]<<8|b[target]
    return u32(result^(b[5]<<24|b[4]<<16))


def create(output):
    output.mkdir(parents=True,exist_ok=False)
    tests=board.REPO/'core/sources/compiler/src/mcs251/tests'
    files=['address-remat.h','address-remat-access.c','address-remat-cases.c','address-board.c']
    for name in files[:-1]:shutil.copy2(tests/name,output/name)
    source=(tests/'machine-board.c').read_text().replace('machine-board.h','address-remat.h')
    begin,end=source.index('        x0 = '),source.index('        TH0 = ')
    source=source[:begin]+source[end:]
    begin,end=source.index('        if (kind == 0)'),source.index('        TR0 = 0; ticks')
    source=source[:begin]+'        result = remat_case(kind,input);\n'+source[end:]
    source=source.replace('u16 ticks, x0, y0, x1, y1, x2, y2;','u16 ticks;')
    (output/'address-board.c').write_text(source,newline='\n')
    units=[dict(name='u'+str(i),file=f,backend='legacy' if f=='address-board.c' else 'ordinary-wide',stackAuto=True,defines=[])
           for i,f in enumerate(files) if f.endswith('.c')]
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=True,standard='sdcc11',runtime='standard',defines=[],files=files,units=units,
        archives=[],link=[u['name'] for u in units]+['@startup']))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(address NONE)\n'
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
