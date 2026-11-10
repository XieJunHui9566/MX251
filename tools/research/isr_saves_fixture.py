"""Real Timer0/1 preemption: known calls and indexed/constant memory access."""
import argparse,json
from pathlib import Path
import call_boundary_fixture as boundary
import machine_pipeline_fixture as board

def expected(kind,seed):return boundary.expected(1,seed)

def create(output):
    boundary.create(output)
    path=output/'call-boundary-isr.c'
    source=path.read_text()
    start,end=source.index('void context_irq('),source.index('u32 context_check(')
    source=source[:start]+'''volatile __data u8 context_mode,context_index,context_value;
volatile __data u8 context_dpx[6];
volatile __xdata u8 context_buffer[16];
#define context_fixed (*(volatile __xdata u8 *)0x12000UL)
volatile __data u32 context_total;
void context_step(void);

void context_irq(void) __interrupt(1)
{
    context_step();
    ++context_count;
}

void context_leaf_irq(void) __interrupt(3)
{
    context_buffer[context_index] = context_value;
    if (context_fixed & 1) context_fixed &= 0xfe;
    ++context_count;
}

'''+source[end:]
    source=source.replace('IE = 0x02; TR0 = 0; TF0 = 0; TMOD = 1;',
        'IE = context_mode ? 0x08 : 0x02; TR0 = 0; TF0 = 0; TMOD = 0x11;\n'
        '    context_index = seed & 15; context_value = seed ^ 0xa5;\n'
        '    context_buffer[context_index] = 0; context_fixed = 0x93; context_total = 0x12345678UL;')
    source=source.replace('IE = 0; TF0 = 0;',
        'IE = 0; TF0 = 0;\n'
        '    if (context_mode) {\n'
        '        if (context_buffer[context_index] != context_value || context_fixed != 0x92) ++context_errors;\n'
        '    } else if (context_total != (0x12345678UL + 0x01234567UL)) ++context_errors;\n'
        '    for (i = 0; i < 3; ++i) if (context_dpx[i] != context_dpx[i+3]) ++context_errors;')
    path.write_text(source,newline='\n')
    helper='''typedef unsigned long u32;
extern volatile __data u32 context_total;
void context_step(void) { context_total += 0x01234567UL; }
'''
    (output/'isr-helper.c').write_text(helper,newline='\n')
    path=output/'call-boundary-probe.asm';text=path.read_text()
    text=text.replace('.globl _context_bank','.globl _context_bank\n.globl _context_mode\n.globl _context_dpx')
    text=text.replace('\tsetb c','\tmov dptr,#0x9876\n\tmov dpxl,#0x5a\n'
        '\tmov _context_dpx,dpl\n\tmov (_context_dpx+1),dph\n\tmov (_context_dpx+2),dpxl\n\tsetb c')
    text=text.replace('\tclr 0xaf','\tclr 0xaf\n\tmov (_context_dpx+3),dpl\n'
        '\tmov (_context_dpx+4),dph\n\tmov (_context_dpx+5),dpxl')
    text=text.replace('\tsetb 0x8d',
        '\tmov a,_context_mode\n\tjz 00001$\n\tsetb 0x8f\n\tsjmp 00002$\n00001$:\n\tsetb 0x8d\n00002$:')
    path.write_text(text,newline='\n')
    path=output/'call-boundary-board.c';text=path.read_text()
    text=text.replace('static u8 checksum;','extern volatile __data u8 context_mode;\nstatic u8 checksum;')
    text=text.replace('if (kind == 0) result = tail_check(input);\n        else { TR0 = 0; result = context_check(input); }',
        'TR0 = 0; context_mode = kind; result = context_check(input);')
    path.write_text(text,newline='\n')
    config=json.loads((output/'mx251.json').read_text())
    config['files'].append('isr-helper.c')
    config['units'].append(dict(name='u6',file='isr-helper.c',backend='ordinary-wide',stackAuto=True,defines=[]))
    config['link'].insert(-1,'u6');board.write(output/'mx251.json',config)
    path=output/'CMakeLists.txt';path.write_text(path.read_text().replace('tail-boundary.h machine-board.h','isr-helper.c tail-boundary.h machine-board.h'),newline='\n')
    board.write(output/'source.json',dict(files={name:board.sha(output/name) for name in config['files']}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
