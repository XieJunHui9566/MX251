"""Compile the CY dependency for both backends; compare unrelated cases unchanged."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

CONTROL = '''typedef unsigned char u8;
__sbit __at(0xd7) carry_alias;
__sbit __at(0xb4) peripheral;
void barrier(void);
u8 plain(u8 v) { v <<= 1; return v; }
u8 different_bit(u8 v) { v <<= 1; return peripheral; }
u8 intervening_call(u8 v) { v <<= 1; barrier(); return carry_alias; }
u8 explicit_overwrite(u8 v) { v <<= 1; carry_alias=0; return carry_alias; }
u8 count_two(u8 v) { v <<= 2; return carry_alias; }
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for key in ('sdcc', 'baseline', 'support', 'output'):
        ap.add_argument('--'+key, type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'MCS251_', 'SDCC_'))}
    env['SDCC_HOME'] = str(args.support.resolve())
    env['PATH'] = str(args.support.resolve()/'bin')+os.pathsep+env['PATH']
    source = Path(__file__).with_name('carry-observation.c').read_text()
    rows = []
    for model in ('small', 'large'):
        for objective in ('speed', 'size'):
            for backend in ('legacy', 'wide'):
                directory = args.output/f'{model}-{objective}-{backend}'
                directory.mkdir()
                (directory/'probe.c').write_text(source)
                (directory/'control.c').write_text(CONTROL)
                switches = ['-mmcs251', '--model-'+model, '--opt-code-'+objective,
                            '--stack-auto', '--mcs251-machine-ir', '--mcs251-function-metadata']
                if backend == 'wide':
                    switches += ['--mcs251-wide-ordinary']
                    env['MX251_BODY_BACKEND_CHOICE'] = '0'
                    env['MX251_LINEAR_BACKEND_CHOICE'] = '0'
                else:
                    env.pop('MX251_BODY_BACKEND_CHOICE', None)
                    env.pop('MX251_LINEAR_BACKEND_CHOICE', None)
                commands = []
                for label, cc, file in [('probe', args.sdcc, 'probe.c'),
                                        ('parent-control', args.baseline, 'control.c'),
                                        ('new-control', args.sdcc, 'control.c')]:
                    command = [str(cc.resolve()), *switches, '--dump-i-code', '-c', file, '-o', label+'.rel']
                    proc = subprocess.run(command, cwd=directory, env=env, capture_output=True, timeout=60)
                    (directory/(label+'.log')).write_bytes(proc.stdout+proc.stderr)
                    assert proc.returncode == 0, (directory, proc.stderr)
                    commands.append(command)
                raw = (directory/'probe.dumpraw0').read_text()
                # Seven source sites bind the outgoing bit to CY before generic DCE.
                assert len(re.findall(r'\t_ArithmeticCarry[^\n]*:= iTemp', raw)) == 7, directory
                asm = (directory/'probe.asm').read_text()
                packet = asm.split('symbol=_packet isr=',1)[1].split('; mx251-function end',1)[0]
                assert re.search(r'\tmov\s+r\d+,\s*dpl', packet), packet
                assert '_ArithmeticCarry' in packet
                if backend == 'wide':
                    assert 'wide-mir accepted: packet;' in packet, packet
                def instructions(label):
                    return [x.strip() for x in (directory/(label+'.asm')).read_text().splitlines()
                            if x.startswith('\t')]
                assert instructions('parent-control') == instructions('new-control'), directory
                rows.append(dict(model=model, objective=objective, backend=backend,
                                 observedShiftSites=7, unchangedControls=5, commands=commands))
    report = dict(status='PASS', rows=rows, compilerSha256=hashlib.sha256(args.sdcc.read_bytes()).hexdigest(),
                  semantics='Compilation and unchanged controls only; execute carry_observation_fixture for numerical results.')
    (args.output/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print('PASS: 8 configurations, 7 observed shifts and 5 unchanged controls per configuration')


if __name__ == '__main__':
    main()
