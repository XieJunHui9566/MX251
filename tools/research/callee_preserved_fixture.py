"""Original rectangle/line algorithms with a pixel trace and independent oracle."""
import argparse,re,shutil
from pathlib import Path
import machine_pipeline_fixture as board

def extract(data,name):
    m=re.search(rb'\nvoid '+name.encode()+rb'\([^\n]*\)\s*\{',data)
    assert m,name
    start=m.start()+1;end=m.end();depth=1
    while depth:
        depth+=(data[end]==123)-(data[end]==125);end+=1
    return data[start:end]+b'\n'

def create(source,output):
    output.mkdir(parents=True,exist_ok=False)
    tests=board.REPO/'core/sources/compiler/src/mcs251/tests'
    header=(tests/'machine-board.h').read_text()+'\nvoid LCD_DrawPoint(u16,u16);\nvoid LCD_DrawRectangle(u16,u16,u16,u16);\n'
    (output/'machine-board.h').write_text(header)
    (output/'rectangle.c').write_bytes(b'#include "machine-board.h"\n'+
        extract(source.read_bytes(),'LCD_DrawLine')+extract(source.read_bytes(),'LCD_DrawRectangle'))
    (output/'pixels.c').write_text('''#include "machine-board.h"
u16 POINT_COLOR;
u32 trace;
void LCD_DrawPoint(u16 x,u16 y) {
    trace=(trace<<5)^trace^x;
    trace=(trace<<5)^trace^y;
    trace=(trace<<5)^trace^POINT_COLOR;
}
''')
    shutil.copy2(tests/'machine-control.c',output/'machine-control.c')
    text=(tests/'machine-board.c').read_text().replace('Fill_Triangel(x0,y0,x1,y1,x2,y2);','LCD_DrawRectangle(x0,y0,x1,y1);')
    (output/'machine-board.c').write_text(text)
    files=['machine-board.h','rectangle.c','pixels.c','machine-control.c','machine-board.c']
    units=[dict(name='u'+str(i),file=f,backend='legacy' if f=='machine-board.c' else 'ordinary-wide',
                stackAuto=True,defines=[]) for i,f in enumerate(files[1:])]
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[],files=files,
        units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(preserved NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE '+
        ' '.join(files)+')\nmx251_firmware(app)\n')
    board.write(output/'source.json',dict(origin=str(source),originSha256=board.sha(source),
        functions=['LCD_DrawLine','LCD_DrawRectangle'],files={f:board.sha(output/f) for f in files}))

def expected(kind,seed):
    if kind:return board.expected(kind,seed)
    x1,y1=10+seed,2+(seed&7)
    x2,y2=40+(seed&7),3+((seed>>1)&7)
    trace=0x12345678
    for x,y,right,bottom in [(x1,y1,x2,y1),(x1,y1,x1,y2),(x1,y2,x2,y2),(x2,y1,x2,y2)]:
        dx,dy=right-x,bottom-y;ix=(dx>0)-(dx<0);iy=(dy>0)-(dy<0)
        dx,dy=abs(dx),abs(dy);distance=max(dx,dy);ex=ey=0
        for _ in range(distance+2):
            for value in [x&65535,y&65535,0x5a00+seed]:trace=((trace<<5)^trace^value)&0xffffffff
            ex+=dx;ey+=dy
            if ex>distance:ex-=distance;x+=ix
            if ey>distance:ey-=distance;y+=iy
    return trace

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--source',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.source,a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
