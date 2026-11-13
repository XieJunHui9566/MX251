"""Cross-page tail groups, fixed jump tables, cycles, ABI2/4 and escaping entries."""
import argparse
from pathlib import Path
import machine_pipeline_fixture as board

HEADER = '''typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
extern volatile u16 tg_state;
extern volatile u8 tg_depth, tg_select;
'''


def expected(kind, seed):
    result = 0x13579bdf
    for i in range(8):
        x = (seed * 1031 + i * 337) & 65535
        if kind:
            arg = (((x ^ 0x1234) + 19) & 65535) ^ 0x5aa5
            pair = (((x ^ 0x2345) + ((x ^ 0x4321) + 37 & 65535)) & 65535) ^ 0xa55a
            escaped = (x * 3 + 7) & 65535
            value = arg ^ pair ^ escaped ^ (((x ^ 0x5678) * 3 + 7) & 65535)
        else:
            k = (seed + i) & 7
            routed = ((x + (k+1)*257) & 65535) ^ (0x1200+k*17)
            depth = (seed + i) & 15
            cyclic = x ^ (0x2468 if depth % 2 == 0 else 0x1357)
            value = routed ^ cyclic
        result = ((result << 3) ^ result ^ value) & 0xffffffff
    return result


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    prototypes = ''.join(f'u16 tg_action{i}();\n' for i in range(8))
    route = HEADER + prototypes + '''u16 tg_cycle_b() __sdcccall(4);
u16 tg_arg_leaf(u16);
u16 tg_pair_leaf(u16,u16) __sdcccall(4);
u16 tg_escape_leaf(u16);
u16 tg_route() {
    switch (tg_select) {
''' + ''.join(f'    case {i}: return tg_action{i}();\n' for i in range(8)) + '''    default: return tg_action0();
    }
}
u16 tg_front() { return tg_route(); }
u16 tg_cycle_a() __sdcccall(4) {
    if (!tg_depth) return tg_state ^ 0x2468;
    --tg_depth;
    return tg_cycle_b();
}
u16 tg_arg_head(u16 x) { return tg_arg_leaf(x ^ 0x1234); }
u16 tg_pair_head(u16 x,u16 y) __sdcccall(4) { return tg_pair_leaf(x ^ 0x2345,y + 37); }
u16 tg_escape_head(u16 x) { return tg_escape_leaf(x); }
'''
    leaf = HEADER + 'volatile u16 tg_state;\nvolatile u8 tg_depth, tg_select;\n'
    leaf += ''.join(f'u16 tg_action{i}() {{ return (u16)(tg_state + {(i+1)*257}u) ^ {0x1200+i*17}u; }}\n' for i in range(8))
    leaf += '''u16 tg_cycle_a() __sdcccall(4);
u16 tg_cycle_b() __sdcccall(4) {
    if (!tg_depth) return tg_state ^ 0x1357;
    --tg_depth;
    return tg_cycle_a();
}
u16 tg_arg_leaf(u16 x) { return (x + 19u) ^ 0x5aa5; }
u16 tg_pair_leaf(u16 x,u16 y) __sdcccall(4) { return (x + y) ^ 0xa55a; }
u16 tg_escape_leaf(u16 x) { return x * 3u + 7u; }
u16 (* volatile tg_callback)(u16) = tg_escape_leaf;
'''
    work = HEADER + '''u16 tg_front();
u16 tg_cycle_a() __sdcccall(4);
u16 tg_arg_head(u16);
u16 tg_pair_head(u16,u16) __sdcccall(4);
u16 tg_escape_head(u16);
extern u16 (* volatile tg_callback)(u16);
u32 tg_work(u16 seed,u8 kind) {
    u8 i;
    u16 x,value;
    u32 result = 0x13579bdfUL;
    for (i=0; i<8; ++i) {
        x=seed*1031u+(u16)i*337u;
        if (kind) {
            value=tg_arg_head(x) ^ tg_pair_head(x,x ^ 0x4321);
            value^=tg_escape_head(x) ^ tg_callback(x ^ 0x5678);
        } else {
            tg_state=x; tg_select=(seed+i)&7;
            value=tg_front();
            tg_depth=(seed+i)&15;
            value^=tg_cycle_a();
        }
        result=(result<<3) ^ result ^ value;
    }
    return result;
}
'''
    tests=board.REPO/'core/sources/compiler/src/mcs251/tests'
    main=(tests/'machine-board.c').read_text().replace('static u8 checksum;','u32 tg_work(u16,u8);\nvoid tg_gap(void);\nstatic u8 checksum;')
    main=main.replace('u16 ticks, x0, y0, x1, y1, x2, y2;','u16 ticks;').replace('    EA = 0;','    EA = 0;\n    tg_gap();')
    begin,end=main.index('        x0 ='),main.index('        TH0 =');main=main[:begin]+main[end:]
    begin,end=main.index('        if (kind == 0)'),main.index('        TR0 = 0; ticks')
    main=main[:begin]+'        result = tg_work(input,kind);\n'+main[end:]
    files={'route.c':route,'leaves.c':leaf,'work.c':work,'gap.c':'void tg_gap(void) {\n__asm\n'+'nop\n'*1800+'__endasm;\n}\n','board.c':main,'machine-board.h':(tests/'machine-board.h').read_text()}
    for name,text in files.items():(output/name).write_text(text,newline='\n')
    units=[dict(name='u'+str(i),file=f,backend='legacy' if f in ('gap.c','board.c') else 'ordinary-wide',stackAuto=True,defines=[]) for i,f in enumerate(list(files)[:-1])]
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[],files=list(files),units=units,archives=[],link=['u0','u3','u1','u2','u4','@startup']))
    board.write(output/'source.json',dict(files={f:board.sha(output/f) for f in files}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
