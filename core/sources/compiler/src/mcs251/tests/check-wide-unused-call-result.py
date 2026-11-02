"""Unused ABI2 results: retain side effects and live results across CFG boundaries."""
from pathlib import Path
import argparse,subprocess,json,re
ap=argparse.ArgumentParser();ap.add_argument('--sdcc',type=Path,required=True);ap.add_argument('--baseline',type=Path,required=True);ap.add_argument('--output-dir',type=Path,required=True);a=ap.parse_args()
a.output_dir.mkdir(exist_ok=False);rows=[]
header='''typedef unsigned char u8; typedef unsigned short u16; typedef unsigned long u32;
#define OLD __reentrant
#define NATIVE __reentrant __sdcccall(4)
u8 side8(u32,u32) OLD; u16 side16(u32,u32) OLD; u32 side32(u32,u32) OLD;
float sidef(u32,u32) OLD; u32 __xdata *sidep(u32,u32) OLD; void sidev(u32,u32) OLD;
u32 nat(u32,u32) NATIVE;
'''
types=[('8','u8'),('16','u16'),('32','u32'),('f','float'),('p','u32 __xdata *')]
functions=[]
for suffix,t in types:
    functions += [f'u32 drop{suffix}(u32 a,u32 b) OLD {{side{suffix}(a,b);return a^b;}}',
                  f'{t} keep{suffix}(u32 a,u32 b) OLD {{return side{suffix}(a,b);}}',
                  f'u32 store{suffix}(u32 a,u32 b) OLD {{{t} volatile local;local=side{suffix}(a,b);return a^b;}}']
functions += ['u32 branch(u32 a,u32 b) OLD {u32 x=side32(a,b);if(a&1UL)return x+b;return a;}',
 'u32 loop(u32 a,u32 b) OLD {u32 x=side32(a,b);while(b){a+=x;b--;}return a;}',
 'u32 ignored_then_used(u32 a,u32 b) OLD {side32(a,b);return side32(b,a)^a;}',
 'u32 voidcall(u32 a,u32 b) OLD {sidev(a,b);return a^b;}',
 'u32 nativecall(u32 a,u32 b) OLD {nat(a,b);return a^b;}']
source=header+'\n'.join(functions)+'\n'
def parts(asm):
    result={}
    for chunk in re.split(r'(?=^; mcs251 wide-mir accepted: )',asm,flags=re.M)[1:]:
        name=re.match(r'; mcs251 wide-mir accepted: ([^;]+);',chunk)[1]
        result[name]=chunk.split('; mx251-effects',1)[0]
    return result
def code(s):return [re.sub(r'\s+','',l.split(';')[0]) for l in s.splitlines()[2:] if l.split(';')[0].strip()]
for model in ('small','large'):
 for opt in ('speed','size'):
  for feedback in (False,True):
   name=model+'-'+opt+('-feedback' if feedback else '-unknown-callees')
   outputs={}
   for role,cc in [('parent',a.baseline),('candidate',a.sdcc)]:
    d=a.output_dir/(name+'-'+role);d.mkdir();(d/'probe.c').write_text(source,encoding='utf-8')
    # A conservative externally supplied clobber summary exercises the feedback
    # path without pretending a fabricated callee is independently qualified.
    (d/'effects.txt').write_text('MX251-LEAF-EFFECTS-1\n'+''.join(f'_side{s} 2 ffffffff\n' for s in ('8','16','32','f','p','v'))+'___m251r4_nat 4 ffffffff\n')
    cmd=[str(cc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-wide-ordinary']
    if feedback:cmd+=['--mcs251-leaf-effects','effects.txt']
    cmd+=['-c','probe.c'];q=subprocess.run(cmd,cwd=d,capture_output=True,timeout=30)
    (d/'command.json').write_text(json.dumps(cmd,indent=2));(d/'compile.log').write_bytes(q.stdout+q.stderr);assert q.returncode==0,(name,q.stderr)
    outputs[role]=parts((d/'probe.asm').read_text())
   old,new=outputs['parent'],outputs['candidate'];assert old.keys()==new.keys()
   for suffix,t in types:
    f='drop'+suffix;assert f in new
    assert new[f].count('wide ordinary result unused:')==1
    assert re.findall(r'\tecall\t\S+',old[f])==re.findall(r'\tecall\t\S+',new[f])
    assert re.findall(r'\b(?:push|pop|sub\s+spx)\b[^\n]*',old[f])==re.findall(r'\b(?:push|pop|sub\s+spx)\b[^\n]*',new[f])
    for prefix in ('keep','store'):
     f=prefix+suffix;assert f in new and 'wide ordinary result unused:' not in new[f]
     assert code(old[f])==code(new[f]),f
   for f in ('branch','loop','voidcall','nativecall'):
    assert f in new and 'wide ordinary result unused:' not in new[f]
    assert code(old[f])==code(new[f]),f
   f='ignored_then_used';assert f in new and new[f].count('wide ordinary result unused:')==1
   assert len(re.findall(r'\tecall\t_side32',new[f]))==2
   # Disabled ordinary path must remain identical, including the native fallback.
   for role,cc in [('parent',a.baseline),('candidate',a.sdcc)]:
    d=a.output_dir/(name+'-'+role);cmd=[str(cc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-wide-backend','-c','probe.c','-o','disabled.rel']
    q=subprocess.run(cmd,cwd=d,capture_output=True,timeout=30);(d/'disabled.log').write_bytes(q.stdout+q.stderr);assert not q.returncode
   assert (a.output_dir/(name+'-parent')/'disabled.rel').read_bytes()==(a.output_dir/(name+'-candidate')/'disabled.rel').read_bytes()
   rows.append(dict(model=model,opt=opt,feedback=feedback,acceptedFunctions=len(new),discardedResultSites=6,usedAndVolatileAndCFGResultsRetained=True,disabledObjectIdentical=True))
(a.output_dir/'result.json').write_text(json.dumps(dict(passed=True,configurations=rows,hardware=False,limits='Assembly and object contracts only. Real fixed-function execution has a separate test; clobber summary here is deliberately conservative.'),indent=2)+'\n')
print('PASS',len(rows),'configurations; ignored return types, side effects, live CFG/volatile uses and fallback')
