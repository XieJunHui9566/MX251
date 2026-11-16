"""Compare a reused shared-library trial with a fresh build of the same choice."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from unified_sdk_matrix import pin, read, save

p=argparse.ArgumentParser(description=__doc__)
for name in ('candidate','host','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
root=a.output/'project';root.mkdir()
(root/'main.c').write_text('#include <stdio.h>\nextern unsigned int value(unsigned int);\nvolatile unsigned int input;\nvoid main(void){printf("status\\n");printf("%u\\n",value(input));while(1){}}\n',newline='\n')
(root/'helper.c').write_text('unsigned int value(unsigned int x){return (x*7u)^0x1234u;}\n',newline='\n')
(root/'output.c').write_text('volatile unsigned char output;\nint putchar(int c){output=(unsigned char)c;return c;}\n',newline='\n')
save(root/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[]))
(root/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(compile_reuse NONE)\ninclude("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE main.c helper.c output.c)\nmx251_firmware(app)\n',newline='\n')
pwsh=a.host/'powershell/pwsh.exe'
def run(name,cmd,env=None):
    with (a.output/(name+'.log')).open('wb') as f:
        q=subprocess.run(list(map(str,cmd)),env=env,stdout=f,stderr=subprocess.STDOUT)
    assert q.returncode==0,(name,a.output/(name+'.log'))
run('public',[pwsh,'-NoProfile','-File',a.candidate/'Build.ps1','-ProjectPath',root,
              '-CMake',a.host/'cmake/bin/cmake.exe','-Ninja',a.host/'ninja/ninja.exe','-PowerShell',pwsh])
latest=root/'.build/mx251/latest';choice=read(latest/'stdio-sharing.json')
assert choice['status']=='SHARED_PRINTF' and choice['reusedInvocations']>0,choice
args=choice['arguments'];args[args.index('-OutputDirectory')+1]=str(a.output/'fresh')
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('MX251_','SDCC_'))}
env['MX251_STDIO_PRINTF_UNITS']=','.join(choice['units'])
run('fresh',[pwsh,*args],env)
selected=pin(latest/'firmware.hex');fresh=pin(a.output/'fresh/firmware.hex')
assert selected['sha256']==fresh['sha256'],(selected,fresh)
save(a.output/'result.json',dict(status='PASS',selected=selected,fresh=fresh,
     reusedInvocations=choice['reusedInvocations'],savedBytes=choice['savedBytes'],
     compilerSha256=read(latest/'receipt.json')['compilerSha256'],
     note='Fresh trial starts with an empty invocation cache; complete final HEX is identical.'))
print('PASS: reused and fresh final HEX identical;',choice['reusedInvocations'],'reused invocations')
