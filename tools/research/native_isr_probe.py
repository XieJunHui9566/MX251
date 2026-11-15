"""Replay final compiler commands for ISR units with frozen call feedback."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


def run(matrix, compiler, output, ids):
    output.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256(compiler.read_bytes()).hexdigest()
    identity = dict(compilerSha256=digest,matrix=str(matrix),ids=sorted(ids))
    stamp = output/'inputs.json'
    if stamp.exists(): assert json.loads(stamp.read_text()) == identity
    else: stamp.write_text(json.dumps(identity,indent=2)+'\n')
    env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'SDCC_'))}
    support = matrix/'stc32g/candidate/core/toolchain'
    env['SDCC_HOME'] = str(support)
    env['PATH'] = str(support/'bin')+';'+env['PATH']
    jobs = []
    for corpus in ('stc32g', 'ai8051u'):
        for latest in sorted((matrix/corpus).glob('projects/*/mx251/.build/mx251/latest')):
            pid = latest.parents[3].name
            if ids and pid not in ids: continue
            if not (latest/'receipt.json').exists():
                latest = matrix/'gc-fixed/projects'/pid/'mx251/.build/mx251/latest'
            commands = json.loads((latest/'commands.json').read_text(encoding='utf-8-sig'))
            final = {}
            for cmd in commands:
                args = cmd['arguments']
                if Path(cmd['executable']).name == 'sdcc.exe' and '-c' in args and '-o' in args:
                    final[Path(args[args.index('-o')+1]).stem] = args
            for unit, args in final.items():
                asm = latest/(unit+'.asm')
                if asm.exists() and re.search(r'; mx251-function begin .* isr=1 ', asm.read_text(errors='replace')):
                    jobs.append((pid, latest, unit, args))

    def one(job):
        pid, latest, unit, args = job
        receipt = json.loads((latest/'receipt.json').read_text(encoding='utf-8-sig'))
        local_env = dict(env)
        capacity = receipt['wideCapacity']
        local_env.update(MX251_WIDE_VALUE_LIMIT=str(capacity['valueLimit']),
                         MX251_WIDE_NODE_LIMIT=str(capacity['nodeLimit']))
        if receipt.get('objectAddressNative',{}).get('enabled'):
            local_env['MX251_OBJECT_ADDRESS_CAPABILITY'] = receipt['objectAddressNative']['contract']
        work = output/pid/unit
        work.mkdir(parents=True,exist_ok=True)
        status=0
        if not (work/(unit+'.rel')).exists():
            shutil.copytree(latest/'src', work/'src',dirs_exist_ok=True)
            for pattern in ('*.txt', '*.c', '*.h'):
                for source in latest.glob(pattern): shutil.copy2(source, work/source.name)
            proc = subprocess.run([str(compiler), *args], cwd=work, env=local_env, capture_output=True, timeout=120)
            (work/'compiler.log').write_bytes(proc.stdout+proc.stderr)
            status=proc.returncode
        text = (work/(unit+'.asm')).read_text(errors='replace') if (work/(unit+'.asm')).exists() else ''
        row = dict(id=pid, unit=unit, exitCode=status, source=str(latest), arguments=args, capacity=capacity,
                   choices=re.findall(r'; mx251-isr-choice function=(\S+) selected=(\S+)', text),
                   costs=re.findall(r'; mx251-backend-choice .*', text),
                   refusals=re.findall(r'; wide (?:refusal|admission) .*', text))
        print(pid, unit, status, row['choices'], flush=True)
        return row

    with ThreadPoolExecutor(max_workers=4) as pool: rows = list(pool.map(one, jobs))
    report = dict(compiler=str(compiler), compilerSha256=digest,
                  matrix=str(matrix), units=len(rows), failed=sum(r['exitCode']!=0 for r in rows), rows=rows)
    (output/'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('matrix','compiler','output'): p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--ids',default='')
    a = p.parse_args()
    run(a.matrix.resolve(),a.compiler.resolve(),a.output.resolve(),set(filter(None,a.ids.split(','))))
