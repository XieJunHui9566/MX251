"""Compile/assemble tail transfers and the cases which need a real call frame."""
import argparse,json,re,shutil,subprocess
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--sdcc',type=Path,required=True)
p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False)
tests=Path(__file__).resolve().parent;rows=[]
for model in ('small','large'):
    for opt in ('speed','size'):
        d=a.output_dir/(model+'-'+opt);d.mkdir()
        for name in ('tail-boundary.h','tail-boundary-callers.c'):
            shutil.copy2(tests/name,d/name)
        cmd=[str(a.sdcc.resolve()),'-mmcs251','--model-'+model,'--stack-auto',
             '--opt-code-'+opt,'--mcs251-wide-ordinary','--mcs251-function-metadata',
             '-c','tail-boundary-callers.c']
        q=subprocess.run(cmd,cwd=d,capture_output=True)
        (d/'compile.log').write_bytes(q.stdout+q.stderr)
        assert not q.returncode,q.stderr
        text=(d/'tail-boundary-callers.asm').read_text()
        functions={m[1].removeprefix('___m251r4_').removeprefix('_'):m[2]
            for m in re.finditer(r'; mx251-function begin schema=1 symbol=(\S+)[^\n]*\n(.*?)'
                                r'; mx251-function end schema=1',text,re.S)}
        tails={'tail8':'_leaf8','tail16':'_leaf16','tail32':'_leaf32','tailf':'_leaff',
               'tailp':'_leafp','incoming_stack':'_leaf32','implicit_void':'_sink0',
               'discarded_result':'_leaf32','native_tail':'___m251r4_native_pair'}
        for name,callee in tails.items():
            body=functions[name]
            assert re.search(r'\bejmp\s+'+callee+r'\s*$',body,re.M),name
            assert not re.search(r'\becall\s+'+callee+r'\s*$',body,re.M),name
        for name,callee in {'outgoing_stack':'_pair32','local_address':'_read_local',
                            'parameter_address':'_read_local','after_call':'_leaf32',
                            'converted_result':'_leaf8','mixed_abi':'___m251r4_native_pair'}.items():
            body=functions[name]
            assert re.search(r'\becall\s+'+callee+r'\s*$',body,re.M),name
            assert not re.search(r'\bejmp\s+'+callee+r'\s*$',body,re.M),name
        body=functions['dispatch']
        assert len(re.findall(r'\bejmp\s+_sink[0-7]\s*$',body,re.M))==8
        assert not re.search(r'\becall\s+_sink',body)
        # No unreachable outgoing jump after a terminal transfer, including
        # jump-table functions which bypass the shared text CFG optimizer.
        instructions=[s.split(';',1)[0].strip() for s in body.splitlines()]
        instructions=[s for s in instructions if s]
        for i,s in enumerate(instructions[:-1]):
            if re.match(r'ejmp\s+_sink',s):
                assert not re.match(r'(?:s|e|l)jmp\s+__wm_',instructions[i+1]),instructions[i:i+2]
        rows.append(dict(model=model,optimization=opt,tailFunctions=len(tails),dispatchTargets=8,
                         retainedCallFunctions=6,command=cmd))
(a.output_dir/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
print('PASS: tail transfers, shared exits, stack arguments, local addresses and ABI boundaries in 4 configurations')
