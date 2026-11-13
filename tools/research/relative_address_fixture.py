"""Same-input relative-address firmware with an independent integer oracle."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


def expected(kind,seed):
    d=[((seed*73+i*29+0x81)^(seed>>1))&255 for i in range(24)]
    u32=lambda x:x&0xffffffff
    tag=u32(0x81020300+seed*0x01030507)
    forward=lambda:int.from_bytes(bytes(d[4:8]),'big')
    reverse=lambda:int.from_bytes(bytes(d[4:8]),'little')
    if not kind:
        offset=seed&7
        values=[forward(),reverse(),forward(),forward(),(d[4]<<16)|(d[5+offset]<<8)|d[6+offset]]
    else:
        a=d[4]+d[7] if seed&1 else d[4]^d[8]
        values=[(d[4]<<16)|(d[5]<<8)|d[6],(a<<8)|d[5],u32(d[4]*tag)^(d[5]<<8)^d[6]]
        d[4:8]=tag.to_bytes(4,'big')
        values += [forward(),reverse(),d[3]*256+d[8]]
    result=0x13579bdf
    for value in values:result=u32((result<<3)^result^value)
    return result


def create(output):
    output.mkdir(parents=True,exist_ok=False)
    tests=board.REPO/'core/sources/compiler/src/mcs251/tests'
    files=['relative-address.h','relative-address.c','relative-address-cases.c','address-board.c']
    for name in files[:-1]:shutil.copy2(tests/name,output/name)
    source=(tests/'machine-board.c').read_text().replace('machine-board.h','relative-address.h')
    begin,end=source.index('        x0 = '),source.index('        TH0 = ')
    source=source[:begin]+source[end:]
    begin,end=source.index('        if (kind == 0)'),source.index('        TR0 = 0; ticks')
    source=source[:begin]+'        result = address_case(kind,input);\n'+source[end:]
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
