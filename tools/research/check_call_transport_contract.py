"""Compile actual multi-TU fixtures under V2/V3 internal ABI contracts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    commands = json.loads((args.baseline / 'commands.json').read_text())
    compiler = Path(commands[0]['executable'])
    tool_bin = compiler.parents[3] / 'toolchain/bin'
    definitions = {}
    checks = []

    def compile_case(name, contract, fail=False):
        root = args.output / name
        root.mkdir()
        for source in args.baseline.iterdir():
            if source.suffix in ('.c', '.h'):
                shutil.copy2(source, root / source.name)
        path = root / 'contract.txt'
        path.write_text(contract, encoding='ascii', newline='\n')
        env = os.environ.copy()
        env['PATH'] = str(tool_bin) + os.pathsep + env['PATH']
        env['MX251_INTERNAL_ABI4_CONTRACT'] = str(path.resolve())
        assembly = {}
        for unit in ('u1', 'u2'):
            command = next(c for c in commands if c['label'] == unit)
            argv = ['-S' if a == '-c' else a for a in command['arguments']]
            argv[argv.index('-o') + 1] = unit + '.asm'
            process = subprocess.run([command['executable'], *argv], cwd=root,
                                     env=env, capture_output=True, text=True)
            (root / (unit + '.log')).write_text(process.stdout + process.stderr)
            if fail:
                assert process.returncode != 0 and 'transport signature mismatch' in process.stderr, process.stderr
                checks.append(dict(case=name, status='PASS'))
                return
            assert process.returncode == 0, process.stderr
            assembly[unit] = (root / (unit + '.asm')).read_text()
        definitions[name] = assembly
        checks.append(dict(case=name, status='PASS'))

    compile_case('v2-global', 'MX251-INTERNAL-ABI4-V2\n_transport_put\tvoid(pG(u8),u8)\n')
    compile_case('v3-global', 'MX251-INTERNAL-ABI4-V3\n*\t_transport_put\tvoid(pG(u8),u8)\n')
    for unit in ('u1', 'u2'):
        def instructions(text):
            return '\n'.join(line.split(';')[0].strip() for line in text.splitlines()
                             if line.split(';')[0].strip())
        assert instructions(definitions['v2-global'][unit]) == instructions(definitions['v3-global'][unit])
    assert 'symbol=___m251r4_transport_put abi=4 ' in definitions['v3-global']['u2']
    assert 'symbol=_take abi=2 ' in definitions['v3-global']['u2']
    compile_case('v3-local', 'MX251-INTERNAL-ABI4-V3\ncall-transport-left.c\t_take\tu8(pG(cu8),u8)\n')
    assert 'symbol=___m251r4_take abi=4 ' in definitions['v3-local']['u1']
    assert 'symbol=_take abi=2 ' in definitions['v3-local']['u2']
    compile_case('signature-mismatch', 'MX251-INTERNAL-ABI4-V3\ncall-transport-left.c\t_take\tu16(pG(cu8),u8)\n', fail=True)
    result = dict(status='PASS', checks=checks, v2V3MachineInstructionsIdentical=True,
                  compilerSha256=hashlib.sha256(compiler.read_bytes()).hexdigest())
    (args.output / 'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
