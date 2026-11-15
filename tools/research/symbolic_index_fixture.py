"""Build a portable callback/array workload and compare its board trace with GCC."""
import argparse
from pathlib import Path
import shutil

import bitfield_fixture
import machine_pipeline_fixture as board
import user_stress


def create(root,gcc):
    root.mkdir(parents=True,exist_ok=False)
    host,project=root/'host',root/'board';host.mkdir();project.mkdir()
    tests=user_stress.SOURCES.parent
    files=['symbolic-index.h','symbolic-index-access.c','symbolic-index-cases.c']
    for folder in (host,project):
        for name in files:shutil.copy2(tests/name,folder/name)
    (host/'main.c').write_text('#include <stdio.h>\n#include "symbolic-index.h"\n'
        'int main(void) {unsigned k,s;for(k=0;k<8;k++)for(s=0;s<64;s++)'
        'printf("%u:%u %lu\\n",k,s,(unsigned long)stress_run(k,s));return 0;}\n')
    user_stress.run([gcc,'-std=c99','-O2','-Wall','main.c',*files[1:],'-o','reference.exe'],host)
    values={k:int(v) for k,v in (line.split() for line in user_stress.run([host/'reference.exe'],host).splitlines())}
    board.write(root/'reference.json',values)
    main=(tests/'machine-board.c').read_text().replace('"machine-board.h"','"symbolic-index.h"')
    start,end=main.index('        x0 = '),main.index('        TH0 = ')
    main=main[:start]+main[end:]
    start,end=main.index('        if (kind == 0)'),main.index('        TR0 = 0; ticks')
    main=main[:start]+'        result = stress_run(kind,input);\n'+main[end:]
    main=main.replace('kind < 2','kind < 8').replace('u16 ticks, x0, y0, x1, y1, x2, y2;','u16 ticks;')
    (project/'board.c').write_text(main);files+=['board.c']
    units=[dict(name='u'+str(i),file=name,backend='ordinary-wide',stackAuto=True,defines=[])
        for i,name in enumerate(files[1:])]
    board.write(project/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[],files=files,
        units=units,archives=[],link=[u['name'] for u in units]+['@startup']))
    (project/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(symbolic_index NONE)\n'
        'include("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\n'
        'target_sources(app INTERFACE '+' '.join(files)+')\nmx251_firmware(app)\n')
    board.write(root/'source.json',dict(cases=len(values),
        files={name:board.sha(project/name) for name in files},
        kinds=['byte-rmw','word-callbacks','long-callbacks','constant-offset',
            'branch-pointer','negative-index','dynamic-pointer','volatile']))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--gcc',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True)
    for c in sub.choices.values():c.add_argument('--root',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.root,a.gcc)
    else:bitfield_fixture.capture(a.hex,a.root/'capture',a.root/'reference.json')
