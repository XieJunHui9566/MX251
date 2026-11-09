"""Tail ABI values and real Timer0 preemption across all four register banks."""
import argparse,shutil
from pathlib import Path
import machine_pipeline_fixture as board

def fold(result,value):return ((result<<5)^result^value)&0xffffffff
def expected(kind,seed):
    result=0x13579bdf
    if kind:
        for i in range(32):result=fold(result,(i*7+3)&255)
        return fold(fold(result,1),0)
    x=0xf1234567^(seed*0x10001);y=0x13579bdf^(seed*257)
    leaf=lambda value:((value^0x89abcdef)+0x1234567)&0xffffffff
    values=[(x&255)^0xa5,((x&65535)+0x8123)&65535,leaf(x),x,0x10000+seed*17,
            leaf(x^y),(x+x+3)&0xffffffff,x^0x12345678^0x56789abc,x^0x56789abc,
            (leaf(x)+1)&0xffffffff,(x&255)^0xa5,((y<<3)^x)&0xffffffff,
            ((x<<3)^y)&0xffffffff,0x13579b00,0x13579b00+(seed&7),0x13579b00+(seed&7)]
    for value in values:result=fold(result,value)
    return result

def create(output):
    output.mkdir(parents=True,exist_ok=False)
    tests=board.REPO/'core/sources/compiler/src/mcs251/tests'
    files=['tail-boundary.h','tail-boundary-callers.c','tail-boundary-callees.c',
           'call-boundary-runtime.c','call-boundary-isr.c','machine-board.h']
    for name in files:shutil.copy2(tests/name,output/name)
    text=(tests/'machine-board.c').read_text()
    text=text.replace('static u8 checksum;','u32 tail_check(u8);\nu32 context_check(u8);\n'
        'extern volatile __data u8 context_image[40],context_count,context_errors;\nstatic u8 checksum;')
    start,end=text.index('        x0 ='),text.index('        TH0 =')
    text=text[:start]+text[end:]
    start,end=text.index('        if (kind == 0)'),text.index('        TR0 = 0; ticks')
    text=text[:start]+'''        if (kind == 0) result = tail_check(input);
        else { TR0 = 0; result = context_check(input); }
'''+text[end:]
    text=text.replace('ticks, x0, y0, x1, y1, x2, y2','ticks')
    text=text.replace('send(ticks >> 8); send(ticks); send(overflow); send(checksum); send(13); send(10);',
        '''send(ticks >> 8); send(ticks); send(overflow); send(checksum); send(13); send(10);
        if (kind) {
            u8 i; checksum = 0;
            send('C'); send('T'); send('X'); send('1'); send(input);
            for (i = 0; i < 40; ++i) send(context_image[i]);
            send(context_count); send(context_errors); send(checksum); send(13); send(10);
        }''')
    (output/'call-boundary-board.c').write_text(text,encoding='utf-8')
    asm=['.module boundary_probe','.optsdcc -mmcs251 --model-small','.globl _context_probe',
         '.globl _context_clobber','.globl _context_image','.globl _context_bank',
         '.area CSEG (CODE)','_context_probe:',
         '\tsetb 0x8d','\tmov psw,_context_bank']
    for r in range(0,32,4):
        value=int.from_bytes(bytes((i*7+3)&255 for i in range(r,r+4)),'big')
        # MOV DR,#imm zero-extends a 16-bit immediate on 251. Seed all
        # 32 physical bits with two word moves, as the compiler does.
        asm += [f'\tmov wr{r},#0x{value>>16:04x}',f'\tmov wr{r+2},#0x{value&65535:04x}']
    asm+=['\tmov dpx,spx','\tmov (_context_image+36),dpl','\tmov (_context_image+37),dph',
          '\tsetb c','\torl 0xd1,#0x22',
          '\tmov (_context_image+32),psw','\tmov (_context_image+33),0xd1',
          '\tsetb 0xaf']+['\tnop']*32
    asm+=['\tclr 0xaf','\tmov (_context_image+34),psw','\tmov (_context_image+35),0xd1',
          '\tmov dpx,spx','\tmov (_context_image+38),dpl','\tmov (_context_image+39),dph']
    for r in range(0,32,4):asm.append(f'\tmov (_context_image+{r}),dr{r}')
    asm+=['\tmov psw,#0','\teret','_context_clobber:']
    for r in range(0,32,4):asm += [f'\tmov wr{r},#0xdead',f'\tmov wr{r+2},#{r}']
    asm+=['\tmov dptr,#0x9876','\tmov dpxl,#0x5a','\tclr c','\torl 0xd1,#0x22','\teret']
    (output/'call-boundary-probe.asm').write_text('\n'.join(asm)+'\n')
    sources=['tail-boundary-callers.c','tail-boundary-callees.c','call-boundary-runtime.c',
             'call-boundary-isr.c','call-boundary-board.c','call-boundary-probe.asm']
    files+=['call-boundary-board.c','call-boundary-probe.asm']
    units=[dict(name='u'+str(i),file=name,backend='legacy' if name.endswith(('board.c','.asm')) else 'ordinary-wide',
                stackAuto=True,defines=[]) for i,name in enumerate(sources)]
    board.write(output/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=True,standard='sdcc11',runtime='standard',defines=[],files=files,
        units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (output/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(call_boundary NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\n'
        'target_sources(app INTERFACE '+' '.join(sources+['tail-boundary.h','machine-board.h'])+')\nmx251_firmware(app)\n')
    board.write(output/'source.json',dict(files={name:board.sha(output/name) for name in files}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
