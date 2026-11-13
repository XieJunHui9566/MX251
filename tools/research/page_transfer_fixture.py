"""Run the mixed-call workload across two code pages and translation units."""
import argparse,json
from pathlib import Path
import near_transfer_fixture as near

def create(output):
 near.create(output)
 cases=output/'near-transfers-cases.c';text=cases.read_text()
 begin=text.index('u16 near_recursive(');end=text.index('u16 far_stack(')
 (output/'recursive.c').write_text('typedef unsigned int u16;\n'+text[begin:end],newline='\n')
 cases.write_text(text[:begin]+'u16 near_recursive(u16);\n\n'+text[end:],newline='\n')
 (output/'gap.c').write_text('void page_gap(void) {\n__asm\n'+'nop\n'*1400+'__endasm;\n}\n',newline='\n')
 driver=output/'board.c'
 driver.write_text(driver.read_text().replace('void main(void)','void page_gap(void);\nvoid main(void)').replace('    EA = 0;','    EA = 0;\n    page_gap();'),newline='\n')
 config=json.loads((output/'mx251.json').read_text())
 config['units'] += [dict(name='u2',file='recursive.c',backend='ordinary-wide',stackAuto=True,defines=[]),dict(name='u3',file='gap.c',backend='legacy',stackAuto=True,defines=[])]
 config['files']+=['recursive.c','gap.c'];config['link']=['u0','u3','u2','u1','@startup']
 near.board.write(output/'mx251.json',config)
 near.board.write(output/'source.json',dict(files={f:near.board.sha(output/f) for f in config['files']}))

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();create(a.output)
