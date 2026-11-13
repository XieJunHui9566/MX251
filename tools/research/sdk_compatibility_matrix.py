"""Build uncovered C projects from original inputs with current dialect adapters.

Vendor arithmetic libraries are explicitly replaced by MX251's runtime. USB
binary libraries remain separate inputs: their functions cannot be discarded.
"""
import argparse
import concurrent.futures
from pathlib import Path
import re
import shutil
import importlib.util
import sdk_coverage_matrix as matrix


ARITHMETIC_LIBRARIES = {
    'stc32_mdu32_v1.1.lib', 'stc32_mdu32_large_v1.1.lib',
    'stc32_fpmu_large_v1.0.lib',
}


def prepare(config, coverage, project):
    base = Path(config['output'])/'projects'/project['id']
    out = base/'mx251'
    if (out/'dialect.json').exists():
        return
    source = coverage/'projects'/project['id']
    meta = matrix.read(source/'original-inputs.json')
    libraries = {s.lower() for s in meta['libraries']}
    if libraries - ARITHMETIC_LIBRARIES:
        raise ValueError('Binary library still required: '+', '.join(sorted(libraries)))
    prep, dialect = matrix.adapters(Path(config['corpus']))
    shutil.copytree(source/'native', base/'native')
    # Some vendor projects rely on the shared header without listing its path.
    if not any(p.name.lower() == 'ai8051u.h' for p in (base/'native').iterdir()):
        header = prep.ROOT/'COMM/AI8051U.H'
        shutil.copy2(header, base/'native/AI8051U.H')
        meta['files'].append(dict(name='AI8051U.H', source=str(header),
            sourceSha256=matrix.pin(header)['sha256'], copySha256=matrix.pin(header)['sha256']))
    errata = []
    for path in (base/'native').iterdir():
        if path.suffix.lower() not in ('.c', '.h'): continue
        before = path.read_bytes()
        after = before
        if project['id'] == 'p083' and path.name.lower() == 'sample.c':
            # Both arguments are unsigned long; keep the native and adapted
            # comparison inputs identical instead of accepting mismatched varargs.
            after = after.replace(b'Result2=%d', b'Result2=%lu')
            after = after.replace(b'Result3=%u', b'Result3=%lu')
        if project['id'] in ('p070', 'p106', 'p123'):
            after = after.replace(b'QSPI_Init', b'QSPI_init')
        if project['id'] in ('p144', 'p145'):
            if path.name.lower() == 'u8g2_porting.c':
                after = after.replace(b'0, &buffer, buf_idx', b'0, buffer, buf_idx')
            if path.name.lower() == 'u8g2_test.c':
                after = after.replace(b'u8g2_DrawBox(&u8g2,', b'u8g2_DrawBox(u8g2,')
                after = after.replace(b'u8g2_prepare();', b'u8g2_prepare(u8g2);')
                after = after.replace(b'u8g2_DrawStr(u8g2, frame_size + 12,', b'u8g2_DrawStr(u8g2, 0, frame_size + 12,')
        if after != before:
            old = matrix.pin(path)
            path.write_bytes(after)
            errata.append(dict(before=old, after=matrix.pin(path)))
            next(f for f in meta['files'] if f['name'] == path.name)['copySha256'] = matrix.pin(path)['sha256']
    matrix.save(base/'source-errata.json', dict(files=errata,
        reason='Correct symbol case, pointer indirection, missing arguments and long printf specifiers in vendor examples'))
    matrix.save(base/'original-inputs.json', meta)
    texts = {p.name: prep.decode(p) for p in (base/'native').iterdir()
             if p.suffix.lower() in ('.c', '.h', '.asm')}
    code = '\n'.join(prep.code(s) for s in texts.values())
    regs = {n:int(a,0) for n,a in re.findall(r'\bsfr\s+(\w+)\s*=\s*(0x[\da-fA-F]+|\d+)\s*;', code)}
    ext = sorted({n for n,r,b in re.findall(r'\bsbit\s+(\w+)\s*=\s*(\w+)\s*\^\s*([0-7])\s*;', code) if regs[r]%8})
    (out/'src').mkdir(parents=True)
    changes = []
    converted_names = {}
    provider = Path(__file__).resolve().parents[2]/'core/compat/keil'
    spec = importlib.util.spec_from_file_location('vector_aliases', provider/'vector_aliases.py')
    aliases = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(aliases)
    for name, text in texts.items():
        converted = dialect['asm_dialect'](text) if name in meta['assembly'] else dialect['mx_dialect'](text, regs, ext, [])
        target_name = name.lower()
        if name in meta['assembly']:
            redirects = aliases.translate(text, 'mx251_'+Path(name).stem.lower())
            if redirects is not None:
                converted = redirects
                target_name += '.c'
        converted_names[name] = target_name
        converted = prep.INC.sub(lambda m:m[1]+m[2].lower()+m[3], converted)
        target = out/'src'/target_name
        target.write_bytes(converted.encode('gb18030'))
        changes.append(dict(source=matrix.pin(base/'native'/name), adapted=matrix.pin(target)))
    (out/'src/intrins.h').write_text('#define _nop_() do { __asm nop __endasm; } while (0)\n', encoding='ascii')
    model = 'large' if project['id'] in ('p144', 'p145') else 'small'
    runtime = 'native-math' if libraries else 'standard'
    if libraries and all('mdu32' in name for name in libraries):
        # Preserve the requested hardware integer provider without pulling in
        # a floating-point profile. Selection is by input library capability.
        runtime = 'native-int-mdu32'
    settings = dict(schema=2, target='AI8051U-34K64-32', model=model,
        optimization='speed', backend='ordinary-wide', explicitAbi=False,
        standard='sdcc11', runtime=runtime, defines=[])
    if model == 'small': settings['defaultDataSpace'] = 'edata'
    matrix.save(out/'mx251.json', settings)
    cm = 'cmake_minimum_required(VERSION 3.25)\nproject(official_box NONE)\ninclude("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\n'
    sources = [converted_names[n] for n in meta['sources']+meta['assembly']]
    if project['id'] in ('p070', 'p106'):
        for name in ('uart1.h', 'uart1.c'):
            shutil.copy2(provider/name, out/'src'/name)
        sources.append('uart1.c')
        for name in meta['sources']:
            path = out/'src'/name.lower()
            path.write_bytes(b'#include "uart1.h"\n'+path.read_bytes())
    cm += 'target_sources(app INTERFACE '+' '.join('"src/'+n+'"' for n in sources)+')\n'
    cm += 'target_include_directories(app INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")\n'
    cm += 'target_compile_definitions(app INTERFACE __C251__=1 edata=__edata near=__edata '+meta['defines'].replace(',', ' ')+')\nmx251_firmware(app)\n'
    (out/'CMakeLists.txt').write_text(cm, encoding='utf-8')
    matrix.save(out/'dialect.json', dict(adapter=matrix.pin(Path(config['corpus'])/'build.py'),
        files=changes, replacedArithmeticLibraries=meta['libraries'], runtime=runtime,
        sourceErrataApplied=bool(errata), vectorAliasAdapter=matrix.pin(provider/'vector_aliases.py')))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('coverage', 'output', 'core'):
        ap.add_argument('--'+name, type=Path, required=True)
    ap.add_argument('--ids', required=True)
    ap.add_argument('--jobs', type=int, default=2)
    args = ap.parse_args()
    config = matrix.read(args.coverage/'config.json')
    config.update(output=str(args.output.resolve()), core=str(args.core.resolve()))
    matrix.save(args.output/'config.json', config)
    ids = set(args.ids.split(','))
    projects = [p for p in matrix.read(args.coverage/'inputs.json')['projects'] if p['id'] in ids]
    assert {p['id'] for p in projects} == ids
    tasks, prepared = [], []
    for original in projects:
        prepare(config, args.coverage, original)
        project = dict(original, libraries=[])
        project.pop('preparationError', None)
        base = args.output/'projects'/project['id']
        project['sourceManifest'] = matrix.pin(base/'original-inputs.json')
        prepared.append(project)
        local = dict(config)
        settings = matrix.read(base/'mx251/mx251.json')
        if settings['runtime'] != 'standard' or settings['model'] == 'large':
            local['mxSwitches'] = [s for s in config['mxSwitches'] if s != '-NativeIntegerRuntime']
        tasks.append((local, project, 'mx251'))
    matrix.save(args.output/'inputs.json', dict(projects=prepared, configuration=config,
        runner=matrix.pin(__file__)))
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        rows = list(pool.map(lambda t: matrix.one(*t), tasks))
    matrix.save(args.output/'results.json', rows)


if __name__ == '__main__':
    main()
