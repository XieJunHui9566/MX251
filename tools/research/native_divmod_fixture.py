"""Exercise both native DIV outputs, order, truncation and value lifetimes."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board
import narrow_division_fixture as narrow


def expected(kind, seed):
    result = narrow.expected(kind, seed)
    if kind:
        for a in range(seed*4, seed*4+4):
            for b in narrow.BYTE_DIVISORS:
                q, r = divmod(a, b)
                result = narrow.fold(result, (q << 8) | r)
                result = narrow.fold(result, q+r)
        return result
    a = narrow.ENDPOINTS[seed & 7] if seed < 32 else (65535-seed*1031) & 65535
    b = narrow.WORD_DIVISORS[seed & 15]
    q, r = divmod(a, b)
    values = [(q*17 & 65535)^r, (r*13 & 65535)^q,
              ((q & 255) << 8) | (r & 255), q^((a+3 & 65535) % b),
              (q if a & 1 else 7)^r, q^((a^0xa55a) % b), q^(a^0x3333)^r]
    total = 0
    for i in range(9):
        q, r = divmod(a, b)
        total = (total+q*3+r) & 65535
        a = (a+1031) & 65535
    for value in values+[total]:
        result = narrow.fold(result, value)
    return result


def create(output):
    narrow.create(output)
    tests = board.REPO/'core/sources/compiler/src/mcs251/tests'
    for name in ('native-divmod-pairs.h','native-divmod-pairs.c'):
        shutil.copy2(tests/name,output/name)
    p = output/'division-driver.c';s = p.read_text()
    s = '#include "native-divmod-pairs.h"\n'+s
    s += '\nuint16_t pair_touch(uint16_t a) { return a^0x3333u; }\n'
    s = s.replace('return mix(hash, div_volatile(a,b));', '''hash=mix(hash,div_volatile(a,b));
    hash=mix(hash,pair_qr(a,b));
    hash=mix(hash,pair_rq(a,b));
    hash=mix(hash,pair_truncated(a,b));
    hash=mix(hash,pair_changed(a,b));
    hash=mix(hash,pair_branch(a,b));
    hash=mix(hash,pair_volatile(a,b));
    hash=mix(hash,pair_call(a,b));
    return mix(hash,pair_loop(a,b));''')
    start=s.index('div_u32 division_bytes')
    tail=s[start:]
    tail=tail.replace('    return hash;', '''    for(j=0;j<4;++j)for(k=0;k<16;++k) {
        a=seed*4u+j; b=byte_divisors[k];
        hash=mix(hash,pair_byte(a,b));
        hash=mix(hash,pair_byte_wide(a,b));
    }
    return hash;''',1)
    p.write_text(s[:start]+tail,encoding='ascii',newline='\n')
    import json
    config=json.loads((output/'mx251.json').read_text())
    config['files']+=['native-divmod-pairs.c','native-divmod-pairs.h']
    config['units'].append(dict(name='pairs',file='native-divmod-pairs.c',backend='ordinary-wide',stackAuto=True,defines=[]))
    config['link'].insert(0,'pairs');board.write(output/'mx251.json',config)
    p=output/'CMakeLists.txt';p.write_text(p.read_text().replace('division-driver.c','native-divmod-pairs.c native-divmod-pairs.h division-driver.c'),encoding='ascii')
    board.write(output/'source.json',dict(files={f:board.sha(output/f) for f in config['files']}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('create');c.add_argument('--output',type=Path,required=True)
    c=sub.add_parser('capture');c.add_argument('--hex',type=Path,required=True);c.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='create':create(a.output)
    else:board.capture(a.hex,a.output,lambda data:board.check(data,expected))
