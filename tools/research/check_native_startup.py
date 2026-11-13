"""Exercise linked startup with actual native floating-point library members."""
import argparse
import concurrent.futures
from pathlib import Path
from check_linked_startup import case
from unified_sdk_matrix import read, save


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config',type=Path,required=True)
    p.add_argument('--parent',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    config=read(a.config)
    prefix='volatile __edata float input=1.25f, result;\n'
    specs=[
        ('native-empty','STC32G12K128','',None),
        ('native-internal','AI8051U-34K64-32','volatile __data unsigned char x=0x51;',None),
        ('native-copy','AI8051U-34K64-32','volatile __xdata unsigned char x[3]={1,2,3};',None),
        ('native-clear','AI8051U-34K64-32','volatile __pdata unsigned char p[3]; volatile __xdata unsigned char x[257];',None),
        ('native-hook-zero','AI8051U-34K64-32','',0),
        ('native-hook-one','AI8051U-34K64-32','',1),
    ]
    def run(spec):
        name,target,storage,hook=spec
        source=prefix+storage+'\n'
        if hook is not None:
            source+='unsigned char __sdcc_external_startup(void) { input=7.0f; return '+str(hook)+'; }\n'
        source+='void main(void) { result=input/5.0f; for (;;) {} }\n'
        row=case(config,a.parent,a.output,name,'small',target,source,hook is not None,'native-math')
        root=a.output/name/'candidate/.build/mx251/latest'
        mapping=(root/'linked.partial.map').read_text()
        assert 'libnativefloat.lib' in mapping and '_fsdiv.rel' in mapping,name
        record=read(root/'startup-runtime.json')
        if hook is None:
            # Explicit --nostdlib CRT operands must select the private archive.
            assert 'startup-libraries' in mapping,name
            assert record['savedBytes']>0,name
            if name=='native-copy':
                assert record['demand']['sectionBytes']['XINIT']==3
                assert 'crtxinit.rel' not in record['demand']['replacements']
            if name=='native-clear':
                asm=(root/'startup-runtime/crtxclear.asm').read_text()
                assert '__mx251_clear_PSEG:' in asm and '__mx251_clear_XSEG:' in asm
        return row
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        rows=list(pool.map(run,specs))
    save(a.output/'result.json',dict(status='PASS',cases=rows))


if __name__=='__main__':main()
