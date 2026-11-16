"""Independent complete-body and mixed CODE/RAM loop execution fixture."""
import argparse
from pathlib import Path
import machine_pipeline_fixture as board
import shared_cost_batch_fixture as batch


def expected(kind, seed):
    if kind:
        first = [i * 7 % 256 for i in range(1024)]
        second = [i * 13 % 256 for i in range(1024)]
        return (seed * 3 + sum(first) + sum(second)) & 0xffffffff
    cursor = seed & 15
    total = 0
    for _ in range(32):
        value = (cursor + seed) & 31
        total = (total * 33 + value * 7) & 0xffffffff
        total = (total * 33 + ((~(1 << (cursor & 7))) & 255)) & 0xffffffff
        cursor = (cursor + 1) & 15
    return total


def create(output):
    batch.create(output, 'small')
    source = '''#include "machine-board.h"
const __code u8 pattern[32]={PATTERN}, mask[16]={MASK};
const __code u8 first[1024]={FIRST}, second[1024]={SECOND};
__data u8 state[16], cursor;
__xdata u8 buffer[1024];
volatile __bit strobe;
extern u32 total;
void consume(u8);
void drain(u8);
void scan(void) {
    consume(pattern[state[cursor]]); consume(~mask[cursor]);
    strobe=1; strobe=0;
    if(++cursor>=16)cursor=0;
}
void copy_tables(u8 rounds,u8 seed) {
    u16 i;
    while(rounds--) {
        for(i=0;i<1024;++i)buffer[i]=0;
        drain(seed);
        for(i=0;i<1024;++i)buffer[i]=first[i];
        drain(seed);
        for(i=0;i<1024;++i)buffer[i]=second[i];
        drain(seed);
    }
}
u32 batch_case(u8 kind,u8 seed) {
    u8 i;
    total=0;
    if(kind)copy_tables(1,seed);
    else {
        for(i=0;i<16;++i)state[i]=(i+seed)&31;
        cursor=seed&15;
        for(i=0;i<32;++i)scan();
        if(strobe)return 0xbad00001UL;
    }
    return total;
}
'''
    for name, values in dict(PATTERN=[i*7 for i in range(32)],
                             MASK=[1<<(i&7) for i in range(16)],
                             FIRST=[i*7%256 for i in range(1024)],
                             SECOND=[i*13%256 for i in range(1024)]).items():
        source=source.replace(name,','.join(map(str,values)))
    (output/'cases.c').write_text(source,newline='\n')
    (output/'consume.c').write_text('''#include "machine-board.h"
u32 total;
extern __xdata u8 buffer[1024];
void consume(u8 value) {total=total*33+value;}
void drain(u8 seed) {
    u16 i;
    u32 value=seed;
    for(i=0;i<1024;++i)value+=buffer[i];
    total+=value;
}
''',newline='\n')
    p=output/'CMakeLists.txt'
    p.write_text(p.read_text().replace('cases.c machine-board.h','cases.c consume.c machine-board.h'),newline='\n')
    board.write(output/'source.json',{p.name:board.sha(p) for p in output.iterdir() if p.is_file() and p.name!='source.json'})


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
