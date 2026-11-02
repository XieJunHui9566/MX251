"""Build the native float recipe with separate legacy trig members with typed bit-transport aliases.

Explicit local inputs; CMake owns the member graph and Ninja executes it.
This builds a candidate archive, not target or hardware qualification.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


# 成员顺序是归档契约的一部分；不从旧二进制归档猜测源码或引入旧对象。
MEMBERS = """_atof _schar2fs _sint2fs _slong2fs _slonglong2fs _uchar2fs
_uint2fs _ulong2fs _ulonglong2fs _fs2schar _fs2sint _fs2slong _fs2slonglong
_fs2uchar _fs2uint _fs2ulong _fs2ulonglong _fsadd _fsdiv _fsmul _fssub _fseq
_fslt _fsneq fabsf frexpf ldexpf expf powf sincosf sinf cosf logf log10f sqrtf
tancotf tanf cotf asincosf asinf acosf atanf atan2f sincoshf sinhf coshf tanhf
floorf ceilf modff errno isinf isnan _fscmp _fsget1arg _fsget2args _fsnormalize
_fsreturnval _fsrshift _fsswapargs _logexpf _fsle sqrt_domain fsmul_u16 _sincosf_compat _tancotf_compat""".split()
CONVERSIONS = {'_fseq': 'eq', '_fsneq': 'ne', '_fslt': 'lt', '_fsle': 'le',
               '_ulong2fs': 'u2f', '_slong2fs': 's2f', '_fs2ulong': 'f2u', '_fs2slong': 'f2s'}
CORES = {'_fsadd': 'fsadd_bits', '_fssub': 'fssub_bits', '_fsdiv': 'fsdiv_bits',
         '_fsmul': 'fsmul_bits', 'sqrtf': 'sqrt_bits'}
BASE_FLAGS = ['-mmcs251', '--model-small', '--stack-auto', '--opt-code-size',
              '--nostdinc', '--std-c23']


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf8')


def recipe(name):
    if name in ('_fsadd', '_fsdiv', '_fsmul'):
        return name, 'device/lib/mcs251/native-float/' + name[1:] + '_kernel.asm', []
    if name in CONVERSIONS:
        stem = 'bits_' + CONVERSIONS[name]
        return stem, 'device/lib/mcs251/native-float/' + stem + '.c', ['--mcs251-wide-backend']
    if name in CORES:
        stem = CORES[name]
        return stem, 'device/lib/mcs251/native-float/' + stem + '.c', ['--mcs251-wide-backend']
    if name == 'sqrt_domain':
        return name, 'device/lib/mcs251/native-float/' + name + '.c', ['--mcs251-native-abi', '--mcs251-wide-backend']
    if name == 'fsmul_u16':
        return name, 'device/lib/mcs251/native-float/' + name + '.c', ['--mcs251-native-abi']
    flags = ['--mcs251-native-runtime', '--mcs251-native-int-runtime']
    if name in ('sincosf', 'tancotf', 'atanf', 'sinf', 'cosf', 'tanf'):
        flags += ['--mcs251-wide-backend']
    return name, 'device/lib/' + name + '.c', flags


def environment(config):
    # 仅子进程使用显式工具目录，避免用户环境替换预处理器、头文件或库。
    env = {k.upper(): v for k, v in os.environ.items()}
    for key in ('SDCC_HOME', 'SDCC_INCLUDE', 'SDCC_LIB', 'SDCC_BIN', 'SDCCLIB',
                'SDCCDIR', 'CPATH', 'C_INCLUDE_PATH', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS'):
        env.pop(key, None)
    env['SDCC_HOME'] = config['toolchain']
    system = Path(env['SYSTEMROOT'])
    env['PATH'] = ';'.join(map(str, [Path(config['toolchain']) / 'bin', system / 'System32', system]))
    env['SOURCE_DATE_EPOCH'] = '0'
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    return env


def run(config, directory, label, args):
    args = list(map(str, args))
    record = {'arguments': args, 'cwd': str(directory)}
    write(directory / (label + '.command.json'), record)
    with (directory / (label + '.stdout.log')).open('wb') as out, (directory / (label + '.stderr.log')).open('wb') as err:
        process = subprocess.run(args, cwd=directory, env=environment(config), stdout=out, stderr=err)
    record['exitCode'] = process.returncode
    write(directory / (label + '.command.json'), record)
    if process.returncode:
        raise RuntimeError(label + ' failed; original logs: ' + str(directory))


def replace_tan_product(assembly, source):
    # 仅本原生运行库配方选择汇编；编译器及非原生 C 路径不变。
    kernel = Path(source) / 'device/lib/mcs251/native-float/tan_product_kernel.inc'
    if not kernel.exists():
        return assembly
    start = assembly.index('; wide capacity function=mcs251_multiply_two_over_pi ')
    end = assembly.index('; wide capacity function=mcs251_reduce_fraction ', start)
    body = kernel.read_text('utf8')
    if body.count('___m251r4_mcs251_multiply_two_over_pi:') != 1:
        raise ValueError('Expected one private ABI4 product entry')
    if re.search(r'^\s*(ecall|lcall|acall|push|pop|reti)\b', body, re.M) or re.search(r'\bspx\b', body):
        raise ValueError('Product leaf must not allocate a frame or call')
    return assembly[:start] + body + '\n' + assembly[end:]


def replace_u2f(assembly, source):
    # 仅作用于已声明 MCS-251 ABI4 的原生库配方；普通 C 路径保持可用。
    kernel = Path(source) / 'device/lib/mcs251/native-float/u2f_kernel.inc'
    if not kernel.exists():
        return assembly
    start = assembly.index('; wide capacity function=bits_u2f ')
    end = assembly.index('\t.area CSEG', assembly.index('; mx251-effects', start))
    body = kernel.read_text('utf8')
    if body.count('___m251r4_bits_u2f:') != 1:
        raise ValueError('Expected unique unsigned conversion ABI4 entry')
    if re.search(r'^\s*(ecall|lcall|acall|push|pop|reti)\b', body, re.M) or re.search(r'\b(spx|dpx|dpl|dph)\b', body):
        raise ValueError('Conversion must remain register-only leaf')
    return assembly[:start] + body + '\n' + assembly[end:]


def replace_tan_reducer(assembly, source):
    kernel = Path(source) / 'device/lib/mcs251/native-float/tan_reduce_kernel.inc'
    if not kernel.exists():
        return assembly
    # 检查真实私有调用闭包；不凭函数名推定寄存器保留。
    def clobbers(body):
        mask = 0
        for line in body.splitlines():
            line = line.split(';', 1)[0].strip()
            if not line or line.endswith(':') or line.startswith('.'):
                continue
            parts = line.split(None, 1); op = parts[0]
            args = [] if len(parts) == 1 else [a.strip() for a in parts[1].split(',')]
            if op in ('eret', 'cmp', 'je', 'jne', 'jc', 'jnc', 'jz', 'jnz', 'sjmp'):
                continue
            if op not in ('mov', 'movz', 'movc', 'clr', 'anl', 'orl', 'xrl', 'add', 'sub', 'inc', 'dec', 'srl', 'mul'):
                return None
            dest = args[0]
            if dest.startswith('@') or dest == 'dpx':
                continue
            if dest in ('a','b'): n,w = (11 if dest=='a' else 10),1
            else:
                m = re.fullmatch(r'(dr|wr|r)(\d+)', dest)
                if not m: return None
                n,w = int(m[2]), {'dr':4,'wr':2,'r':1}[m[1]]
            if op == 'mul': n,w = n//4*4,4
            mask |= ((1<<w)-1)<<n
        return mask
    product = assembly[assembly.index('___m251r4_mcs251_multiply_two_over_pi:'):assembly.index('; wide capacity function=mcs251_reduce_fraction ')]
    bit = assembly[assembly.index('___m251r4_mcs251_product_bit:'):assembly.index('; mx251-effects schema=1 function=___m251r4_mcs251_product_bit')]
    pm, bm = clobbers(product), clobbers(bit)
    # product必须保存DR28；读位函数必须保存WR0/R2/R3/WR26/DR28。
    if pm is None or bm is None or pm & 0xf0000000 or bm & 0xfc00000f:
        return assembly
    start = assembly.index('; wide capacity function=mcs251_reduce_fraction ')
    end = assembly.index('; wide capacity function=mcs251_tancot_denominator ', start)
    body = kernel.read_text('utf8')
    if body.count('___m251r4_mcs251_reduce_fraction:') != 1:
        raise ValueError('Expected unique private reducer entry')
    return assembly[:start] + body + '\n' + assembly[end:]


def replace_atan_flow(assembly, source):
    kernel = Path(source) / 'device/lib/mcs251/native-float/atan_flow_kernel.inc'
    if not kernel.exists():
        return assembly
    # 库自身的可选 ABI4 实现；常量契约不符时保留刚编译的原 C。
    header = (Path(source) / 'device/lib/_mcs251_math_bits.h').read_text('utf8')
    expected = {'MB_ONE':0x3f800000,'MB_EPS':0x39800000,'MB_AT_K1':0x3e8930a3,
        'MB_AT_K2':0x3f3b67af,'MB_AT_K3':0x3fddb3d7,'MB_AT_P0':0xbef110f6,
        'MB_AT_P1':0xbd508691,'MB_AT_Q0':0x3fb4ccd3,'MB_AT_Q1':0x3f800000,
        'MB_AT_A0':0,'MB_AT_A1':0x3f060a92,'MB_AT_A2':0x3fc90fdb,'MB_AT_A3':0x3f860a92}
    constants = {m[1]:int(m[2],16) for m in re.finditer(r'^#define (MB_\w+) (0x[0-9a-fA-F]+)UL$',header,re.M)}
    if any(constants.get(k)!=v for k,v in expected.items()) or '#define MATH_ABI __reentrant __sdcccall(4)' not in header:
        return assembly
    entries = ['___m251r4_mcs251_atan_poly','___m251r4_mcs251_atan_bits']
    if any(assembly.count(e+':')!=1 or not re.search(r'mx251-effects[^\n]*function='+e+r' abi=4 ',assembly) for e in entries):
        return assembly
    start = assembly.index('; wide capacity function=mcs251_atan_poly ')
    end = assembly.index('\t.area CSEG',assembly.index('; mx251-effects schema=1 function=___m251r4_mcs251_atan_bits',start))
    body = kernel.read_text('utf8')
    if any(body.count(e+':')!=1 for e in entries+['___m251r4_atanf']):
        raise ValueError('Expected unique atan ABI4 entries')
    return assembly[:start]+body+'\n'+assembly[end:]


def replace_f2u(assembly, source):
    kernel = Path(source) / 'device/lib/mcs251/native-float/f2u_kernel.inc'
    if not kernel.exists():
        return assembly
    entry = '___m251r4_bits_f2u'
    if assembly.count(entry+':') != 1 or not re.search(r'mx251-effects[^\n]*function='+entry+r' abi=4 ',assembly):
        return assembly
    start = assembly.index('; wide capacity function=bits_f2u ')
    end = assembly.index('\t.area CSEG',assembly.index('; mx251-effects',start))
    body = kernel.read_text('utf8')
    if body.count(entry+':') != 1:
        raise ValueError('Expected unique float-to-unsigned ABI4 entry')
    if re.search(r'^\s*(ecall|lcall|acall|push|pop|reti)\b',body,re.M) or re.search(r'\b(spx|dpx|dpl|dph)\b',body):
        raise ValueError('Conversion must remain register-only leaf')
    return assembly[:start]+body+'\n'+assembly[end:]


def compile_member(config, name):
    if name not in MEMBERS:
        raise ValueError('Unknown native float member: ' + name)
    source = Path(config['source'])
    directory = Path(config['output']) / 'objects'
    stem, relative, extra = recipe(name)
    if name in ('_fsadd', '_fsdiv', '_fsmul'):
        assembly = (source / relative).read_text(encoding='utf8')
        if re.search(r'^\s*(ecall|lcall|acall|push|pop|reti)\b', assembly, re.M):
            raise ValueError('Arithmetic leaf must not call, allocate stack or return from interrupt')
        (directory / (name + '.asm')).write_text(assembly, encoding='utf8')
        run(config, directory, name + '-assemble', [Path(config['toolchain']) / 'bin/sdas251.exe', '-plosgff', name + '.rel', name + '.asm'])
        member = directory / (name + '.rel')
        rel = member.read_text(encoding='utf8')
        definitions = dict(re.findall(r'^S (\S+) Def([0-9A-F]+)$', rel, re.M))
        public = '___m251r4__' + name
        alias = '___m251r4_mcs251_math_' + {'_fsadd': 'add', '_fsdiv': 'div', '_fsmul': 'mul'}[name] + '_bits'
        if set(definitions) != {'.__.ABS.', public, alias}:
            raise ValueError('Unexpected arithmetic exports')
        if definitions[public] != '000000' or definitions[alias] != '000000':
            raise ValueError('Float/bit ABI4 entries must share offset zero')
        if re.search(r'^S \S+ Ref', rel, re.M):
            raise ValueError('Arithmetic leaf must not import any symbol')
        areas = re.findall(r'^A (\S+) size ([0-9A-F]+) ', rel, re.M)
        if [a for a in areas if a != ('_CODE', '0') and a[0] != 'CSEG'] or len([a for a in areas if a[0] == 'CSEG']) != 1:
            raise ValueError('Arithmetic leaf must not allocate data or other sections')
        write(directory / (name + '.complete.json'), {'member': name + '.rel', 'source': relative,
              'sha256': digest(member), 'codeBytes': int(dict(areas)['CSEG'], 16), 'wideAccepted': [],
              'implementation': 'handwritten-native-integer-leaf', 'frameBytes': 0, 'calls': 0})
        return
    flags = BASE_FLAGS + ['-I' + str(source / 'device/include'), '-I' + str(source / 'device/include/mcs51')] + extra
    run(config, directory, name + '-compile', [config['compiler'], *flags, '-c', source / relative, '-o', stem + '.rel'])
    accepted = []
    if '--mcs251-wide-backend' in extra:
        assembly = (directory / (stem + '.asm')).read_text(encoding='utf8')
        accepted = re.findall(r'wide-mir accepted:.*', assembly)
        if not accepted or 'wide-mir fallback:' in assembly:
            raise ValueError('Native float wide admission changed: ' + name)
        if name == 'sincosf':
            helpers = ['mcs251_sincos_polynomial', 'mcs251_sincos_reduce', 'mcs251_sincos_bits']
            actual = [re.search(r'wide-mir accepted: ([^;]+);', row)[1] for row in accepted]
            if actual != helpers:
                raise ValueError('sincosf wide helper contract changed')
        elif name == 'tancotf':
            helpers = ['mcs251_product_bit', 'mcs251_multiply_two_over_pi', 'mcs251_reduce_fraction', 'mcs251_tancot_denominator', 'mcs251_tancot_bits']
            actual = [re.search(r'wide-mir accepted: ([^;]+);', row)[1] for row in accepted]
            if actual != helpers:
                raise ValueError('tancotf wide helper contract changed')
        elif name == 'atanf':
            helpers = ['mcs251_atan_poly', 'mcs251_atan_bits']
            actual = [re.search(r'wide-mir accepted: ([^;]+);', row)[1] for row in accepted]
            if actual != helpers:
                raise ValueError('atanf wide helper contract changed')
            # math.h 的公开 float 原型不变；单参数/返回的 ABI4 均为 DR4。
            # 不改代码字节，只保留原公开入口为整数位模式主体的同址标签。
            public, bits = '___m251r4_atanf', '___m251r4_mcs251_atan_bits'
            label = bits + ':'
            if assembly.count(label) != 1 or public in assembly:
                raise ValueError('atanf public alias must bind one unique bits entry')
            assembly = assembly.replace(label, '.globl ' + public + '\n' + public + ':\n' + label, 1)
        elif name in ('sinf', 'cosf', 'tanf'):
            helper = {'sinf':'mcs251_sin_bits', 'cosf':'mcs251_cos_bits', 'tanf':'mcs251_tan_bits'}[name]
            actual = [re.search(r'wide-mir accepted: ([^;]+);', row)[1] for row in accepted]
            if actual != [helper]:
                raise ValueError(name + ' native entry wide contract changed')
            # 仅ABI4 float与同宽整数入口共享标签；旧ABI2包装不作别名。
            public, bits = '___m251r4_' + name, '___m251r4_' + helper
            label = bits + ':'
            if assembly.count(label) != 1 or public in assembly:
                raise ValueError(name + ' public alias must bind one unique bits entry')
            assembly = assembly.replace(label, '.globl ' + public + '\n' + public + ':\n' + label, 1)
        elif name == '_fsmul':
            assembly = re.sub(r'^\s*\.module[^\n]*\n', '', assembly.replace('___m251r4___fsmul_bits', '___m251r4___fsmul_fallback'), flags=re.M)
            assembly = '.module _fsmul\n.area CSEG (CODE)\n' + (source / 'device/lib/mcs251/native-float/fsmul_fast.inc').read_text(encoding='utf8') + '\n' + assembly
        elif name in ('_fsadd', '_fssub'):
            for old, new in [('__fsadd_bits', '__fsadd'), ('__fssub_bits', '__fssub')]:
                assembly = assembly.replace('___m251r4_' + old, '___m251r4_' + new)
        elif name == '_fsdiv':
            assembly = assembly.replace('___m251r4___fsdiv_bits', '___m251r4___fsdiv')
        elif name == 'sqrtf':
            assembly = assembly.replace('___m251r4___sqrt_bits', '___m251r4_sqrtf')
        elif name == 'sqrt_domain':
            # 原有公共 ABI4 入口本身就是整数域错误 helper，不能套用转换桥。
            # 仅准入已审的零栈叶函数：普通 errno=EDOM、DR4 零返回、ERET。
            actual = [re.search(r'wide-mir accepted: ([^;]+);', row)[1] for row in accepted]
            if actual != ['__sqrt_domain'] or assembly.count('___m251r4___sqrt_domain:') != 1:
                raise ValueError('sqrt_domain original ABI4 entry contract changed')
            instructions = []
            for line in assembly.splitlines():
                match = re.match(r'^\s+([a-z][a-z0-9]*)\s*(.*?)\s*$', line.split(';', 1)[0])
                if match:
                    instructions.append((match[1], re.sub(r'\s+', '', match[2])))
            if instructions != [('mov', 'wr20,#0x0021'), ('mov', '(_errno),wr20'), ('sub', 'dr4,dr4'), ('eret', '')]:
                raise ValueError('sqrt_domain errno/zero-return/zero-stack leaf contract changed')
            if not re.search(r'wide-mir accepted: __sqrt_domain;[^\n]*frame=0', assembly):
                raise ValueError('sqrt_domain must not allocate a frame')
            if not re.search(r'mx251-effects[^\n]*function=___m251r4___sqrt_domain abi=4 ', assembly):
                raise ValueError('sqrt_domain must retain ABI4')
        else:
            # 保留原 ABI2 入口到 ABI4 位模式核心的字节布局和返回搬运。
            bridge = ['.area CSEG (CODE)', '.globl __' + name, '__' + name + ':',
                      'mov r4,a', 'mov r5,b', 'mov r6,dph', 'mov r7,dpl']
            comparison = CONVERSIONS[name] in ('eq', 'ne', 'lt', 'le')
            if comparison:
                bridge += ['mov wr12,@spx-6', 'mov wr14,@spx-4']
            bridge += ['ecall ___m251r4_' + stem]
            bridge += ['mov dpl,r7'] if comparison else ['mov a,r4', 'mov b,r5', 'mov dph,r6', 'mov dpl,r7']
            assembly += '\n' + '\n'.join(bridge + ['eret']) + '\n'
        # 新名字声明整数位型接口，原float/ABI2兼容符号全部保留。
        # 同址标签不新增指令；独立重汇编去标签后必须复现原核心对象。
        aliases = {'_fsadd':'add', '_fssub':'sub', '_fsmul':'mul', '_fsdiv':'div'}
        if name in aliases:
            original = '___m251r4__' + name
            alias = '___m251r4_mcs251_math_' + aliases[name] + '_bits'
            label = original + ':'
            if assembly.count(label) != 1:
                raise ValueError('Missing unique native bit core: ' + original)
            assembly = assembly.replace(label, '.globl ' + alias + '\n' + alias + ':\n' + label, 1)
        if name == 'tancotf':
            assembly = replace_tan_product(assembly, source)
            assembly = replace_tan_reducer(assembly, source)
        if name == '_ulong2fs':
            assembly = replace_u2f(assembly, source)
        if name == '_fs2ulong':
            assembly = replace_f2u(assembly, source)
        if name == 'atanf':
            assembly = replace_atan_flow(assembly, source)
        (directory / (name + '.asm')).write_text(assembly, encoding='utf8')
        run(config, directory, name + '-assemble', [Path(config['toolchain']) / 'bin/sdas251.exe', '-plosgff', name + '.rel', name + '.asm'])
    member = directory / (name + '.rel')
    definitions = dict(re.findall(r'^S (\S+) Def([0-9A-F]+)$', member.read_text(encoding='utf8'), re.M))
    if name == 'sqrt_domain':
        if set(definitions) != {'.__.ABS.', '___m251r4___sqrt_domain'} or definitions['___m251r4___sqrt_domain'] != '000000':
            raise ValueError('sqrt_domain public symbol or bridge contract changed')
        if not re.search(r'^S _errno Ref[0-9A-F]+$', member.read_text(encoding='utf8'), re.M):
            raise ValueError('sqrt_domain must relocate the original errno symbol')
    entry_aliases = {'atanf':'mcs251_atan_bits', 'sinf':'mcs251_sin_bits', 'cosf':'mcs251_cos_bits', 'tanf':'mcs251_tan_bits'}
    if name in entry_aliases:
        public, bits = '___m251r4_' + name, '___m251r4_' + entry_aliases[name]
        if definitions.get(public) is None or definitions.get(public) != definitions.get(bits):
            raise ValueError(name + ' float and bits entries must have the same address')
    if name in ('sincosf', 'tancotf'):
        if ('_' + name) in definitions or ('___m251r4_mcs251_' + name[:-1] + '_bits') not in definitions:
            raise ValueError(name + ' worker and legacy member separation changed')
    compatibility_exports = {'_sincosf_compat':'_sincosf', '_tancotf_compat':'_tancotf'}
    if name in compatibility_exports and compatibility_exports[name] not in definitions:
        raise ValueError('Missing original ABI2 trig entry: ' + name)
    size = re.search(r'^A CSEG size ([0-9A-F]+) ', member.read_text(encoding='utf8'), re.M)
    if not size:
        raise ValueError('Missing CSEG: ' + name)
    write(directory / (name + '.complete.json'), {'member': name + '.rel', 'source': relative,
          'sha256': digest(member), 'codeBytes': int(size[1], 16), 'wideAccepted': accepted})


def create_archive(config):
    objects = Path(config['output']) / 'objects'
    pending = objects / 'libnativefloat.pending.lib'
    run(config, objects, 'archive', [Path(config['toolchain']) / 'bin/sdar.exe', '-rcsD', pending.name, *[n + '.rel' for n in MEMBERS]])


def archive_members(path):
    data = path.read_bytes()
    if data[:8] != b'!<arch>\n':
        raise ValueError('Invalid archive signature')
    offset, names, result = 8, b'', {}
    while offset < len(data):
        header = data[offset:offset + 60]
        if len(header) != 60 or header[58:] != b'`\n':
            raise ValueError('Invalid archive member header')
        count = int(header[48:58]); name = header[:16].decode('ascii').strip()
        payload = data[offset + 60:offset + 60 + count]
        if len(payload) != count:
            raise ValueError('Truncated archive member')
        offset += 60 + count + count % 2
        if name == '//':
            names = payload; continue
        if name == '/':
            continue
        if name.startswith('/'):
            start = int(name[1:]); end = names.find(b'/\n', start)
            if end < start:
                raise ValueError('Invalid archive long name')
            name = names[start:end].decode('ascii')
        else:
            name = name.rstrip('/')
        if name in result:
            raise ValueError('Duplicate archive member')
        result[name] = payload
    return result


def input_inventory(config):
    source = Path(config['source']); paths = {Path(__file__).resolve(), Path(sys.executable).resolve()}
    paths.update(source / recipe(name)[1] for name in MEMBERS)
    paths.update(p for p in (source / 'device/include').rglob('*') if p.is_file())
    paths.update(p for p in (source / 'device/lib').rglob('*') if p.is_file() and p.suffix in ('.h', '.inc'))
    # 汇编器、预处理器、归档器及其 DLL 都是实际构建输入。
    paths.update(p for p in (Path(config['toolchain']) / 'bin').iterdir() if p.is_file())
    for name in ('compiler', 'cmake', 'ninja'):
        tool = Path(config[name]); paths.add(tool); paths.update(tool.parent.glob('*.dll'))
    return [{'path': str(p), 'sha256': digest(p)} for p in sorted(paths)]


def cmake_quote(value):
    # CMake bracket arguments prevent path interpolation, including spaces and '$'.
    value = str(value).replace('\\', '/')
    if ']==]' in value or ';' in value or '\n' in value or '\r' in value:
        raise ValueError('Unsupported CMake argument character')
    return '[==[' + value + ']==]'


def build(args):
    config = {name: str(getattr(args, name).resolve(strict=True)) for name in ('source', 'compiler', 'toolchain', 'cmake', 'ninja')}
    output = args.output.resolve()
    roots = [Path(config['source']), Path(config['toolchain']), Path(__file__).resolve().parent]
    if output.exists() or any(output == root or output.is_relative_to(root) for root in roots):
        raise ValueError('Output must be new and outside source, toolchain and builder inputs')
    for path in [Path(config[n]) for n in ('compiler', 'cmake', 'ninja')] + [Path(config['toolchain']) / 'bin' / n for n in ('sdcpp.exe', 'sdas251.exe', 'sdar.exe')]:
        if not path.is_file():
            raise FileNotFoundError(path)
    if len(MEMBERS) != len(set(MEMBERS)) or MEMBERS[-2:] != ['_sincosf_compat', '_tancotf_compat']:
        raise ValueError('Invalid native float member recipe')
    config['output'] = str(output)
    inventory = input_inventory(config)
    output.mkdir(parents=True, exist_ok=False); (output / 'objects').mkdir()
    try:
        write(output / 'input-lock.json', inventory); write(output / 'build-config.json', config)
        cmake = ['cmake_minimum_required(VERSION 3.25)', 'project(MX251NativeFloat NONE)']
        dependencies = ' '.join(cmake_quote(row['path']) for row in inventory)
        script = Path(__file__).resolve(); python = Path(sys.executable).resolve()
        for name in MEMBERS:
            completed = output / 'objects' / (name + '.complete.json')
            command = [python, '-B', str(script), '--worker', output / 'build-config.json', '--member', name]
            cmake += ['add_custom_command(OUTPUT ' + cmake_quote(completed) + '\n COMMAND ' + ' '.join(map(cmake_quote, command)) + '\n DEPENDS ' + dependencies + '\n VERBATIM)']
        stamps = [output / 'objects' / (n + '.complete.json') for n in MEMBERS]
        pending = output / 'objects/libnativefloat.pending.lib'
        command = [python, '-B', str(script), '--worker', output / 'build-config.json', '--archive']
        cmake += ['add_custom_command(OUTPUT ' + cmake_quote(pending) + '\n COMMAND ' + ' '.join(map(cmake_quote, command)) + '\n DEPENDS ' + ' '.join(map(cmake_quote, stamps)) + '\n VERBATIM)',
                  'add_custom_target(native_float ALL DEPENDS ' + cmake_quote(pending) + ')']
        (output / 'CMakeLists.txt').write_text('\n'.join(cmake) + '\n', encoding='utf8')
        run(config, output, 'configure', [config['cmake'], '-S', output, '-B', output / 'build', '-G', 'Ninja', '-DCMAKE_MAKE_PROGRAM=' + config['ninja']])
        run(config, output, 'build', [config['cmake'], '--build', output / 'build', '--parallel', '4'])
        objects = output / 'objects'; records = []
        for name in MEMBERS:
            row = json.loads((objects / (name + '.complete.json')).read_text(encoding='utf8'))
            if row['sha256'] != digest(objects / (name + '.rel')):
                raise ValueError('Member changed after compilation: ' + name)
            records.append(row)
        contents = archive_members(pending)
        if list(contents) != [n + '.rel' for n in MEMBERS] or any(contents[n + '.rel'] != (objects / (n + '.rel')).read_bytes() for n in MEMBERS):
            raise ValueError('Archive member set, order or bytes differ')
        if input_inventory(config) != inventory:
            raise ValueError('Source or tools changed during build')
        receipt = {'schema': 1, 'status': 'complete', 'runtime': 'native-float', 'model': 'small-stack-auto',
                   'buildChain': ['CMake', 'Ninja', 'MX251'], 'inputLockSha256': digest(output / 'input-lock.json'),
                   'compilerSha256': digest(Path(config['compiler'])), 'archiveSha256': digest(pending),
                   'members': records, 'hardwareExecuted': False, 'qualificationGranted': False}
        write(output / 'receipt.pending.json', receipt)
        pending.replace(output / 'libnativefloat.lib')
        (output / 'receipt.pending.json').replace(output / 'receipt.json')
        print(json.dumps({'archive': str(output / 'libnativefloat.lib'), 'sha256': receipt['archiveSha256'], 'members': len(MEMBERS)}))
    except BaseException as error:
        # 目录由本次排他创建；失败不保留可被误用的最终库或成功凭据。
        for name in ('libnativefloat.lib', 'receipt.json', 'receipt.pending.json'):
            (output / name).unlink(missing_ok=True)
        write(output / 'failure.json', {'status': 'failed', 'error': str(error), 'type': type(error).__name__})
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'compiler', 'toolchain', 'cmake', 'ninja', 'output'):
        parser.add_argument('--' + name, type=Path)
    parser.add_argument('--worker', type=Path, help=argparse.SUPPRESS)
    parser.add_argument('--member', help=argparse.SUPPRESS)
    parser.add_argument('--archive', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.worker:
        if bool(args.member) == args.archive:
            parser.error('Internal worker requires exactly one member or archive')
        config = json.loads(args.worker.read_text(encoding='utf8'))
        create_archive(config) if args.archive else compile_member(config, args.member)
    else:
        if any(getattr(args, name) is None for name in ('source', 'compiler', 'toolchain', 'cmake', 'ninja', 'output')):
            parser.error('--source --compiler --toolchain --cmake --ninja --output are required')
        build(args)


if __name__ == '__main__':
    main()
