"""Research source variants; bound mode is not an implemented compiler pass."""
from pathlib import Path
import argparse,re,json,subprocess,os
parser=argparse.ArgumentParser(description='Compare generic-pointer and explicitly bound-object probe sources.')
for name in ('output','core','vendor','keil-bin','node'):
 parser.add_argument('--'+name,type=Path,required=True)
args=parser.parse_args()
R=args.output.resolve();core=args.core.resolve();keil=args.keil_bin.resolve()
adapter=Path(__file__).with_name('stcide_compile.mjs')
common='typedef unsigned char u8;\ntypedef unsigned int u16;\ntypedef unsigned long u32;\n'
patterns={
 'array_sum':('u8 DATA payload[32];\n','u16 total=0;u8 i;for(i=0;i<count;++i)total+=p[i];return total;'),
 'array_mix':('u8 DATA payload[32];\n','u16 total=0;u8 i;for(i=0;i<count;++i)total=(total<<1)^p[i];return total;'),
 'field_config':('typedef struct {u8 enable;u8 mode;u16 rate;u8 mask;u8 port;} Cfg;\nCfg DATA payload;\n','u16 v=p->rate;if(p->enable){v+=p->mode;if(p->mask&count)v^=0x1357;}else{v+=p->port;}return v;'),
}
results=[]
for name,(decl,body) in patterns.items():
 for mode in ('generic','bound'):
  ptype='const Cfg *' if name=='field_config' else 'const u8 *'
  obj='&payload' if name=='field_config' else 'payload'
  func='u16 probe('+((ptype+'p,') if mode=='generic' else '')+'u8 count)\n{\n'+(body if mode=='generic' else (body.replace('p->','payload.') if name=='field_config' else body.replace('p[','payload[')))+'\n}\n'
  main='volatile u8 DATA input;\nvolatile u16 DATA output;\nvoid main(void){while(1)output=probe('+((obj+',') if mode=='generic' else '')+'input);}\n'
  src=common+decl+func+main
  for compiler in ('mx251','keil','stcide'):
   out=R/'pointer-probes'/name/mode/compiler;out.mkdir(parents=True,exist_ok=True)
   text=src.replace('DATA','__data' if compiler=='mx251' else 'data');(out/'probe.c').write_text(text)
   if compiler=='stcide':
    req=dict(files=[dict(name='probe.c',code=text)],target='AI8051U',opts=dict(eepromSize=512))
    (out/'request.json').write_text(json.dumps(req))
    cmd=[str(args.node.resolve()),str(adapter),str(out/'request.json'),str(out/'response.json'),str(args.vendor.resolve())]
   elif compiler=='mx251':
    cmd=[str(core/'toolchain/bin/sdcc.exe'),'-mmcs251','--model-small','--opt-code-speed','--stack-auto','--mcs251-native-abi','--mcs251-function-metadata','--mcs251-machine-ir','--mcs251-wide-ordinary','--std-c11','-c','probe.c']
   else:cmd=[str(keil/'C251.EXE'),'probe.c','XSMALL','ROM(LARGE)','OPTIMIZE(7,SPEED)','SRC(probe.src)','PRINT(probe.c.lst)']
   env=os.environ.copy();env['SDCC_HOME']=str(core/'toolchain');env['PATH']=str(core/'toolchain/bin')+';'+env['PATH']
   run=subprocess.run(cmd,cwd=out,capture_output=True,env=env,timeout=30);(out/'command.json').write_text(json.dumps(cmd));(out/'compile.log').write_bytes(run.stdout+run.stderr)
   row=dict(pattern=name,mode=mode,compiler=compiler,exit=run.returncode)
   if compiler=='keil' and run.returncode==0:
    a=[str(keil/'A251.EXE'),'probe.src','OBJECT(probe.obj)','PRINT(probe.lst)'];run=subprocess.run(a,cwd=out,capture_output=True);(out/'assemble.log').write_bytes(run.stdout+run.stderr)
    (out/'assemble-command.json').write_text(json.dumps(a));row['assembleExit']=run.returncode
    active=False;count=0
    for line in (out/'probe.lst').read_text('latin1').splitlines():
     if re.search(r'\sPROBE PROC\s*$',line,re.I):active=True
     if re.search(r'\sENDP\s*$',line,re.I):active=False
     m=re.match(r'^([0-9A-F]{6})\s+([0-9A-F]+)\s+',line)
     if active and m:count+=len(m[2])//2
    row['probeBytes']=count
   elif compiler=='mx251' and run.returncode==0:
    active=False;count=0
    for line in (out/'probe.lst').read_text('latin1').splitlines():
     if 'mx251-function begin schema=1 symbol=_probe ' in line:active=True
     if active and re.match(r'^      [0-9A-F]{6} ',line):count+=len(re.findall('[0-9A-F]{2}',line[13:28]))
     if 'mx251-function end schema=1 symbol=_probe' in line:active=False
    row['probeBytes']=count
   elif compiler=='stcide':
    response=json.loads((out/'response.json').read_text());rr=response.get('result',{});row['success']=rr.get('success');row['error']=str(rr.get('error',''))[:300];row['buildVer']=response.get('buildVer')
    if rr.get('success'):
     row['imageBytes']=rr['memReport']['rom'];(out/'probe.asm').write_text(rr['asm'],encoding='utf8');row['warnings']=rr.get('warnings');row['functions']=rr.get('funcDefs');row['notes']=rr.get('notes')
   results.append(row);print({k:v for k,v in row.items() if k not in ('warnings','functions','notes')},flush=True)
(R/'pointer-probes/results.json').write_text(json.dumps(results,indent=2,ensure_ascii=False),encoding='utf8')
