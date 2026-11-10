"""SFR bit consumers: all positions, adjacent uses and ISR bank selection."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for key in ('sdcc', 'toolchain', 'output'):
    p.add_argument('--'+key, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
source = '''typedef unsigned char u8;
__sfr __at(0x89) src;
__sbit __at(0x90) sink;
volatile __data u8 count, memory;
'''
for bit in range(8):
    source += f'''void assign_{bit}(void) {{ sink = (src >> {bit}) & 1; }}
void branch_{bit}(void) {{ if (src & {1 << bit}) ++count; }}
void inverted_{bit}(void) {{ if (!((src >> {bit}) & 1)) ++count; }}
'''
source += '''
void snapshot(void) { u8 v = src; src = 0; sink = (v >> 3) & 1; }
void reused(void) { u8 v = src & 8; memory = v; sink = v; }
void ordinary_ram(void) { sink = (memory >> 3) & 1; }
void multi_mask(void) { if (src & 9) ++count; }
void irq_bit(void) __interrupt(1) { sink = (src >> 7) & 1; }
void irq_branch(void) __interrupt(3) { if (src & 1) sink = 1; else sink = 0; }
void irq_low_regs(void) __interrupt(5) { u8 i; for (i=0;i<count;++i) memory += i; }
'''
env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_', 'SDCC_'))}
env['SDCC_HOME'] = str(a.toolchain.resolve())
env['PATH'] = str(a.toolchain.resolve()/'bin') + os.pathsep + env['PATH']
rows = []
for model in ('small', 'large'):
    for opt in ('speed', 'size'):
        out = a.output/(model+'-'+opt)
        out.mkdir(); (out/'cases.c').write_text(source)
        variants = {}
        for variant in ('enabled', 'disabled'):
            runenv = dict(env, MX251_SFR_BIT_TESTS='0' if variant=='disabled' else '1')
            command = [str(a.sdcc.resolve()), '-mmcs251', '--model-'+model,
                       '--opt-code-'+opt, '--stack-auto', '--mcs251-function-metadata',
                       '-c', 'cases.c', '-o', variant+'.rel']
            result = subprocess.run(command, cwd=out, env=runenv, capture_output=True)
            (out/(variant+'.log')).write_bytes(result.stdout+result.stderr)
            assert result.returncode == 0, result.stderr
            text = (out/(variant+'.asm')).read_text()
            variants[variant] = dict(re.findall(
                r'; mx251-function begin[^\n]*symbol=(\S+)[^\n]*\n(.*?); mx251-function end', text, re.S))
        for prefix in ('assign', 'branch', 'inverted'):
            for bit in range(8):
                text = variants['enabled']['_'+prefix+'_'+str(bit)]
                assert 'direct SFR bit test address=0x89.'+str(bit) in text, (model,opt,prefix,bit)
        for name in ('snapshot', 'reused', 'ordinary_ram', 'multi_mask'):
            assert 'direct SFR bit test' not in variants['enabled']['_'+name], name
        for text in variants['disabled'].values():
            assert 'direct SFR bit test' not in text
        irq = variants['enabled']['_irq_bit']
        assert 'mov\tc,0x89.7' in irq and 'push\tpsw' in irq
        assert not re.search(r'^\s*(?:mov|anl)\s+psw,|^\s*push\s+acc', irq, re.M)
        assert re.search(r'^\s*(?:mov|anl)\s+psw,', variants['enabled']['_irq_low_regs'], re.M)
        rows.append(dict(model=model, optimization=opt, bitPositions=8,
                         consumers=['bit assignment','true branch','false branch'],
                         snapshotAndOtherUsesRetained=True, flagsRetainedWithoutBankSelection=True))
(a.output/'result.json').write_text(json.dumps(dict(status='PASS',configurations=rows),indent=2)+'\n')
print('PASS: 4 configurations x 2 variants; 8 bit positions, snapshots and ISR bank selection')
