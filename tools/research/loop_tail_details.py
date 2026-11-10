"""Extract paired final listings and assemble unlinked cost sketches only."""
from pathlib import Path
import argparse,json,re,subprocess
from call_layout import read_unit,ihex,sha
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--study',type=Path,required=True)
parser.add_argument('--comparison',type=Path,required=True)
a=parser.parse_args();n=a.study
comparison=json.loads(a.comparison.read_text(encoding='utf8'));rows={r['id']:r for r in comparison['rows']}
inv=json.loads((n/'inventory.json').read_text(encoding='utf8'))
focus=[]
for pid,unit,name in [('p051','u0','_UpdatePwm'),('p074','u0','_main'),('p095','u7','_SetMCLK'),('p134','u0','_LoadSPI'),('p039','u0','_get_temperature')]:
 result=json.loads(Path(rows[pid]['evidence']['candidate']['path']).read_text(encoding='utf8'));root=Path(result['hex']['path']).parent;u=read_unit(root,unit,ihex(root/'firmware.hex'));instructions=[i for i in u['instructions'] if i.owner==name]
 reference=Path(rows[pid]['evidence']['keil']['path']).parent;keil=[]
 for p in reference.glob('unit*.lst'):
  text=p.read_text(encoding='latin1')
  m=re.search(r';\s+FUNCTION '+re.escape(name[1:])+r' \(BEGIN\)(.*?);\s+FUNCTION '+re.escape(name[1:])+r' \(END\)',text,re.S|re.I)
  if m:keil.append(dict(path=str(p),sha256=sha(p),text=m[0]))
 maps=(reference/'FIRMWARE.MAP').read_text(encoding='latin1').splitlines()
 segments=[l for l in maps if re.search(r'\?PR\?'+re.escape(name[1:])+r'\?',l,re.I)]
 size=sum(int(re.match(r'^[0-9A-F]+H\s+[0-9A-F]+H\s+([0-9A-F]+)H',l)[1],16) for l in segments)
 focus.append(dict(id=pid,unit=unit,function=name,mxBytes=sum(len(i.data) for i in instructions),keilBytes=size,keilSegments=segments,mxInputs=u['inputs'],keil=keil,mxInstructions=[dict(line=i.line,address=i.address,bytes=len(i.data),source=i.source) for i in instructions]))
 (n/(pid+'-'+name[1:]+'.txt')).write_text('MX final\n'+'\n'.join(f'{i.address:06x} {len(i.data)}B {i.source}' for i in instructions)+'\n\nKeil frozen\n'+'\n'.join(k['text'] for k in keil),encoding='utf8')
(n/'focus.json').write_text(json.dumps(focus,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
# Isolated, unlinked assembly shapes for static encoding costs only.
sketch=n/'encoding-sketch';sketch.mkdir(exist_ok=False)
loop=next(f for f in focus if f['id']=='p074')['mxInstructions'];first=next(i for i,x in enumerate(loop) if re.fullmatch(r'mov\s+dr0,dr12',x['source']));last=next(i for i in range(first,len(loop)) if re.fullmatch(r'jc\s+__wm__main_14',loop[i]['source']))
old=[x['source'].replace('__wm__main_14','study_old_loop') for x in loop[first:last+1]]
new=[]
for src,dst in [(12,4),(28,24)]:
 new += [f'mov r8,@dr{src}',f'mov @dr{dst},r8',f'inc dr{src},#1',f'anl wr{src},#0x00ff',f'inc dr{dst},#1',f'anl wr{dst},#0x00ff']
new+=['inc r9','cmp r9,#0x10','jc study_cursor_loop']
pwm=next(f for p in inv['projects'] if p['id']=='p051' for f in p['functions'] if f['name']=='_UpdatePwm')['parameterizedTail']['bodies']
regions={'old_loop':['study_old_loop:']+old,'cursor_loop':['study_cursor_loop:']+new,'old_pwm_tails':sum(pwm,[])}
shared=pwm[0].copy();shared.pop(1);shared=[('mov wr18,wr26' if x.startswith('mov wr18,#') else x) for x in shared]
selected=[]
for i,body in enumerate(pwm):
 addresses=[x.split(',')[1] for x in body if x.startswith('mov wr18,#')]
 selected += [f'study_arm_{i}:','mov wr18,'+addresses[0],'mov wr26,'+addresses[1]]
 if i<2:selected+=['ejmp study_shared']
regions['shared_pwm_tails']=selected+['study_shared:']+shared
exe=Path(comparison['compiler']['path']).with_name('sdas251.exe');records=[]
for name,lines in regions.items():
 p=sketch/(name+'.asm');p.write_text('.module study\n.optsdcc -mmcs251 --model-small\n.area CSEG (CODE)\n'+'\n'.join(lines)+'\n',encoding='ascii')
 q=subprocess.run([str(exe),'-plosgff',name+'.rel',name+'.asm'],cwd=sketch,capture_output=True);(sketch/(name+'.log')).write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
 rel=(sketch/(name+'.rel')).read_text();size=int(re.search(r'^A CSEG size ([0-9A-F]+)',rel,re.M)[1],16)
 records.append(dict(name=name,bytes=size,instructions=sum(not l.endswith(':') for l in lines),source=dict(path=str(p),sha256=sha(p)),listing=dict(path=str(sketch/(name+'.lst')),sha256=sha(sketch/(name+'.lst')))))
(n/'encoding-sketch.json').write_text(json.dumps(dict(scope='Unlinked illustrative register assignments; static encoding only, no compiler optimization or runtime qualification.',assembler=dict(path=str(exe),sha256=sha(exe)),regions=records),indent=2)+'\n',encoding='utf8')
print('Focus:',[(f['id'],f['function'],f['mxBytes'],f['keilBytes']) for f in focus]);print('Sketch:',[(r['name'],r['bytes'],r['instructions']) for r in records])
