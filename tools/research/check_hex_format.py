"""Compare generated printf/sprintf hex walkers with the host C library."""
import argparse
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('pwsh', 'gcc', 'output'):
    p.add_argument('--'+name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
qualified = Path(__file__).resolve().parents[2]/'core/build/qualified'
groups = [
    ['%02bx'], ['%04X'], ['%08lx'], ['%012lX'], ['%255lX'],
    ['%bx', '%02bX'], ['%x', '%04X', '%08x'],
    ['%lx', '%08lX', '%012lx'], ['%u', '%04x'], ['%lu', '%02X'],
    ['%04x|%02bx|%08lX'], ['%04x|%c'],
]
(a.output/'groups.json').write_text(json.dumps(groups))
quote = lambda path: "'"+str(path).replace("'", "''")+"'"
script = '''$ErrorActionPreference='Stop'
. QUALIFIED
$kernel=[IO.File]::ReadAllText(KERNEL)
$groups=Get-Content GROUPS -Raw|ConvertFrom-Json -NoEnumerate
$rows=@()
foreach($group in $groups){
 $calls=@()
 foreach($format in $group){
  foreach($api in @('printf','sprintf')){
   $calls+=@{api=$api;plan=(Get-Mx251FormatPlan ('<'+$format+'>'))}
  }
 }
 $rows+=@{plans=@($calls|Where-Object{$_.api -eq 'printf'}|ForEach-Object{$_.plan});source=(New-Mx251FormatSource @{calls=$calls;nativeIntegerDivision=$false} $kernel)}
}
ConvertTo-Json -InputObject $rows -Depth 12|Set-Content OUTPUT -Encoding utf8
'''
for key, path in [('QUALIFIED', qualified/'FormatRuntime.ps1'),
                  ('KERNEL', qualified/'IntegerFormatKernel.c.template'),
                  ('GROUPS', a.output/'groups.json'), ('OUTPUT', a.output/'sources.json')]:
    script = script.replace(key, quote(path.resolve()))
(a.output/'generate.ps1').write_text(script)
subprocess.run([str(a.pwsh), '-NoProfile', '-File', str(a.output/'generate.ps1')], check=True)
rows = json.loads((a.output/'sources.json').read_text(encoding='utf-8-sig'))
source = '''#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define __code
static char actual[1024],expected[1024];
static unsigned used,cases;
static int capture(int c) { actual[used++]=(char)c; return c; }
'''
switches = []
for group, row in enumerate(rows):
    body = row['source']
    body = body.replace('mx_printf', f'mx_g{group}_printf').replace('mx_sprintf', f'mx_g{group}_sprintf')
    for api in ('printf', 'sprintf'):
        body = re.sub(r'\b'+api+r'\(', f'g{group}_{api}(', body)
    body = re.sub(r'\bputchar\(', 'capture(', body)
    source += body+'\n'
    (a.output/f'group-{group}.c').write_text(row['source'])
    for plan in row['plans']:
        args, expected = [], []
        for index, token in enumerate(plan['tokens']):
            value = f'(seed*65537u+{index*12345}u)'
            value = f'(uint{token["bits"]}_t){value}'
            ctype = 'unsigned long' if token['bits'] == 32 else 'unsigned int'
            args.append(f'({ctype})({value})')
            expected.append(f'({ctype})'+('(uint8_t)' if token['byteConversion'] else '')+f'({value})')
        fmt = plan['format']
        host = re.sub(r'(%0?\d*)b([xX])', r'\1\2', fmt)
        switches.append(f'''case {len(switches)}:
    want=snprintf(expected,sizeof expected,{json.dumps(host)},{','.join(expected)});
    if(buffer) got=g{group}_sprintf(actual,{json.dumps(fmt)},{','.join(args)});
    else got=g{group}_printf({json.dumps(fmt)},{','.join(args)});
    break;''')
source += '''static int check(unsigned which,uint32_t seed,int buffer)
{
    int want=-1,got=-1;
    memset(actual,0x5a,sizeof actual); used=0;
    switch(which) {
'''+ '\n'.join(switches)+'''
    }
    if(!buffer) actual[used]=0;
    if(want!=got || (!buffer && used!=(unsigned)want) ||
       memcmp(actual,expected,(unsigned)want+1) || actual[want+1]!=0x5a) {
        fprintf(stderr,"case %u seed %u buffer %d: count %d != %d\\n",which,(unsigned)seed,buffer,got,want);
        return 0;
    }
    ++cases; return 1;
}
int main(void)
{
    for(unsigned which=0;which<CASE_COUNT;++which)
        for(unsigned seed=0;seed<65536;++seed)
            for(int buffer=0;buffer<2;++buffer)
                if(!check(which,seed,buffer)) return 1;
    printf("%u\\n",cases); return 0;
}
'''.replace('CASE_COUNT', str(len(switches)))
(a.output/'check.c').write_text(source)
q = subprocess.run([str(a.gcc), '-O2', '-std=c99', 'check.c', '-o', 'check.exe'], cwd=a.output, capture_output=True)
(a.output/'build.log').write_bytes(q.stdout+q.stderr)
assert not q.returncode, q.stderr
q = subprocess.run([str((a.output/'check.exe').resolve())], capture_output=True, text=True)
(a.output/'check.log').write_text(q.stdout+q.stderr)
assert not q.returncode, q.stderr
result = dict(status='PASS', groups=len(rows), formats=len(switches), cases=int(q.stdout),
              apis=['printf', 'sprintf'], hardwareExecuted=False)
(a.output/'result.json').write_text(json.dumps(result, indent=2)+'\n')
print(result)
