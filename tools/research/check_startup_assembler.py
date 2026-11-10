"""Compare the actual empty-CRT templates against licensed Keil A251 encoding."""
from pathlib import Path
import argparse
import json
import re
import subprocess

from unified_sdk_matrix import pin, save


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--keil', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assembler = args.core/'toolchain/bin/sdas251.exe'
    templates = [args.core/'build/qualified/startup-templates'/name for name in ('crtxinit.asm', 'crtxclear.asm')]
    rows = []
    for value in (0, 0xffff, 0x10000, 0xff1234, 0xffffff):
        out = args.output/f'{value:06x}'
        out.mkdir(parents=True, exist_ok=False)
        lines = []
        for template in templates:
            for line in template.read_text().splitlines():
                if not re.match(r'^(mov|cmp)\s', line):
                    continue
                line = re.sub(r'#\(s_\w+ >> 16\)', f'#0x{value>>16:02x}', line)
                lines.append(re.sub(r'#s_\w+', f'#0x{value&65535:04x}', line))
        mx = '.area TEST (ABS,CODE)\n.org 0xff0000\n'+'\n'.join(lines)+'\n'
        keil = 'DPXL DATA 084H\nCSEG AT 0\n'+re.sub(r'0x([0-9a-f]+)', lambda m: '0'+m[1]+'H', '\n'.join(lines))+'\nEND\n'
        (out/'mx.asm').write_text(mx, encoding='ascii')
        (out/'keil.a51').write_text(keil, encoding='ascii')
        for label, command in [('mx', [assembler, '-plosgff', 'mx.rel', 'mx.asm']),
                               ('keil', [args.keil, 'keil.a51', 'MODSRC', 'OBJECT(keil.obj)', 'PRINT(keil.lst)'])]:
            process = subprocess.run(list(map(str, command)), cwd=out, capture_output=True)
            (out/(label+'.log')).write_bytes(process.stdout+process.stderr)
            assert process.returncode == 0, (label, process.returncode)
        kb = b''.join(bytes.fromhex(m[1]) for m in re.finditer(r'^\w{6} ([0-9A-F]+)\s+\d+\s', (out/'keil.lst').read_text('latin1'), re.M))
        mb = b''.join(bytes.fromhex(m[1]) for m in re.finditer(r'^\s+[0-9A-F]{8} ((?:[0-9A-F]{2} )+)\s+\[', (out/'mx.lst').read_text('latin1'), re.M))
        assert kb == mb and len(kb) == 36, (value, kb.hex(), mb.hex())
        rows.append(dict(address=value, instructionCount=len(lines), bytes=kb.hex(), equal=True))
    save(args.output/'result.json', dict(status='PASS', mxAssembler=pin(assembler), keilAssembler=pin(args.keil),
                                      templates=[pin(p) for p in templates], rows=rows,
                                      instructions=sum(r['instructionCount'] for r in rows)))
    print(json.dumps(dict(status='PASS', cases=len(rows), instructions=sum(r['instructionCount'] for r in rows))))


if __name__ == '__main__':
    main()
