"""Check final branch boundaries, CFG semantics and real assembler encodings."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--image', type=Path, action='append', default=[])
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--toolchain', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    tests = Path(__file__).resolve().parent
    probe = args.output / 'probe.exe'
    subprocess.run([str(args.cc), '-std=c17', '-O2', '-I'+str(tests.parent),
                    str(tests/'probe-machine-branch.c'), '-o', str(probe)], check=True)
    boundary = subprocess.check_output([str(probe)], text=True).strip()
    spec = importlib.util.spec_from_file_location('model', tests/'machine-ir-model.py')
    model = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(model)
    programs = {
        'diamond': '_test:\n cmp r0,#128\n jc skip\n ejmp other\nskip:\n mov r4,#7\n ejmp done\nother:\n mov r4,#9\ndone:\n eret\n',
        'loop': '_test:\n mov r15,#0\nloop:\n inc r15\n cmp r15,#8\n je done\n ejmp loop\ndone:\n eret\n',
        'cascade': '_test:\n cmp r0,#128\n jc skip\n ejmp target\nskip:\n'+
                   ' nop\n'*122+' ejmp tail\ntail:\n nop\n nop\n nop\ntarget:\n eret\n',
        'label-entry': '_test:\n cmp r0,#128\n jc enter\n jne skip\nenter:\n ejmp target\nskip:\n mov r4,#7\ntarget:\n eret\n',
        'memory': '_test:\n cmp r0,#128\n jne skip\n ejmp target\nskip:\n mov _data,a\n mov a,_data\ntarget:\n eret\n',
    }
    executions = 0
    for name, source in programs.items():
        path = args.output/(name+'.asm'); path.write_bytes(source.encode())
        result = subprocess.check_output([str(probe), str(path)]).decode().replace('\r\n', '\n')
        (args.output/(name+'-optimized.asm')).write_bytes(result.encode())
        if name != 'memory':
            assert 'mx251-final-branches' in result, name
        for seed in range(256):
            assert model.execute(source, '_test', seed) == model.execute(result, '_test', seed), (name, seed)
            executions += 1
    untouched = {
        'unknown': '_test:\n ejmp target\n .ds 200\ntarget:\n eret\n',
        'external': '_test:\n ejmp _other_function\n eret\n',
        'table': '_test:\n ejmp @dr16\n ejmp target\ntarget:\n eret\n',
        'relative': '_test:\n ejmp target\n sjmp $+6\ntarget:\n eret\n',
        'scoped-numeric-labels': '_test:\n ejmp 00100$\n00100$:\n eret\nhelper:\n00100$:\n eret\n',
    }
    for name, source in untouched.items():
        path = args.output/(name+'.asm'); path.write_bytes(source.encode())
        result = subprocess.check_output([str(probe), str(path)]).decode().replace('\r\n', '\n')
        assert result == source, name
    sys.path.insert(0, str(tests.parents[5]/'tools/research'))
    import call_layout
    images = []
    for root in args.image:
        memory = call_layout.ihex(root/'firmware.hex'); checked = 0; branches = 0
        for asm in root.glob('u[0-9]*.asm'):
            if not re.fullmatch(r'u[0-9]+', asm.stem): continue
            unit = call_layout.read_unit(root, asm.stem, memory)
            # ASxxxx numeric labels are local to a function/global-label scope.
            # The same spelling can occur at several addresses in one unit.
            owner = None; owners = {}; scoped_labels = {}
            for number, line in enumerate(unit['lines'], 1):
                match = re.search(r'mx251-function begin schema=1 symbol=(\S+)', line)
                if match: owner = match[1]
                owners[number] = owner
                if 'mx251-function end schema=1' in line: owner = None
            for line in asm.with_suffix('.rst').read_text(encoding='latin1').splitlines():
                match = re.fullmatch(r'\s*(\d+)\s+([\w$]+):{1,2}\s*', line[34:])
                if match and re.fullmatch(r'[0-9A-Fa-f]{6}', line[6:12]):
                    scoped_labels[owners[int(match[1])], match[2]] = int(line[6:12], 16)
            costs = list(map(int, subprocess.check_output([str(probe), str(asm), '--sizes']).split()))
            for ins in unit['instructions']:
                cost = costs[ins.line-1]
                if cost:
                    assert cost == len(ins.data), (root, asm.name, ins.line, ins.source, cost, len(ins.data))
                    checked += 1
                op, *operands = ins.source.split(None, 1)
                if op in ('sjmp', 'jc', 'jnc', 'je', 'jne', 'jz', 'jnz', 'jg', 'jle', 'jsg', 'jsl', 'jsge', 'jsle'):
                    target = scoped_labels.get((ins.owner, operands[0]))
                    if target is None and not operands[0].endswith('$'):
                        target = unit['labels'].get(operands[0])
                    if target is not None:
                        delta = target - ins.address - len(ins.data)
                        assert -128 <= delta <= 127, (root, asm.name, ins.line, delta)
                        assert ins.data[-1] == delta & 255
                        branches += 1
        images.append(dict(path=str(root), knownInstructionEncodings=checked, relativeBranches=branches))
    order_checks = []
    if args.compiler:
        assert args.toolchain
        env = os.environ.copy(); env['SDCC_HOME'] = str(args.toolchain)
        env['PATH'] = str(args.toolchain/'bin') + os.pathsep + env['PATH']
        source = '''unsigned int compute(unsigned int a, unsigned int b)
{
    unsigned int x = a + b;
    if (x & 8) return (x << 3) ^ b;
    return x + a;
}
'''
        variants = {'first': '', 'after-bank-one': 'void previous(void) __interrupt(3) __using(1) {}\n'}
        bodies = []
        for name, prefix in variants.items():
            folder = args.output/name; folder.mkdir()
            (folder/'probe.c').write_text(prefix+source)
            command = [str(args.compiler), '-mmcs251', '--model-small', '--std-sdcc11', '--stack-auto',
                       '--mcs251-wide-ordinary', '--mcs251-machine-ir', '--mcs251-function-metadata',
                       '-S', 'probe.c', '-o', 'probe.asm']
            run = subprocess.run(command, cwd=folder, env=env, capture_output=True)
            (folder/'compile.log').write_bytes(run.stdout+run.stderr)
            assert run.returncode == 0, run.stderr
            asm = (folder/'probe.asm').read_text()
            body = asm.split('; mx251-function begin schema=1 symbol=_compute ', 1)[1]
            body = body.split('; mx251-function end schema=1', 1)[0]
            assert '; mx251-machine-ir ' in body, name
            code = body.split('_compute:\n', 1)[1]
            labels = {}
            normalized = []
            for line in code.splitlines():
                line = line.split(';', 1)[0].strip()
                if not line: continue
                line = re.sub(r'__wm__compute_\d+|\d{5}\$',
                    lambda m: labels.setdefault(m[0], 'L'+str(len(labels))), line)
                normalized.append(re.sub(r'\s+', ' ', line))
            bodies.append(normalized); order_checks.append(name)
            if prefix:
                previous = asm.split('; mx251-function begin schema=1 symbol=_previous ', 1)[1]
                previous = previous.split('; mx251-function end schema=1', 1)[0]
                assert '; mx251-machine-ir ' not in previous
        assert bodies[0] == bodies[1], bodies
    result = dict(status='PASS', boundaries=boundary, semanticExecutions=executions,
                  unchangedCases=len(untouched), images=images, backendOrderChecks=order_checks)
    (args.output/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
