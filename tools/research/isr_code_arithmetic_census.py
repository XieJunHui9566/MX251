"""Replay each final C compile with identical feedback to find affected projects."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def run(before, compiler, output):
    output.mkdir(parents=True, exist_ok=True)
    env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'SDCC_'))}
    support = before/'candidate/core/toolchain'
    env['SDCC_HOME'] = str(support)
    env['PATH'] = str(support/'bin')+';'+env['PATH']
    parent = support/'bin/sdcc.exe'
    identity = dict(before=str(before), compilerSha256=sha(compiler), parentSha256=sha(parent))
    stamp = output/'inputs.json'
    if stamp.exists(): assert json.loads(stamp.read_text()) == identity
    else: stamp.write_text(json.dumps(identity, indent=2)+'\n')
    jobs = []
    for latest in sorted(before.glob('projects/g*/mx251/.build/mx251/latest')):
        result = json.loads((latest.parents[2]/'result.json').read_text())
        if result['status'] != 'BUILT': continue
        commands = json.loads((latest/'commands.json').read_text())
        final = {}
        for cmd in commands:
            args = cmd['arguments']
            if Path(cmd['executable']).name == 'sdcc.exe' and '-c' in args and '-o' in args:
                final[Path(args[args.index('-o')+1]).stem] = args
        for unit, args in final.items(): jobs.append((latest, unit, args))

    def one(job):
        latest, unit, args = job
        project = latest.parents[3].name
        row = dict(id=project, unit=unit, arguments=args)
        for name, exe in [('before', parent), ('after', compiler)]:
            work = output/project/unit/name
            work.mkdir(parents=True, exist_ok=True)
            if not (work/(unit+'.rel')).exists():
                shutil.copytree(latest/'src', work/'src', dirs_exist_ok=True)
                for pattern in ('*.txt', '*.c', '*.h'):
                    for f in latest.glob(pattern): shutil.copy2(f, work/f.name)
                proc = subprocess.run([str(exe), *args], cwd=work, env=env, capture_output=True)
                (work/'compiler.log').write_bytes(proc.stdout+proc.stderr)
                assert proc.returncode == 0, (project, unit, name, proc.stderr[-2000:])
            row[name] = dict(relSha256=sha(work/(unit+'.rel')), asmSha256=sha(work/(unit+'.asm')))
        row['changed'] = row['before']['relSha256'] != row['after']['relSha256']
        return row

    with ThreadPoolExecutor(max_workers=4) as pool: rows = list(pool.map(one, jobs))
    result = dict(compilerSha256=sha(compiler), parentCompilerSha256=sha(parent), rows=rows,
                  units=len(rows), projects=len({r['id'] for r in rows}),
                  affectedProjects=sorted({r['id'] for r in rows if r['changed']}))
    (output/'result.json').write_text(json.dumps(result, indent=2)+'\n', newline='\n')
    print(json.dumps({k:v for k,v in result.items() if k!='rows'}), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('before', 'compiler', 'output'): parser.add_argument('--'+key, type=Path, required=True)
    args = parser.parse_args()
    run(args.before.resolve(), args.compiler.resolve(), args.output.resolve())
