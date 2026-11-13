"""Check default-hook elimination, custom-hook equivalence and both profiles."""
import argparse
import concurrent.futures
import re
from pathlib import Path
import subprocess
from unified_sdk_matrix import read, save, pin, image


def case(config, parent, output, name, model, target, source, custom):
    results=[]
    for label,core in [('candidate',Path(config['core']))]+([('parent',parent)] if custom else []):
        out=output/name/label;out.mkdir(parents=True,exist_ok=False)
        (out/'main.c').write_text(source,encoding='ascii')
        save(out/'mx251.json',dict(schema=2,target=target,model=model,optimization='speed',backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[],files=['main.c'],units=[dict(name='u0',file='main.c',backend='ordinary-wide',stackAuto=True,defines=[])],archives=[],link=['u0','@startup']))
        (out/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(startup_check NONE)\ninclude("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\ntarget_sources(app INTERFACE main.c)\nmx251_firmware(app)\n',encoding='ascii')
        argv=[config['pwsh'],'-NoProfile','-File',str(core/'build/Build-CMake.ps1'),'-ProjectPath',str(out),'-CMake',config['cmake'],'-Ninja',config['ninja'],'-PowerShell',config['pwsh'],'-MachineIr']
        run=subprocess.run(argv,capture_output=True,timeout=180)
        (out/'build.log').write_bytes(run.stdout+run.stderr)
        assert run.returncode==0,(name,label,run.stderr.decode(errors='replace'))
        build=out/'.build/mx251/latest';r=read(build/'startup-runtime.json')
        assert r['applied'] != custom,(name,r.get('reason'))
        if custom:
            if label=='candidate':assert r['reason']=='startup-custom-external-hook',r
        else:
            assert r['layout']['savedBytes']==8,r
            assert '__sdcc_mcs251_reset_trampoline::' in (build/'startup.asm').read_text()
            owner=(build/r['demand']['mainOwner']).read_text()
            assert not re.search(r'ejmp\s+__sdcc_(?:gsinit_startup|program_startup)',owner)
            assert re.search(r'__sdcc_program_exit:\s*sjmp\s+\.',owner)
            if name in ('stc-small','external-initialized'):
                assert 'crtclear.rel' in r['demand']['replacements'],r['demand']['internalObjectBytes']
            if name=='internal-initialized':
                assert 'crtclear.rel' not in r['demand']['replacements']
            assert 'ecall   ___sdcc_external_startup' not in (build/'startup.asm').read_text()
            if name=='referenced-hook':
                assert r['demand']['hookReferenced']
                assert '_startup.rel' in (build/'linked.partial.map').read_text()
            if name=='pdata-xdata':
                asm=(build/'startup-runtime/crtxclear.asm').read_text()
                assert '__mx251_clear_PSEG:' in asm and '__mx251_clear_XSEG:' in asm
        stats,_=image(build/'firmware.hex')
        results.append(dict(label=label,record=pin(build/'startup-runtime.json'),**stats))
    if custom:
        assert results[0]['hex']['sha256']==results[1]['hex']['sha256'],name
    print(name,'PASS',flush=True)
    return dict(name=name,model=model,target=target,customHookUnchanged=custom,results=results)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--config',type=Path,required=True);p.add_argument('--parent',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    config=read(a.config); ai='AI8051U-34K64-32';stc='STC32G12K128'
    cases=[('stc-small','small',stc,'void main(void) { for (;;) {} }',False),
           ('internal-initialized','small',ai,'volatile __data unsigned char x=0x51; void main(void) { for (;;) x++; }',False),
           ('external-initialized','small',ai,'volatile __xdata unsigned char x[3]={1,2,3}; void main(void) { for (;;) x[0]++; }',False),
           ('stc-large','large',stc,'volatile __xdata unsigned char x[257]; void main(void) { for (;;) x[0]++; }',False),
           ('pdata-xdata','small',ai,'volatile __pdata unsigned char p[3]; volatile __xdata unsigned char x[257]; void main(void) { for (;;) x[0]=p[0]; }',False),
           ('referenced-hook','small',ai,'unsigned char __sdcc_external_startup(void); volatile unsigned char result; void main(void) { result=__sdcc_external_startup(); for (;;) {} }',False),
           ('hook-zero','small',ai,'volatile __xdata unsigned char x=0x51; unsigned char __sdcc_external_startup(void) { x=0xa7; return 0; } void main(void) { for (;;) {} }',True),
           ('hook-one','large',ai,'volatile __xdata unsigned char x=0x51; unsigned char __sdcc_external_startup(void) { x=0xa7; return 1; } void main(void) { for (;;) {} }',True)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        futures=[pool.submit(case,config,a.parent,a.output,*c) for c in cases]
        rows=[f.result() for f in futures]
    save(a.output/'result.json',dict(status='PASS',cases=rows,core=pin(Path(config['core'])/'core.json'),parent=pin(a.parent/'core.json')))


if __name__=='__main__':main()
