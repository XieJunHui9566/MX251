"""Default EDATA: declarations, mixed spaces, initialization and capacity."""
from pathlib import Path
import argparse
import json
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--toolchain',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();t=a.toolchain.resolve();a.output.mkdir(parents=True,exist_ok=False)
flags=['-mmcs251','--model-small','--stack-auto','--mcs251-default-edata',
       '--std-sdcc11','--nostdinc','--nostdlib','--mcs251-function-metadata']
layout=['--code-loc','0xff0000','--xram-loc','0x10000','--xram-size','0x7000',
        '--iram-size','256','--mcs251-stack','0x400,0x400,0x1000']
libs=[t/'lib/mcs251-small-stack-auto'/(n+'.lib') for n in ('mcs251','libsdcc','libint','liblong','liblonglong')]
checks=[]


def run(root,label,args,success=True,diagnostic=None):
    q=subprocess.run(list(map(str,args)),cwd=root,capture_output=True)
    log=q.stdout+q.stderr;(root/(label+'.log')).write_bytes(log)
    assert (q.returncode==0)==success,(label,log.decode(errors='replace'))
    if diagnostic:assert diagnostic.encode() in log,(label,log)
    checks.append(dict(case=root.name,step=label,exitCode=q.returncode))


for backend in ('legacy','ordinary-wide'):
    root=a.output/backend;root.mkdir()
    extra=['--mcs251-wide-ordinary'] if backend=='ordinary-wide' else []
    for name in ('edata','edata-other'):
        text=Path(__file__).with_name(name+'.c').read_text().replace('__edata','')
        if name=='edata':
            text+='\nextern volatile u16 word;\nvolatile __data unsigned char direct;\n'
        (root/(name+'.c')).write_text(text)
        run(root,name,[t/'bin/sdcc.exe',*flags,*extra,'-c',name+'.c'])
    run(root,'link',[t/'bin/sdcc.exe',*flags,*layout,'edata.rel','edata-other.rel',*libs,'-o','firmware.hex'])
    mapping=(root/'firmware.map').read_text()
    def address(symbol):
        return int(re.search(r'^(?:\w:)?\s+([0-9A-F]+)\s+'+re.escape(symbol)+r'\s',mapping,re.M)[1],16)
    assert address('_buffer')==0x100
    assert 0x100<=address('_word')<0x400
    assert address('_fixed')==0x300 and address('_result')==0x10000
    assert address('_direct')<0x100
    asm=(root/'edata.asm').read_text()
    assert asm.count('; EDATA unit clear:')==1
    assert asm.count('mx251-function begin schema=1 symbol=_static_value ')==1
    assert '; EDATA clear _fixed:' in asm

for size,success in ((768,True),(769,False)):
    root=a.output/('capacity-'+str(size));root.mkdir()
    (root/'main.c').write_text(f'unsigned char buffer[{size}]; void main(void) {{}}')
    run(root,'compile',[t/'bin/sdcc.exe',*flags,'-c','main.c'])
    run(root,'link',[t/'bin/sdcc.exe',*flags,*layout,'main.rel',*libs,'-o','firmware.hex'],success,None if success else 'MCS251 EDATA')
root=a.output/'model';root.mkdir();(root/'main.c').write_text('unsigned char value;')
for option in ('--model-large','--model-small'):
    # The second case omits stack-auto.
    chosen=[x for x in flags if x not in ('--model-small','--stack-auto')]+[option]
    run(root,option[2:],[t/'bin/sdcc.exe',*chosen,'-c','main.c'],False,'requires --model-small --stack-auto')
(a.output/'results.json').write_text(json.dumps(dict(status='PASS',checks=checks),indent=2)+'\n')
print('PASS',len(checks),'default EDATA checks')
