"""Replay final C commands and find selected address rematerialization for full linking."""
import argparse,hashlib,json,os,re,shutil,subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def code_size(path):
    return sum(int(size,16) for name,size in re.findall(r'^A (\S+) size ([0-9A-Fa-f]+)',path.read_text(),re.M)
               if name=='CSEG' or name.startswith('CSEG$'))

def run(matrix,compiler,support,output):
    output.mkdir(parents=True,exist_ok=False)
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','SDCC_'))}
    env['SDCC_HOME']=str(support);env['PATH']=str(support/'bin')+os.pathsep+env['PATH']
    jobs=[]
    for row in read(matrix)['rows']:
        latest=Path(row['receipt']['path']).parent;commands={}
        for cmd in read(latest/'commands.json'):
            args=cmd['arguments']
            if Path(cmd['executable']).name=='sdcc.exe' and '-c' in args and '-o' in args:
                commands[Path(args[args.index('-o')+1]).stem]=args
        for unit,args in commands.items():jobs.append((row['id'],latest,unit,args))
    def one(job):
        pid,latest,unit,args=job;d=output/pid/unit;d.mkdir(parents=True)
        shutil.copytree(latest/'src',d/'src')
        for pattern in ('*.txt','*.c','*.h'):
            for f in latest.glob(pattern):shutil.copy2(f,d/f.name)
        receipt=read(latest/'receipt.json');local=dict(env);cap=receipt['wideCapacity']
        local.update(MX251_WIDE_VALUE_LIMIT=str(cap['valueLimit']),MX251_WIDE_NODE_LIMIT=str(cap['nodeLimit']))
        if receipt.get('objectAddressNative',{}).get('enabled'):
            local['MX251_OBJECT_ADDRESS_CAPABILITY']=receipt['objectAddressNative']['contract']
        row=dict(id=pid,unit=unit,source=str(latest),arguments=args)
        try:
            q=subprocess.run([str(compiler),*args],cwd=d,env=local,capture_output=True,timeout=180)
            (d/'compiler.log').write_bytes(q.stdout+q.stderr);row['exitCode']=q.returncode
            if not q.returncode:
                # Original objects have already undergone sharing/GC. Their
                # area sizes are not a like-for-like compiler comparison.
                row['compilerCodeBytes']=code_size(d/(unit+'.rel'))
                text=(d/(unit+'.asm')).read_text(errors='replace')
                row['addressRematerialization']=re.findall(r'^; wide address-remat choice function=(\S+) recipes=(\d+) selected=1 .*old-bytes=(\d+) new-bytes=(\d+)',text,re.M)
        except Exception as e:row.update(exitCode=-1,error=str(e))
        if row.get('addressRematerialization') or row['exitCode']:print(pid,unit,row.get('addressRematerialization'),row['exitCode'],flush=True)
        return row
    with ThreadPoolExecutor(max_workers=2) as pool:rows=list(pool.map(one,jobs))
    result=dict(matrix=dict(path=str(matrix),sha256=sha(matrix)),compiler=dict(path=str(compiler),sha256=sha(compiler)),
                units=len(rows),failed=sum(r['exitCode']!=0 for r in rows),
                changedProjects=sorted({r['id'] for r in rows if r.get('addressRematerialization')}),rows=rows)
    (output/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('DONE',result['units'],'units;',result['failed'],'failed;',len(result['changedProjects']),'changed projects',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('matrix','compiler','support','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();run(a.matrix,a.compiler,a.support,a.output)
