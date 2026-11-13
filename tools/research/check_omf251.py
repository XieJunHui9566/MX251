"""Compare imported C251/A251 objects with L251 at two independent layouts."""
import argparse
from dataclasses import replace
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('omf251', REPO/'core/compat/keil/omf251.py')
omf = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = omf
spec.loader.exec_module(omf)


def run(output, label, args):
    result = subprocess.run(list(map(str,args)), cwd=output, capture_output=True)
    (output/(label+'.log')).write_bytes(result.stdout+result.stderr)
    (output/(label+'.command.json')).write_text(json.dumps(list(map(str,args)),indent=2))
    # L251 uses exit 1 for the expected no-main/root warnings in object probes.
    if result.returncode and not (label.startswith('link-keil-') and result.returncode == 1
                                 and b'0 ERROR(S)' in result.stdout):
        raise RuntimeError(f'{label}: exit {result.returncode}; see {output/(label+".log")}')


def image(path):
    memory, base = {}, 0
    for line in path.read_text().splitlines():
        b=bytes.fromhex(line[1:])
        assert sum(b)%256==0
        addr=int.from_bytes(b[1:3],'big')
        if b[3]==0:
            for i,v in enumerate(b[4:-1]):
                assert base+addr+i not in memory
                memory[base+addr+i]=v
        elif b[3]==2: base=int.from_bytes(b[4:6],'big')*16
        elif b[3]==4: base=int.from_bytes(b[4:6],'big')*65536
        elif b[3]!=1: raise ValueError('unexpected HEX record')
    return memory


SOURCES = {
    'spaces.c': '''unsigned char data dd;
unsigned char edata ee[3]; unsigned char xdata xx[5];
const unsigned char code cc[3]={0x12,0x34,0x56};
''',
    'refs.c': '''extern unsigned char edata ee[];
extern unsigned char xdata xx[]; extern const unsigned char code cc[];
extern unsigned int other(unsigned int);
unsigned int foo(unsigned int a) {ee[2]=xx[3]; return other(a)+cc[1];}
''',
    'pointers.c': '''extern unsigned char edata ee[];
extern unsigned char xdata xx[]; extern const unsigned char code cc[];
unsigned char edata * code ep=ee+2;
unsigned char xdata * code xp=xx+3;
const unsigned char code * code cp=cc+1;
unsigned char far * code fp=xx+3;
const unsigned char far * code fcp=cc+1;
unsigned char far * code fep=ee+2;
''',
    'control.a51': '''NAME control
PUBLIC entry, table
EXTRN CODE (other)
EXTRN EDATA (ee)
EXTRN XDATA (xx)
CONTROL_CODE SEGMENT CODE
CONTROL_CONST SEGMENT CODE
RSEG CONTROL_CODE
entry:
 MOV WR0,#ee+2
 MOV DPTR,#xx+3
 LCALL other
 MOV R2,ee+1
 ECALL other
 SJMP target
 DB 1,2,3
target:
 ERET
RSEG CONTROL_CONST
table:
 DB LOW ee,HIGH ee
 DW xx+4,other+1
END
''',
    'other.a51': '''NAME callee
PUBLIC other
CALLEE_CODE SEGMENT CODE
RSEG CALLEE_CODE
other:
 NOP
 RET
END
'''
}


def check(args):
    out=args.output
    out.mkdir(parents=True,exist_ok=False)
    modules, objects = [], []
    for name,text in SOURCES.items():
        (out/name).write_text(text,encoding='ascii')
        stem=Path(name).stem
        obj=stem+'.obj'
        options = ['XSMALL','ROM(LARGE)','OPTIMIZE(8,SPEED)','CODE'] if name.endswith('.c') else ['MODSRC','CASE']
        run(out, stem, [args.keil/('C251.EXE' if name.endswith('.c') else 'A251.EXE'), name,
                        *options, f'OBJECT({obj})'])
        modules += omf.read(out/obj)[0]
        objects.append(obj)
    (out/'import.asm').write_text(omf.assembly(modules,'test'),encoding='ascii')
    run(out,'assemble',[args.tools/'sdas251.exe','-plosgff','import.rel','import.asm'])
    rows=[]
    for layout in range(2):
        bases={5:0xff0123+layout*0x1000,7:0x234+layout*0x100,
               1:0x10123+layout*0x100,2:0x30+layout*0x10}
        segments,areas=[],[]
        for mi,m in enumerate(modules):
            for si,s in enumerate(m.segments,1):
                addr=bases[s.space]
                bases[s.space]+=s.size+7
                segments.append(f'{s.name}(0x{addr:x})')
                areas.append(f'-b OMF_test_M{mi}_S{si}=0x{addr:x}')
        run(out,f'link-keil-{layout}',[args.keil/'L251.EXE',','.join(objects),'TO',f'keil{layout}',
                                     'CASE','SEGMENTS('+','.join(segments)+')'])
        run(out,f'hex-keil-{layout}',[args.keil/'OH251.EXE',f'keil{layout}',f'HEXFILE(keil{layout}.hex)'])
        (out/f'layout{layout}.lk').write_text('\n'.join(['-mwx',f'-i mx{layout}.ihx',*areas,'import.rel','-e','']))
        run(out,f'link-mx-{layout}',[args.tools/'sdld.exe','-nf',f'layout{layout}.lk'])
        expected,actual=image(out/f'keil{layout}.hex'),image(out/f'mx{layout}.ihx')
        assert expected==actual, [(hex(k),expected.get(k),actual.get(k)) for k in sorted(expected.keys()|actual.keys()) if expected.get(k)!=actual.get(k)][:20]
        rows.append(dict(layout=layout,bytes=len(actual),equal=True))
    # Preserve unsupported forms as explicit inventory, never silent byte copies.
    changed=bytearray((out/'refs.obj').read_bytes());changed[-1]^=1
    try: list(omf.records(changed))
    except omf.OmfError: pass
    else: raise AssertionError('bad checksum accepted')
    first=modules[1].chunks[0]
    original=first.relocations[0]
    first.relocations[0]=replace(original,kind=0xff)
    try: omf.assembly(modules)
    except omf.OmfError: pass
    else: raise AssertionError('unknown relocation emitted')
    first.relocations[0]=original
    report=dict(status='PASS',modules=len(modules),segments=sum(len(m.segments) for m in modules),
                relocations=sum(len(c.relocations) for m in modules for c in m.chunks),
                layouts=rows,checksumRejection=True,unknownRelocationRejection=True,
                hardwareExecuted=False,cAbiAdapted=False)
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--keil',type=Path,required=True,help='C251 BIN directory')
    p.add_argument('--tools',type=Path,required=True,help='MX251 toolchain bin directory')
    p.add_argument('--output',type=Path,required=True)
    check(p.parse_args())
