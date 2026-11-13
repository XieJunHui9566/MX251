"""Independent byte-assembly results for the AI8051U board fixture."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


def expected(kind,seed):
    d=[(seed*73+0x80)&255,seed^255,(seed*13+0x55)&255,((seed<<2)^(seed>>1)^0x81)&255]
    be=int.from_bytes(bytes(d),'big');u32=lambda x:x&0xffffffff
    a,b=0x81020300+seed,0x7ffe4500^(seed<<9)^seed
    if not kind:
        values=[be,int.from_bytes(bytes(d),'little'),(d[0]<<8)|d[1],be,be,be,
            (d[0]<<24)|(d[0]<<16)|(d[1]<<8)|d[1],(d[2]<<24)|0x005a0000|(d[3]<<8)]
    else:
        signed=[x if x<128 else x-256 for x in d]
        values=[u32((signed[0]<<24)|(signed[1]<<16)|(signed[2]<<8)|signed[3]),
            (d[0]+d[1])<<8,((d[0]<<16)+d[1])^(d[0]<<16),
            (d[1]<<8)|d[2],u32(be+u32(a+0x12345678)+((b^0x87654321)^u32(a-b))+u32(a+b)),a^be]
    result=0x13579bdf
    for value in values:result=u32((result<<3)^result^value)
    return result


def create(output):
    output.mkdir(parents=True,exist_ok=False)
    tests=board.REPO/'core/sources/compiler/src/mcs251/tests'
    files=['byte-pack.h','byte-pack.c','byte-pack-cases.c','pack-board.c']
    for name in files[:-1]:shutil.copy2(tests/name,output/name)
    source=(tests/'machine-board.c').read_text().replace('machine-board.h','byte-pack.h')
    begin,end=source.index('        x0 = '),source.index('        TH0 = ')
    source=source[:begin]+source[end:]
    begin,end=source.index('        if (kind == 0)'),source.index('        TR0 = 0; ticks')
    source=source[:begin]+'        result = pack_case(kind,input);\n'+source[end:]
    source=source.replace('u16 ticks, x0, y0, x1, y1, x2, y2;','u16 ticks;')
    (output/'pack-board.c').write_text(source,newline='\n')
    units=[dict(name='u'+str(i),file=f,backend='legacy' if f=='pack-board.c' else 'ordinary-wide',stackAuto=True,defines=[])
        for i,f in enumerate(files) if f.endswith('.c')]
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=True,standard='sdcc11',runtime='standard',defines=[],files=files,units=units,
        archives=[],link=[u['name'] for u in units]+['@startup']))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(pack NONE)\n'
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
