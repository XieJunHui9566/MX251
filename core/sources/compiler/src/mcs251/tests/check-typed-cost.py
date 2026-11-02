"""Compare typed byte costs with actual Source-mode assembler output and C symbols.

No device access. Unknown costs must remain unknown. Does not estimate cycles.
"""
from pathlib import Path
import argparse, json, os, re, subprocess
p=argparse.ArgumentParser()
for name in ('sdcc','assembler','gcc','support','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args(); d=a.output.resolve();d.mkdir(parents=True,exist_ok=False)
header=Path(__file__).resolve().parents[1]
env={k.upper():v for k,v in os.environ.items()}
env['PATH']=str(a.support.resolve()/'bin')+';'+env['SYSTEMROOT']+'/System32'
env['SDCC_HOME']=str(a.support.resolve())
for k in ('CPATH','C_INCLUDE_PATH','CFLAGS','SDCC_INCLUDE','SDCC_LIB'):env.pop(k,None)
commands=[]
def run(label,args,cwd,stdin=None):
    q=subprocess.run(list(map(str,args)),cwd=cwd,input=stdin,capture_output=True,env=env,timeout=60)
    (cwd/(label+'.stdout.log')).write_bytes(q.stdout);(cwd/(label+'.stderr.log')).write_bytes(q.stderr)
    commands.append(dict(label=label,args=list(map(str,args)),cwd=str(cwd),exitCode=q.returncode))
    (d/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    return q
driver=r'''
#include <stdio.h>
#include "wide_cost.h"
static wide_cost_operand resolve(const char *s,const void *unused) {
  (void)unused;
  if(wide_cost_symbol_offset(s,"_Case",4)>=0 || wide_cost_symbol_offset(s,"_Sfr",1)>=0)
    return (wide_cost_operand){WC_DIRECT,0,0};
  if(wide_cost_symbol_offset(s,"_case",1)>=0)return (wide_cost_operand){WC_BIT,0,0};
  return (wide_cost_operand){WC_UNKNOWN,0,0};
}
int main(void) {
  char line[4096];while(fgets(line,sizeof line,stdin)) {
    wide_byte_cost c=wide_function_bytes_context(line,strlen(line),resolve,NULL);
    wide_byte_cost legacy=wide_function_bytes(line,strlen(line));
    printf("%u %u %u %u\n",c.bytes,c.unknown,legacy.bytes,legacy.unknown);
  }
  return wide_instruction_bytes("mov r1,r2",9)!=2;
}
'''
(d/'driver.c').write_text(driver)
q=run('gcc',[a.gcc.resolve(),'-std=c17','-O2','-Wall','-Wextra','-I'+str(header),d/'driver.c','-o',d/'driver.exe'],d);assert q.returncode==0
cases=[]
for reg in ('a','r0','r9','wr6','dr4'):
    for expr in ('_Case','(_Case)','(_Case+0)'):
        size=2 if reg=='a' else 3
        cases.extend([(f'mov {reg},{expr}',size),(f'mov {expr},{reg}',size)])
for offset in range(4):cases.append((f'mov (_Case+{offset}),#0x12',3))
cases += [('mov a,_Sfr',2),('mov _Sfr,r7',3),('mov _Sfr,#3',3),('clr _case',2),('setb _case',2),('mov c,_case',2),('mov _case,c',2),('mov c,acc.0',2),('clr a',1),('mov r7,r15',2)]
unknown=['clr _Case','mov r7,_case','mov a,_CASE','mov a,(_Case+4)', 'mov a,(_Case-1)', 'mov a,(_Case+2147483648)', 'mov a,(_Case+4294967296)', 'mov a,(_Case+1+1)', 'mov a,((_Case))', 'mov a,(_Case+0x1)', 'mov a,_CaseSuffix','mov a,(_Sfr+1)', 'mov a,_external_xdata', 'setb _missing', 'mov c,_missing', 'mov dr4,@dr0', 'mov a,(0x1234)']
cases += [(ins,None) for ins in unknown]
input_text='\n'.join(x[0] for x in cases)+'\n'
q=run('evaluate',[d/'driver.exe'],d,input_text.encode());assert q.returncode==0
rows=[]
for i,((ins,expected),line) in enumerate(zip(cases,q.stdout.decode().splitlines(),strict=True)):
    actual,unk,legacy,legacy_unknown=map(int,line.split())
    assert (unk==1 and actual==0) if expected is None else (unk==0 and actual==expected),(ins,line,expected)
    row=dict(instruction=ins,expected=expected,cost=actual,unknown=unk,legacyUnknown=legacy_unknown)
    if expected is not None:
        f=d/f'probe{i:03}.asm'
        f.write_text('.module cost\n.optsdcc -mmcs251\n.area DSEG (DATA)\n_Case: .ds 4\n.area BSEG (BIT)\n_case: .ds 1\n_Sfr = 0x95\n.area CSEG (CODE)\n\t'+ins+'\n')
        q=run(f'assemble{i:03}',[a.assembler.resolve(),'-plosgff',f.name],d);assert q.returncode==0,(ins,q.stderr)
        text=f.with_suffix('.lst').read_text();line=next(x for x in text.splitlines() if x.endswith('\t'+ins))
        m=re.match(r'^      [0-9A-F]{6}(.*?)\s+\d+ \t',line);assert m,line
        octets=re.sub(r'\[\s*\d+\]\s*$','',m[1]).strip()
        tokens=re.findall(r'[rstu*]?[0-9A-F]{2}',octets);assert ''.join(tokens)==re.sub(r'\s+','',octets),line
        assert len(tokens)==expected,(ins,tokens,expected)
        row['assemblerBytes']=len(tokens)
    rows.append(row)
(d/'operand-results.json').write_text(json.dumps(rows,indent=2)+'\n')
# 真实前端类型接入；同名不同大小写对象分别位于 data 与 bit 空间。
source='''
typedef unsigned long u32;
extern volatile __data u32 Ext;
volatile __data u32 Case;
volatile __bit cAse;
__sfr __at(0x95) Port;
__sbit __at(0x97) Pin;
#define ABI __reentrant __sdcccall(4)
u32 external(u32 x) ABI { Ext=x;return Ext+1; }
u32 mixed(u32 x) ABI {Case=x;cAse=x!=0;Port=(unsigned char)x;Pin=cAse;return Case+(u32)Port;}
unsigned char bits(unsigned char x) ABI {cAse=x;Pin=0;return cAse;}
volatile __xdata u32 Far;
u32 far_object(u32 x) ABI {Far=x;return Far;}
'''
results=[]
for model in ('small','large'):
    for opt in ('size','speed'):
        wd=d/(model+'-'+opt);wd.mkdir();(wd/'core.c').write_text(source)
        q=run('compile',[a.sdcc.resolve(),'-mmcs251','--model-'+model,'--stack-auto','--mcs251-native-abi','--mcs251-wide-backend','--opt-code-'+opt,'-c','core.c'],wd)
        assert q.returncode==0,q.stderr
        text=(wd/'core.asm').read_text()
        for name in ('external','mixed','bits'):assert 'wide-mir accepted: '+name+';' in text,(model,opt,name)
        assert 'wide-mir accepted: far_object;' not in text
        costs=re.findall(r'; wide cost objective=(\S+) known-bytes=(\d+) unknown=(\d+) group-known=(\d+)',text)
        assert len(costs)==3 and all(int(x[2])==0 and x[3]=='1' for x in costs),(wd,costs)
        results.append(dict(model=model,opt=opt,costs=costs,farSpaceFallback=True))
(d/'frontend-results.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS typed operands, actual assembler lengths, case/offset/unknown boundaries, frontend small/large size/speed. No hardware.')
