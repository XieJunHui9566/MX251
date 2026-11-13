"""Whole-object argument reads, with unchanged volatile sampling and ABI transport."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

SOURCE = r'''
typedef unsigned int u16;
typedef unsigned long u32;
__data u16 word;
__data u32 dword;
__data signed long signed_dword;
__data float real;
extern __data u32 external;
volatile __data u16 device_word;
volatile __data u32 device_dword;
__sfr __at(0x99) port;
u32 take_word(u16);
u32 take_dword(u32);
u32 take_signed(signed long);
u32 take_float(float);
u32 take_stack(u16,u32,signed long,float);
u32 take_native(u32,u16,u32) __sdcccall(4);
u32 take_variadic(u16,...);
void mutate(void);
u32 normal_word(void) __sdcccall(4) { return take_word(word); }
u32 normal_dword(void) __sdcccall(4) { return take_dword(dword); }
u32 normal_signed(void) __sdcccall(4) { return take_signed(signed_dword); }
u32 normal_float(void) __sdcccall(4) { return take_float(real); }
u32 normal_external(void) __sdcccall(4) { return take_dword(external); }
u32 normal_stack(void) __sdcccall(4) { return take_stack(word,dword,signed_dword,real); }
u32 normal_native(void) __sdcccall(4) { return take_native(dword,word,external); }
u32 normal_variadic(void) __sdcccall(4) { return take_variadic(3,word,dword,real); }
u32 repeated(void) __sdcccall(4) {
    u32 before=take_dword(dword);
    mutate();
    return before^take_dword(dword);
}
u32 volatile_word(void) __sdcccall(4) { return take_word(device_word); }
u32 volatile_dword(void) __sdcccall(4) { return take_dword(device_dword); }
u32 volatile_stack(void) __sdcccall(4) { return take_stack(0,device_dword,0,0); }
u32 sfr_argument(void) __sdcccall(4) { return take_word(port); }
'''


def body(asm, name):
    return re.split(r'; mx251-function begin schema=1 symbol=(?:_|___m251r4_)'+name+' ', asm, maxsplit=1)[1].split('; mx251-function end', 1)[0]


def instructions(text):
    return [line for line in text.splitlines() if line.startswith('\t')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sdcc', 'baseline', 'support', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('MX251_', 'MCS251_', 'SDCC_'))}
    env['SDCC_HOME'] = str(args.support.resolve())
    env['PATH'] = str(args.support.resolve()/'bin')+';'+env['PATH']
    rows = []
    for model, opt, data_wide in [('small', 'speed', False), ('large', 'size', False),
                                  ('small', 'speed', True)]:
        directory = args.output/(model+'-'+opt+('-data-wide' if data_wide else ''))
        directory.mkdir()
        (directory/'probe.c').write_text(SOURCE)
        results = {}
        for tag, cc in [('parent', args.baseline), ('candidate', args.sdcc)]:
            command = [str(cc.resolve()), '-mmcs251', '--model-'+model, '--opt-code-'+opt,
                       '--stack-auto', '--mcs251-wide-ordinary', '--mcs251-native-abi',
                       '--mcs251-machine-ir', '--mcs251-function-metadata']
            if data_wide:
                command.append('--mcs251-volatile-data-wide')
            command += ['-c', 'probe.c', '-o', tag+'.rel']
            proc = subprocess.run(command, cwd=directory, env=env, capture_output=True, timeout=60)
            (directory/(tag+'.log')).write_bytes(proc.stdout+proc.stderr)
            assert proc.returncode == 0, proc.stderr
            results[tag] = (directory/(tag+'.asm')).read_text()
        candidate = results['candidate']
        for name, symbol, prefix in [('word', 'word', 'wr'), ('dword', 'dword', 'dr'),
                                      ('signed', 'signed_dword', 'dr'), ('float', 'real', 'dr'),
                                      ('external', 'external', 'dr')]:
            text = body(candidate, 'normal_'+name)
            assert 'wide-mir accepted: normal_'+name+';' in text, name
            assert re.search(r'\tmov\t'+prefix+r'\d+,\(_'+symbol+r'\+0\)', text), text
            assert not re.search(r'\tmov\t(?:r\d+|a|b),\(_'+symbol+r'\+', text), text
        for name in ('stack', 'native', 'variadic'):
            text = body(candidate, 'normal_'+name)
            assert 'wide-mir accepted: normal_'+name+';' in text, name
            assert re.search(r'\tmov\tdr\d+,\(_(?:dword|external)\+0\)', text), text
        text = body(candidate, 'repeated')
        loads = [m.start() for m in re.finditer(r'\tmov\tdr\d+,\(_dword\+0\)', text)]
        assert len(loads) == 2 and loads[0] < text.index('\tecall\t_mutate') < loads[1], text
        for name in ('volatile_word', 'volatile_dword', 'volatile_stack', 'sfr_argument'):
            assert instructions(body(candidate, name)) == instructions(body(results['parent'], name)), name
        for name, symbol, offsets in [('volatile_word', 'device_word', [1, 0]),
                                       ('volatile_dword', 'device_dword', [3, 2, 1, 0]),
                                       ('volatile_stack', 'device_dword', [0, 1, 2, 3])]:
            assert [int(x) for x in re.findall(r'\(_'+symbol+r'\+(\d+)\)', body(candidate, name))] == offsets
        sizes = {tag: int(re.search(r'^A CSEG size ([0-9A-Fa-f]+)',
                 (directory/(tag+'.rel')).read_text(), re.M)[1], 16) for tag in results}
        assert sizes['candidate'] < sizes['parent'], sizes
        rows.append(dict(model=model, objective=opt, volatileDataWide=data_wide, sizes=sizes,
                         normalFunctions=9, volatileAndSfrFunctionsUnchanged=4))
    (args.output/'result.json').write_text(json.dumps(dict(status='PASS', rows=rows,
        compilerSha256=hashlib.sha256(args.sdcc.read_bytes()).hexdigest()), indent=2)+'\n')
    print(json.dumps(rows, indent=2))


if __name__ == '__main__':
    main()
