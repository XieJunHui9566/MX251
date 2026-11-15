"""Consecutive ABI2/4 calls with live values, stack arguments and interrupt frames."""
import argparse
import json
from pathlib import Path
import native_isr_fixture as native
import isr_saves_fixture as isr
import machine_pipeline_fixture as board


def digest(seed,mode):
    sample=seed^0xa5
    total=native.expected_total(sample,mode)
    value=0x13579bdf
    for first,word,last in [(1,0x1234,0x89abcdef),(2,0x2345,0x76543210),
                            (3,0x3456,0xfedcba98),(sample,(sample<<5)^0x9876,total)]:
        value=((value^last)+(word<<3)+first)&0xffffffff
    return value


def create(output):
    native.create(output)
    path=output/'call-boundary-isr.c';source=path.read_text()
    source=source.replace('u32 context_sum(u8 first,u16 word,u32 value);',
        'u32 context_sum(u8 first,u16 word,u32 value);\n'
        'volatile __edata u32 context_digest;\n'
        'void context_record(u8 first,u16 word,u32 value);\n'
        'void context_native(u8 first,u16 word,u32 value) __reentrant __sdcccall(4);\n')
    tables=''.join('const __code u32 digest_'+str(mode)+'[64]={'+','.join(
        hex(digest(seed,mode))+'UL' for seed in range(64))+'};\n' for mode in (0,1))
    source=source.replace('void context_irq(void)',tables+'void context_irq(void)')
    sequence='''{
    u8 sample=context_value;u16 word=((u16)sample<<5)^0x9876u;
    context_record(1,0x1234u,0x89abcdefUL);
    context_record(2,0x2345u,0x76543210UL);
    context_record(3,0x3456u,0xfedcba98UL);
    context_record(sample,word,total);
    }
    context_total=total;'''
    source=source.replace('context_total=total;',sequence)
    # The framed IRQ runs the same transport through ABI4, including its third
    # stack argument. The final call consumes the held values themselves.
    at=source.index('void context_leaf_irq(');end=source.index('u32 context_check(',at)
    source=source[:at]+source[at:end].replace('context_record(','context_native(')+source[end:]
    source=source.replace('context_total = 0x12345678UL;',
        'context_total = 0x12345678UL; context_digest=0x13579bdfUL;')
    source=source.replace('if(context_total !=',
        'if(context_digest != (context_mode ? digest_1[seed] : digest_0[seed]))++context_errors;\n'
        '    if(context_total !=')
    path.write_text(source,newline='\n')
    helper=output/'isr-helper.c'
    helper.write_text(helper.read_text()+'''
extern volatile __edata u32 context_digest;
void context_clobber(void);
void context_record(u8 first,u16 word,u32 value) {
    context_digest=(context_digest^value)+((u32)word<<3)+first;
    context_clobber();
}
void context_native(u8 first,u16 word,u32 value) __reentrant __sdcccall(4) {
    context_digest=(context_digest^value)+((u32)word<<3)+first;
    context_clobber();
}
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
