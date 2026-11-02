"""Check mixed-width allocation, overlapping spill slots and deterministic output."""
from pathlib import Path
import argparse,subprocess,re,json
p=argparse.ArgumentParser();p.add_argument('--sdcc',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False);rows=[]
for model in ['small','large']:
 for opt in ['speed','size']:
  d=a.output_dir/(model+'-'+opt);d.mkdir();(d/'test.c').write_bytes(a.source.read_bytes()+b'\n'+(a.source.parent/'wide-spill-reuse.c').read_bytes())
  args=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto','--opt-code-'+opt,'--mcs251-wide-backend','--mcs251-native-int-runtime','-c','test.c']
  q=subprocess.run(args,cwd=d,capture_output=True);(d/'compile.log').write_bytes(q.stdout+q.stderr);assert not q.returncode,q.stderr
  first=(d/'test.rel').read_bytes();asm=(d/'test.asm').read_text();accepted=re.findall(r'wide-mir accepted: (\w+)',asm)
  assert accepted==['p_touch','p_mixed','p_consume','p_spill','p_phases'] and 'wide-mir fallback' not in asm,accepted
  shared=0;functions=[]
  for name in accepted:
   body=asm.split('wide-mir accepted: '+name+';',1)[1].split('; wide allocation ',1)[0]
   frame=int(re.search(r'frame=(\d+)',body)[1]);objects=[(int(s),int(w)) for s,w in re.findall(r'; wide object \S+ slot=(\d+) size=(\d+)',body)]
   values=[tuple(map(int,[i,w,g,s]))+(int(e.replace(':',''),16),) for i,w,g,s,e in re.findall(r'; wide v(\d+) \S+ width=(\d+) reg=(-?\d+) slot=(-?\d+) edges=([0-9a-f:]+)',body)]
   assert len(values)==int(re.search(r'values=(\d+)',body)[1])
   occupied=set();count=0;spilledWidths=set()
   for s,w in objects:
    region=set(range(s,s+w));assert region.isdisjoint(occupied) and s>=0 and s+w<=frame;occupied|=region
   for i,w,g,s,e in values:
    assert w in [1,2,4] and not e&(1<<i) and e<(1<<len(values))
    if g<0:
     region=set(range(s,s+w));assert s>=0 and s+w<=frame and region.isdisjoint(occupied);spilledWidths.add(w)
    else:
     assert g%w==0 and g+w<=32 and (w!=1 or g<16) and set(range(g,g+w)).isdisjoint(range(16,24))
    for j,v,h,t,edges in values:
     assert bool(e&(1<<j))==bool(edges&(1<<i))
     if j>=i:continue
     if g<0 and h<0:
      overlap=not set(range(s,s+w)).isdisjoint(range(t,t+v))
      assert not (overlap and e&(1<<j)),(name,i,j)
      count+=overlap
     elif g>=0 and h>=0 and e&(1<<j):assert set(range(g,g+w)).isdisjoint(range(h,h+v)),(name,i,j)
   if name=='p_mixed':assert {v[1] for v in values}=={1,2,4} and objects
   shared+=count;functions.append(dict(name=name,frame=frame,sharedNoninterferingPairs=count,spilledWidths=sorted(spilledWidths)))
  assert shared>0,'fixture must exercise real slot sharing'
  q=subprocess.run(args,cwd=d,capture_output=True);(d/'determinism.log').write_bytes(q.stdout+q.stderr)
  assert not q.returncode and first==(d/'test.rel').read_bytes()
  rows.append(dict(model=model,optimization=opt,functions=functions,deterministic=True,registerAndSpillInterferenceChecked=True))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2));print('PASS 4 mixed-width configurations, real slot reuse, interference and deterministic machine code')
