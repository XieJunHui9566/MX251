"""Rectangle trace through a small two-call wrapper with automatic preservation."""
import argparse
import json
from pathlib import Path
import callee_preserved_fixture as rectangle
import machine_pipeline_fixture as board


def create(source, output):
    rectangle.create(source, output)
    (output/'pixels.c').write_text('''#include "machine-board.h"
u16 POINT_COLOR;
u32 trace;
void set_coordinates(u16,u16,u16,u16);
void record_color(u16);
void LCD_DrawPoint(u16 x,u16 y) {
    set_coordinates(x,y,x,y);
    record_color(POINT_COLOR);
}
''')
    (output/'transport.c').write_text('''#include "machine-board.h"
static u16 px,py;
void set_coordinates(u16 x,u16 y,u16 right,u16 bottom) {
    px=x; py=y;
    /* All four transported arguments contribute to the checked trace. */
    trace=(trace<<5)^trace^(right^x)^(bottom^y);
}
void record_color(u16 color) {
    trace=(trace<<5)^trace^px;
    trace=(trace<<5)^trace^py;
    trace=(trace<<5)^trace^color;
}
''')
    config=json.loads((output/'mx251.json').read_text())
    config['files'].append('transport.c')
    unit=dict(name='u'+str(len(config['units'])),file='transport.c',backend='ordinary-wide',stackAuto=True,defines=[])
    config['units'].append(unit);config['link'].insert(-1,unit['name'])
    board.write(output/'mx251.json',config)
    p=output/'CMakeLists.txt'
    p.write_text(p.read_text().replace(')\nmx251_firmware(app)',' transport.c)\nmx251_firmware(app)'))
    board.write(output/'source.json',dict(origin=str(source),originSha256=board.sha(source),files={f:board.sha(output/f) for f in config['files']}))


def expected(kind,seed):
    if kind:return board.expected(kind,seed)
    x1,y1=10+seed,2+(seed&7)
    x2,y2=40+(seed&7),3+((seed>>1)&7)
    trace=0x12345678
    for x,y,right,bottom in [(x1,y1,x2,y1),(x1,y1,x1,y2),(x1,y2,x2,y2),(x2,y1,x2,y2)]:
        dx,dy=right-x,bottom-y;ix=(dx>0)-(dx<0);iy=(dy>0)-(dy<0)
        dx,dy=abs(dx),abs(dy);distance=max(dx,dy);ex=ey=0
        for _ in range(distance+2):
            for value in [0,x&65535,y&65535,0x5a00+seed]:trace=((trace<<5)^trace^value)&0xffffffff
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
