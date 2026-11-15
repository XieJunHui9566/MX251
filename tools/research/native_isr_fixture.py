"""Bank0 wide ISR bodies under real preemption of all four register banks."""
import argparse
import json
from pathlib import Path
import isr_saves_fixture as isr
import machine_pipeline_fixture as board


def expected_total(value, framed):
    total = 0x89abcdef
    for i in range(9):
        total = ((total << 3) ^ (total >> 7) ^ ((value+i)*0x12345)) & 0xffffffff
    if framed: total = ((total ^ 0x56780000)+value) & 0xffffffff
    return total


def create(output):
    isr.create(output)
    path=output/'call-boundary-isr.c'
    source=path.read_text().replace('volatile __data u8 context_image[40];',
        'volatile __data u8 context_reserved_banks[24];\nvolatile __data u8 context_image[40];')
    source=source.replace('volatile __data u32 context_total;',
        'volatile __edata u32 context_total;\n'
        'u32 context_sum(u8 first,u16 word,u32 value);\n')
    tables=''.join('const __code u32 expected_'+str(mode)+'[64]={'+','.join(
        hex(expected_total(seed^0xa5,mode))+'UL' for seed in range(64))+'};\n' for mode in (0,1))
    body='''u8 i;u32 total=0x89abcdefUL;
    for(i=0;i<9;i++)total=(total<<3)^(total>>7)^((u32)(context_value+i)*0x12345UL);
'''
    start,end=source.index('void context_irq('),source.index('u32 context_check(')
    source=source[:start]+tables+'''void context_irq(void) __interrupt(1) {
'''+body+'''context_total=total;
    ++context_count;
    if(context_value & 1)return;
    context_buffer[context_index]=context_value;
}
void context_leaf_irq(void) __interrupt(3) {
    volatile u8 guard[9];
'''+body+'''for(i=0;i<9;i++)guard[i]=i^context_value;
    total=context_sum(context_value,0x5678u,total);
    context_clobber();
    for(i=0;i<9;i++)if(guard[i]!=(i^context_value))++context_errors;
    context_total=total;
    ++context_count;
}
'''+source[end:]
    begin=source.index('    if (context_mode) {',source.index('u32 context_check'))
    end=source.index('    for (i = 0; i < 3;',begin)
    source=source[:begin]+'''    if(context_total != (context_mode ? expected_1[seed] : expected_0[seed]))++context_errors;
    if(!context_mode && !(context_value & 1) && context_buffer[context_index]!=context_value)++context_errors;
'''+source[end:]
    path.write_text(source,newline='\n')
    (output/'isr-helper.c').write_text('''typedef unsigned char u8;typedef unsigned int u16;typedef unsigned long u32;
u32 context_sum(u8 first,u16 word,u32 value){return (value^((u32)word<<16))+first;}
''',newline='\n')
    config=json.loads((output/'mx251.json').read_text())
    board.write(output/'source.json',dict(files={name:board.sha(output/name) for name in config['files']}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    q=sub.add_parser('create');q.add_argument('--output',type=Path,required=True)
    q=sub.add_parser('capture');q.add_argument('--hex',type=Path,required=True);q.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,isr.expected))
