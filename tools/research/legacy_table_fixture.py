"""Legacy four-byte jump slots and split 24-bit address tables in near groups."""
import argparse
import json
from pathlib import Path
import re
from call_layout import ihex
import machine_pipeline_fixture as board


def expected(kind, seed):
    result = 0x13579bdf
    for i in range(24):
        select = (seed + i) % (21 if kind else 6)
        count = 17 if kind else 4
        value = (seed * 1031 + i * 337) & 65535
        if select < count:
            value = ((value + (select + 1) * 257) & 65535) ^ (0x1200 + select * 17)
        else:
            value ^= 0xa55a
        result = ((result << 3) ^ result ^ value) & 0xffffffff
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    header = 'typedef unsigned char u8;\ntypedef unsigned int u16;\ntypedef unsigned long u32;\nextern volatile u8 lt_select;\nextern volatile u16 lt_state;\n'
    route = header + 'volatile u8 lt_select;\nvolatile u16 lt_state;\n'
    for name, count in (('small', 4), ('large', 17)):
        route += f'u16 lt_{name}() {{\n switch (lt_select) {{\n'
        for i in range(count):
            route += f' case {i}: lt_state += {(i+1)*257}u; return lt_state ^ {0x1200+i*17}u;\n'
        route += ' default: return lt_state ^ 0xa55a;\n }\n}\n'
    front = header + 'u16 lt_small();\nu16 lt_large();\nu16 lt_small_head() { return lt_small(); }\nu16 lt_large_head() { return lt_large(); }\n'
    work = header + '''u16 lt_small_head();
u16 lt_large_head();
u32 lt_work(u16 seed,u8 kind) {
 u8 i;
 u16 value;
 u32 result=0x13579bdfUL;
 for(i=0;i<24;++i) {
  lt_select=(seed+i) % (kind ? 21 : 6);
  lt_state=seed*1031u+(u16)i*337u;
  value=kind ? lt_large_head() : lt_small_head();
  result=(result<<3)^result^value;
 }
 return result;
}
'''
    tests=board.REPO/'core/sources/compiler/src/mcs251/tests'
    main=(tests/'machine-board.c').read_text().replace('static u8 checksum;','u32 lt_work(u16,u8);\nstatic u8 checksum;')
    main=main.replace('u16 ticks, x0, y0, x1, y1, x2, y2;','u16 ticks;')
    begin,end=main.index('        x0 ='),main.index('        TH0 =');main=main[:begin]+main[end:]
    begin,end=main.index('        if (kind == 0)'),main.index('        TR0 = 0; ticks')
    main=main[:begin]+'        result = lt_work(input,kind);\n'+main[end:]
    files={'route.c':route,'front.c':front,'work.c':work,'board.c':main,'machine-board.h':(tests/'machine-board.h').read_text()}
    for name,text in files.items():(output/name).write_text(text,newline='\n')
    units=[dict(name='u'+str(i),file=f,backend='legacy' if f in ('route.c','board.c') else 'ordinary-wide',stackAuto=True,defines=[]) for i,f in enumerate(list(files)[:-1])]
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[],files=list(files),units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    board.write(output/'source.json',dict(files={f:board.sha(output/f) for f in files}))


def check_layout(root, output):
    plan=json.loads((root/'near-transfers.json').read_text())
    groups=[set(g['members']) for g in plan['groups']]
    assert {'_lt_small_head','_lt_small'} in groups
    assert {'_lt_large_head','_lt_large'} in groups
    memory=ihex(root/'firmware.hex');lines=(root/'u0.asm').read_text().splitlines();listing={};labels={};owner=''
    for row in (root/'u0.rst').read_text().splitlines():
        m=re.match(r'\s*(\d+) (.*)',row[34:]) if len(row)>34 else None
        if m and re.fullmatch('[0-9A-F]{6}',row[6:12]):
            listing[int(m[1])]=(int(row[6:12],16),bytes.fromhex(row[13:28]))
    for i,line in enumerate(lines):
        m=re.search(r'mx251-function begin schema=1 symbol=(\S+)',line)
        if m:owner=m[1]
        if re.fullmatch(r'[\w.$]+:',line) and i+1 in listing:labels[owner,line[:-1]]=listing[i+1][0]
    tables=[]
    for i,line in enumerate(lines):
        m=re.fullmatch(r';\s*mx251-jump-table schema=1 owner=(\S+) label=(\S+) entries=(\d+) stride=4 dispatch=a-dptr',line)
        if m:
            assert lines[i+1]==m[2]+':'
            addresses=[]
            for j in range(i+2,i+2+int(m[3])):
                target=re.fullmatch(r'\s*ejmp\s+(\S+)',lines[j])[1]
                address,data=listing[j+1]
                assert len(data)==4 and bytes(memory[address+k] for k in range(4))==data
                assert int.from_bytes(data[1:],'big')==labels[m[1],target]
                addresses.append(address)
            assert all(b-a==4 for a,b in zip(addresses,addresses[1:]))
            tables.append(dict(kind='ejmp',entries=len(addresses),stride=4))
        m=re.fullmatch(r';\s*mx251-address-table schema=1 owner=(\S+) low=(\S+) high=(\S+) ext=(\S+) entries=(\d+)',line)
        if m:
            count=int(m[5]);bases=[labels[m[1],m[p]] for p in (2,3,4)]
            j=i+2
            for entry in range(count):
                target=re.fullmatch(r'\s*\.db\s+(\S+)',lines[j+entry])[1]
                address=sum(memory[base+entry]<<(8*part) for part,base in enumerate(bases))
                assert address==labels[m[1],target]
            tables.append(dict(kind='address-bytes',entries=count,planes=3))
    assert tables==[dict(kind='ejmp',entries=4,stride=4),dict(kind='address-bytes',entries=17,planes=3)],tables
    board.write(output,dict(status='PASS',tables=tables,groups=plan['groups'],firmwareSha256=board.sha(root/'firmware.hex')))
    print('PASS legacy dispatch groups, fixed slot widths and relocated 24-bit destinations')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('check-layout');c.add_argument('--root',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    elif a.command=='check-layout':check_layout(a.root,a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
