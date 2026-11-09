"""Execute original and optimized CFGs with an independent instruction model."""
import argparse
import importlib.util
import json
from pathlib import Path
import random
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('execution_model', Path(__file__).with_name('check-function-sharing.py'))
    model = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(model)
    reference = importlib.util.spec_from_file_location('machine_model', Path(__file__).with_name('machine-ir-model.py'))
    machine = importlib.util.module_from_spec(reference); reference.loader.exec_module(machine)
    programs = {
        'typed-memory': '_test:\n mov r6,#0\n mov a,_data\n mov r6,#0\n mov _data,a\n eret\n',
        'typed-call': '_test:\n mov wr6,wr2\n mov wr4,wr12\n ecall _known\n mov wr12,wr6\n eret\n',
        'cjne-fold': '_test:\n mov r6,#0\n cjne r6,#0,L\n mov r7,#3\nL:\n cmp r0,#1\n eret\n',
        'cjne-flags-live': '_test:\n mov r6,#0\n cjne r6,#0,L\n jne L\n mov r7,#3\nL:\n eret\n',
        'cjne-carry': '_test:\n mov r6,#0\n cjne r6,#0,L\n jc L\n mov r7,#3\nL:\n cmp r0,#1\n eret\n',
        'coalesce': '_test:\n mov wr16,wr4\n add wr16,wr2\n mov wr6,wr16\n mov wr16,#0\n eret\n',
        'overlap': '_test:\n mov dr0,dr4\n mov wr6,wr2\n mov r1,r7\n mov dr4,dr0\n eret\n',
        'loop': '_test:\n mov wr0,wr4\n mov r15,#0\n__wm_loop:\n add wr0,#3\n mov wr6,wr0\n inc r15,#1\n cmp r15,#5\n jc __wm_loop\n mov wr4,wr6\n eret\n',
        'join': '_test:\n cmp wr0,#12345\n jc __wm_left\n mov wr4,wr2\n sjmp __wm_join\n__wm_left:\n mov wr4,wr6\n__wm_join:\n mov wr16,wr4\n add wr16,wr0\n mov wr2,wr16\n mov wr16,#0\n eret\n',
        'same-join': '_test:\n cmp wr0,#12345\n jc __wm_left\n mov wr4,wr2\n sjmp __wm_join\n__wm_left:\n mov wr4,wr2\n__wm_join:\n mov wr6,wr4\n eret\n',
        'stack': '_test:\n push wr4\n mov wr16,wr0\n add wr16,wr2\n mov wr4,wr16\n mov wr16,#0\n pop wr0\n mov @spx-7,wr4\n eret\n',
    }
    rng = random.Random(251)
    for op in ('mul', 'div'):
        for first in range(32):
            if first < 16:
                programs[f'{op}-byte-{first}'] = f'_test:\n mov wr20,wr6\n mov r14,#17\n {op} r{first},r14\n mov wr24,wr20\n eret\n'
            if first % 2 == 0:
                programs[f'{op}-word-{first}'] = f'_test:\n mov wr20,wr6\n mov wr12,#37\n {op} wr{first},wr12\n mov wr24,wr20\n eret\n'
        programs[f'{op}-ab'] = f'_test:\n mov wr6,wr0\n mov b,#3\n {op} ab\n mov wr4,wr6\n eret\n'
        programs[f'{op}-implicit-half'] = f'_test:\n mov wr6,wr0\n mov wr12,#37\n {op} wr4,wr12\n mov wr16,wr6\n mov wr6,#0\n eret\n'
    registers = {1: ['r'+str(r) for r in [0,1,2,3,4,5,6,7,12,13]],
                 2: ['wr'+str(r) for r in [0,2,4,6,12,16,18,20,22,24,26,28,30]],
                 4: ['dr'+str(r) for r in [0,4,16,20,24,28]]}
    for number in range(96):
        lines = ['_test:', ' mov r15,#0', '__wm_loop:']
        for k in range(32):
            width = rng.choice([1,2,4])
            d, s = rng.choices(registers[width], k=2)
            op = rng.choice(['mov','mov','add','sub','anl','orl','xrl'])
            if rng.randrange(3) == 0:
                s = '#' + str(rng.randrange(1 << min(16, 8*width)))
            lines.append(f' {op} {d},{s}')
            if k in [9,19]:
                lines.extend([' cmp wr0,#32768', f' jc __wm_join{k}', ' mov wr4,wr2', f'__wm_join{k}:'])
        lines.extend([' inc r15,#1',' cmp r15,#3',' jc __wm_loop',' eret'])
        programs[f'generated-{number}'] = '\n'.join(lines)+'\n'
    for i in range(3):
        programs[f'render-template-{i}'] = model.function(i)
    executions = 0
    changes = 0
    for name, original in programs.items():
        source = args.output / (name+'.asm')
        source.write_bytes(original.encode())
        candidate = subprocess.run([str(args.probe),str(source)],capture_output=True,check=True,timeout=15).stdout.decode().replace('\r\n','\n')
        (args.output / (name+'-optimized.asm')).write_bytes(candidate.encode())
        changes += original.splitlines() != candidate.splitlines()[:-1]
        entry = '_sample'+name[-1] if name.startswith('render-template') else '_test'
        for seed in range(64):
            before = machine.execute(original,entry,seed)
            after = machine.execute(candidate,entry,seed)
            assert before == after,(name,seed)
            executions += 1
    assert 'cjne' not in (args.output/'cjne-fold-optimized.asm').read_text()
    assert 'cjne' in (args.output/'cjne-flags-live-optimized.asm').read_text()
    assert 'mx251-machine-call' in (args.output/'typed-call-optimized.asm').read_text()
    # A caller-visible value and a fixed PC displacement must remain observable.
    for name, text in {
        'opaque-call':'_test:\n mov wr4,wr16\n ecall _opaque\n eret\n',
        'relative-pc':'_test:\n mov wr4,wr4\n sjmp $+2\n eret\n',
        'bank-change':'_test:\n mov psw,#8\n mov wr4,wr4\n eret\n',
        'indirect-jump':'_test:\n mov wr4,wr4\n jmp @a+dptr\n',
    }.items():
        p=args.output/(name+'.asm');p.write_bytes(text.encode())
        result=subprocess.run([str(args.probe),str(p)],capture_output=True,check=True).stdout.decode()
        assert 'mov wr4,wr16' in result if name=='opaque-call' else result.replace('\r\n','\n')==text,name
    report={'status':'PASS','programs':len(programs),'executions':executions,'transformedPrograms':changes,'boundaries':4}
    (args.output/'result.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
    print(json.dumps(report))


if __name__=='__main__':
    main()
