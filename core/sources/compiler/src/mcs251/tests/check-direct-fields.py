"""Native field widths/offsets and unchanged volatile byte transactions."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('sdcc', 'support', 'output'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
tests = Path(__file__).resolve().parent
env = os.environ.copy()
env['SDCC_HOME'] = str(a.support.resolve())
env['PATH'] = str(a.support.resolve() / 'bin') + os.pathsep + env['PATH']
rows = []
for model in ('small', 'large'):
    d = a.output / model
    d.mkdir()
    for name in ('machine-board.h', 'direct-fields.h', 'direct-fields-access.c'):
        shutil.copy2(tests / name, d / name)
    command = [str(a.sdcc.resolve()), '-mmcs251', '--model-' + model, '--stack-auto',
               '--opt-code-speed', '--mcs251-wide-ordinary', '--mcs251-machine-ir',
               '--mcs251-function-metadata', '-c', 'direct-fields-access.c']
    q = subprocess.run(command, cwd=d, env=env, capture_output=True)
    (d / 'compile.log').write_bytes(q.stdout + q.stderr)
    assert q.returncode == 0, q.stderr
    asm = (d / 'direct-fields-access.asm').read_text()
    assert 'wide-mir accepted: df_step;' in asm
    assert 'wide-mir accepted: df_volatile;' in asm
    body = asm.split('_df_step:', 1)[1].split('; mx251-function end', 1)[0]
    for reg, offset in (('wr', 1), ('dr', 3)):
        assert re.search(r'mov\s+' + reg + r'\d+,\(_df_record\+' + str(offset) + r'\)', body)
        assert re.search(r'mov\s+\(_df_record\+' + str(offset) + r'\),' + reg + r'\d+', body)
    assert re.search(r'mov\s+\(_df_words\+2\),wr\d+', body)
    body = asm.split('_df_volatile:', 1)[1].split('; mx251-function end', 1)[0]
    reads = re.findall(r'mov\s+([a-z]+\d*),\(_df_io\+(\d+)\)', body)
    writes = re.findall(r'mov\s+\(_df_io\+(\d+)\),([^\s]+)', body)
    assert [int(offset) for reg, offset in reads] == [3, 4, 5, 6, 1, 2, 8, 9, 0, 7, 10]
    assert [int(offset) for offset, reg in writes] == [0, 7, 10, 1, 2, 3, 4, 5, 6, 8, 9]
    assert not any(reg.startswith(('wr', 'dr')) for reg, offset in reads)
    assert not any(reg.startswith(('wr', 'dr')) for offset, reg in writes)
    assert (d / 'direct-fields-access.rel').is_file()
    rows.append(dict(model=model, command=command, unalignedOffsets=[1, 3],
                     volatileReads=11, volatileWrites=11))
(a.output / 'result.json').write_text(json.dumps(dict(status='PASS', configurations=rows), indent=2) + '\n')
print('PASS: small/large; native word/dword fields and array elements; 22 ordered volatile byte accesses')
