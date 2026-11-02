#!/usr/bin/env python3
"""Keep asynchronous DPXL/wide-register preservation across codegen options.

This is a compile/assemble gate. Real preemption, stack locals and nested
interrupts require the separate AI8051U board regression, not this inspection.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sdcc', required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--output-dir')
    args = parser.parse_args()
    root = Path(args.output_dir or tempfile.mkdtemp(prefix='mcs251-irq-')).resolve()
    root.mkdir(exist_ok=True, parents=True)
    results = []
    for model in ['small', 'large']:
        for auto in [False, True]:
            for optimize in ['speed', 'size']:
                for nopeep in [False, True]:
                    name = f'{model}-auto{int(auto)}-{optimize}-nopeep{int(nopeep)}'
                    target = root / name
                    target.mkdir(exist_ok=False)
                    command = [str(Path(args.sdcc).resolve()), '-mmcs251',
                               '--model-' + model, '--opt-code-' + optimize]
                    if auto:
                        command.append('--stack-auto')
                    if nopeep:
                        command.append('--no-peep')
                    command += ['-c', str(Path(args.source).resolve()), '-o', 'test.rel']
                    run = subprocess.run(command, cwd=target, capture_output=True)
                    (target/'compile.log').write_bytes(run.stdout + run.stderr)
                    assert run.returncode == 0, (name, run.stderr)
                    assembly = (target/'test.asm').read_text()
                    for function in ['irq_leaf', 'irq_calls', 'irq_bank', 'irq_naked']:
                        body = re.search(r'^_' + function + r':\n(.*?)(?=^;-{20,}$|\Z)',
                                         assembly, re.M | re.S).group(1)
                        assert re.search(r'^\s*reti\s*$', body, re.M), (name, function)
                        if function == 'irq_naked':
                            assert not re.search(r'^\s*(push|pop)\s', body, re.M)
                        else:
                            # Body spills/stack arguments may also push DPXL;
                            # inspect only the asynchronous entry/exit frame.
                            prologue = body.split('\tmov\tpsw,', 1)[0]
                            epilogue = body.rsplit('\tpop\tpsw', 1)[1]
                            for reg in ['dpxl', 'dr16', 'dr20', 'dr24', 'dr28']:
                                pushed = len(re.findall(r'^\s*push\s+'+reg+r'\s*$', prologue, re.M))
                                popped = len(re.findall(r'^\s*pop\s+'+reg+r'\s*$', epilogue, re.M))
                                assert pushed == 1 and popped == 1, (name, function, reg, pushed, popped)
                            saves = [body.index('\tpush\t'+r) for r in ['dpxl','dr16','dr20','dr24','dr28']]
                            restores = [body.rindex('\tpop\t'+r) for r in ['dr28','dr24','dr20','dr16','dpxl']]
                            assert saves == sorted(saves) and restores == sorted(restores)
                        results.append(dict(configuration=name, function=function, passed=True))
    (root/'results.json').write_text(json.dumps(results, indent=2))
    print(f'PASS: {len(results)} interrupt codegen checks; artifacts: {root}')


if __name__ == '__main__':
    main()
