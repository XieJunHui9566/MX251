"""Independent closed-form path counts; cross-path compensation must be rejected."""
import argparse
import itertools
import json
from pathlib import Path
import random
import subprocess

ap=argparse.ArgumentParser()
ap.add_argument('--probe',type=Path,required=True)
ap.add_argument('--output',type=Path,required=True)
a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=False);rows=[]

def probe(name,left,right):
    paths=[a.output/(name+'-'+kind+'.asm') for kind in ('parent','candidate')]
    for path,text in zip(paths,(left,right)):path.write_text(text,encoding='utf8')
    p=subprocess.run([str(a.probe.resolve()),*map(str,paths)],capture_output=True,timeout=15)
    assert p.returncode==0,(name,p.stderr)
    j=json.loads(p.stdout);rows.append(dict(name=name,result=j));return j

def payload(v):
    moves,reads,writes,pairs=v
    return '\tmov\tr4,r5\n'*moves+'\tmov\twr4,@spx-2\n'*reads+'\tmov\t@spx-2,wr4\n'*writes+'\tpush\tr6\n\tpop\tr6\n'*pairs

def metrics(v):
    m,r,w,p=v
    return [m+r+w+2*p,m+r+w,r+w,r,w,p,p]

def program(blocks,prefix=''):
    text=f'entry{prefix}:\nloop{prefix}:\n\tcmp\tr0,#0\n\tje\tdone{prefix}\n'
    for k,(left,right) in enumerate(blocks):
        text+=f'D{prefix}{k}:\n\tcmp\tr1,#0\n\tje\tT{prefix}{k}\n'+payload(left)+f'\tsjmp\tJ{prefix}{k}\nT{prefix}{k}:\n'+payload(right)+f'J{prefix}{k}:\n'
    text+=f'\tdec\tr0\n\tejmp\tloop{prefix}\ndone{prefix}:\n\teret\n'
    expected={(1,1,1):[2,0,0,0,0,0,0]}
    for choices in itertools.product((0,1),repeat=len(blocks)):
        value=[4,0,0,0,0,0,0]
        for k,choice in enumerate(choices):
            vector=metrics(blocks[k][choice]);vector[0]+=2+(1-choice)
            value=[x+y for x,y in zip(value,vector)]
        expected[((1<<(len(blocks)+1))-1,sum(c<<(k+1) for k,c in enumerate(choices)),0)]=value
    return text,expected

def actual(j):
    assert not j['unknown'],j
    return {(p['seen'],p['taken'],p['exited']):p['metrics'] for p in j['paths']}

rng=random.Random(138)
for k in range(100):
    n=rng.randrange(1,4)
    before=[[tuple(rng.randrange(3) for _ in range(4)) for _ in range(2)] for _ in range(n)]
    after=[[tuple(rng.randrange(x+1) for x in block) for block in pair] for pair in before] if k%3==0 else [[tuple(rng.randrange(3) for _ in range(4)) for _ in range(2)] for _ in range(n)]
    p,old=program(before);c,new=program(after);j=probe('generated-'+str(k),p,c)
    assert actual(j['parent'])==old and actual(j['candidate'])==new,(k,j)
    expected=int(all(all(y<=x for x,y in zip(old[key],new[key])) for key in old))
    assert j['decision']==expected,(k,j)

# Equal maximum/total cost is insufficient: the once-cheaper branch became expensive.
p,old=program([[(10,0,0,0),(0,0,0,0)]])
c,new=program([[(0,0,0,0),(10,0,0,0)]])
j=probe('cross-path-compensation',p,c)
assert j['decision']==0 and actual(j['parent'])==old and actual(j['candidate'])==new

for name,text in [('predicate-code',p.replace('\tje\tT0','\tjne\tT0')),('renamed-anchor',p.replace('D0:','other:')),('renamed-target',p.replace('T0','other')),('removed-branch',p.replace('\tje\tT0','\tejmp\tT0'))]:
    j=probe('identity-'+name,p,text);assert j['decision']==-1,(name,j)

# 7 diamonds produce 128 backedge paths plus one exit: bound must be observable.
large,_=program([[(0,0,0,0),(0,0,0,0)]]*7)
j=probe('path-budget',large,large);assert j['parent']['unknown'] and j['decision']==-1
# 17 disjoint one-diamond loops need 34 branch identities, beyond the 32-site budget.
large=''.join(program([[(0,0,0,0),(0,0,0,0)]],prefix=str(i)+'_')[0].replace('\teret\n','') for i in range(17))+'\teret\n'
j=probe('branch-budget',large,large);assert j['parent']['unknown'] and j['decision']==-1
# Exact 32-site limit exercises the unsigned top bit without exponential paths.
bounded=''.join(f'L{i}:\n\tcmp\tr0,#0\n\tje\tE{i}\n\tmov\tr4,r5\n\tejmp\tL{i}\nE{i}:\n' for i in range(32))+'\teret\n'
j=probe('branch-budget-boundary',bounded,bounded)
assert j['decision']==1 and j['parent']['branchCount']==32 and len(j['parent']['paths'])==64

(a.output/'results.json').write_text(json.dumps(dict(passed=True,cases=rows),indent=2)+'\n',encoding='utf8')
print('PASS',len(rows),'independent path, identity, compensation and budget cases')
