"""Inventory the official box archive and build projects outside the frozen 75.

Inputs and dialect conversions are reused from the original frozen corpus.
The compiler is not changed by this runner. Pure assembly and binary-library
projects remain visible even when a toolchain cannot consume their inputs.
"""
from pathlib import Path
import argparse
import ast
import collections
import concurrent.futures
import importlib.util
import json
import re
import shutil
import time
import traceback
import zipfile
import unified_sdk_matrix as matrix

read, save, pin = matrix.read, matrix.save, matrix.pin
MATH_IDS = {'p041', 'p076', 'p077', 'p079', 'p081', 'p093', 'p094', 'p127', 'p130'}


def adapters(corpus):
    spec = importlib.util.spec_from_file_location('box_prepare', corpus/'prepare.py')
    prep = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prep)
    # Load only the published dialect functions, not historical build entrypoints.
    tree = ast.parse((corpus/'build.py').read_text('utf-8'))
    definitions = [n for n in tree.body if isinstance(n, (ast.FunctionDef, ast.ClassDef))
                   and n.name in ('Blocked', 'mx_dialect', 'asm_dialect')]
    namespace = dict(re=re, LEX=prep.LEX)
    exec(compile(ast.Module(body=definitions, type_ignores=[]), str(corpus/'build.py'), 'exec'), namespace)
    return prep, namespace


def prepare(config):
    root, corpus = Path(config['output']), Path(config['corpus'])
    manifest = root/'inputs.json'
    if manifest.exists():
        existing = read(manifest)
        assert existing['configuration'] == config
        return existing['projects']
    inv = read(corpus/'inventory.json')
    ledger = read(config['ledger'])['rows']
    baseline = {r['id'] for r in ledger
                if r['temporaryAcceptanceLabel'] != '体积尚不可按同模型验收，耗时未知'}
    assert len(baseline) == 75
    old = {r['id']: r for r in read(corpus/'COMPARISON.json')['rows']}
    prep, dialect = adapters(corpus)
    projects = []
    for row in inv:
        if row['id'] in baseline:
            continue
        key = row['id']
        source, dest = corpus/'projects'/key, root/'projects'/key
        native = dest/'native'
        native.mkdir(parents=True)
        if row['cSources']:
            meta = read(source/'inputs.json')
            for item in meta['files']:
                p = source/'native'/item['name']
                assert pin(p)['sha256'] == item['copySha256']
                shutil.copy2(p, native/p.name)
        else:
            target, files, names, flat = prep.collect(row)
            opts = target.find('TargetOption/Target251')
            for name, text in flat.items():
                (native/name).write_bytes(text.encode('gb18030'))
            meta = dict(id=key, name=row['name'], sources=[], libraries=[],
                        assembly=[names[Path(f['path'])] for f in row['files'] if f['type']=='2'],
                        files=[dict(name=names[p], source=str(p), sourceSha256=prep.sha(p),
                                    copySha256=prep.sha(native/names[p])) for p in files],
                        options=prep.ET.tostring(opts, encoding='unicode'),
                        defines=opts.findtext('C251/VariousControls/Define') or '',
                        model=opts.findtext('Target251Misc/MemoryModel'),
                        optLevel=int(opts.findtext('C251/Optim')), project=pin(row['project']))
        save(dest/'original-inputs.json', meta)
        adapted = source/('mx251-braces' if (source/'mx251-braces/src').is_dir() else 'mx251')
        project = dict(id=key, name=row['name'], cSources=row['cSources'],
                       group='math9' if key in MATH_IDS else 'other-c' if row['cSources'] else 'assembly',
                       assembly=meta['assembly'], libraries=meta['libraries'],
                       sourceManifest=pin(dest/'original-inputs.json'),
                       originalFiles=[pin(p) for p in sorted(native.iterdir())],
                       historicalKeilBytes=old.get(key, {}).get('suites', {}).get('keil-official', {}).get('result', {}).get('payloadBytes'))
        mx = dest/'mx251'
        mx.mkdir()
        if row['cSources'] and not meta['libraries']:
            try:
                if (adapted/'src').is_dir():
                    shutil.copytree(adapted/'src', mx/'src')
                    for name in ('CMakeLists.txt', 'mx251.json', 'dialect.json'):
                        shutil.copy2(adapted/name, mx/name)
                    project['mxInputOrigin'] = str(adapted)
                else:
                    texts = {p.name: prep.decode(p) for p in native.iterdir()
                             if p.suffix.lower() in ('.c', '.h', '.asm')}
                    allcode = '\n'.join(prep.code(s) for s in texts.values())
                    registers = {n:int(a,0) for n,a in re.findall(r'\bsfr\s+(\w+)\s*=\s*(0x[\da-fA-F]+|\d+)\s*;', allcode)}
                    extended = sorted({n for n,r,b in re.findall(r'\bsbit\s+(\w+)\s*=\s*(\w+)\s*\^\s*([0-7])\s*;', allcode) if registers[r]%8})
                    (mx/'src').mkdir()
                    for name, text in texts.items():
                        result = dialect['asm_dialect'](text) if name in meta['assembly'] else dialect['mx_dialect'](text, registers, extended, [])
                        result = prep.INC.sub(lambda m:m[1]+m[2].lower()+m[3], result)
                        (mx/'src'/name.lower()).write_bytes(result.encode('gb18030'))
                    (mx/'src/intrins.h').write_text('#define _nop_() do { __asm nop __endasm; } while (0)\n')
                    save(mx/'mx251.json', dict(schema=2, target='AI8051U-34K64-32', model='small', optimization='speed', backend='ordinary-wide', explicitAbi=False, standard='sdcc11', runtime='standard', defines=[]))
                    cm = 'cmake_minimum_required(VERSION 3.25)\nproject(official_box NONE)\ninclude("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\n'
                    cm += 'target_sources(app INTERFACE '+' '.join('"src/'+n.lower()+'"' for n in meta['sources']+meta['assembly'])+')\n'
                    cm += 'target_include_directories(app INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")\n'
                    cm += 'target_compile_definitions(app INTERFACE __C251__=1 '+meta['defines'].replace(',', ' ')+')\nmx251_firmware(app)\n'
                    (mx/'CMakeLists.txt').write_text(cm)
                    project['mxInputOrigin'] = 'frozen corpus dialect functions'
            except Exception as error:
                project['preparationError'] = str(error)
            project['mxInputs'] = [pin(p) for p in sorted(mx.rglob('*')) if p.is_file()]
        projects.append(project)
    archive = read(corpus/'SOURCE.json')['archive']
    with zipfile.ZipFile(archive) as z:
        archive_projects = collections.Counter(n.split('/')[0] for n in z.namelist()
                                               if n.lower().endswith(('.uvproj', '.uvprojx')))
    actual = {str(p.resolve()).lower() for p in prep.ROOT.rglob('*.uvproj')}
    assert actual == {str(Path(r['project']).resolve()).lower() for r in inv}
    save(root/'inventory.json', dict(archive=pin(archive), archiveProjectCounts=dict(archive_projects),
        c251Projects=len(inv), cProjects=sum(bool(r['cSources']) for r in inv), assemblyProjects=sum(not r['cSources'] for r in inv),
        baselineIds=sorted(baseline), rebuildCIds=[p['id'] for p in projects if p['cSources']],
        rows=[dict(id=r['id'], name=r['name'], cSources=r['cSources'], baseline=r['id'] in baseline) for r in inv]))
    save(manifest, dict(configuration=config, projects=projects, compiler=read(Path(config['core'])/'core.json'),
                        dialectAdapter=pin(corpus/'build.py'), toolPins=[pin(p) for p in config['toolFiles']],
                        hardwareExecuted=False, sourceAlgorithmsChanged=False))
    return projects


def one(config, project, suite):
    out = Path(config['output'])/'projects'/project['id']/suite
    out.mkdir(exist_ok=True)
    if (out/'result.json').exists():
        return read(out/'result.json')
    started = time.monotonic()
    try:
        if suite.startswith('mx251'):
            if project['libraries']:
                result = dict(status='UNSUPPORTED_KEIL_OMF', reason=', '.join(project['libraries']))
            elif not project['cSources']:
                # Invoke the shipped assembler on the original A251 dialect and
                # retain its diagnostics; do not pretend C codegen was measured.
                meta = read(out.parent/'original-inputs.json')
                for p in (out.parent/'native').iterdir(): shutil.copy2(p, out/p.name)
                assembler = Path(config['core'])/'toolchain/bin/sdas251.exe'
                matrix.run(out, 'assemble', [assembler, '-plosgff', meta['assembly'][0]], config)
                result = dict(status='ASSEMBLED_ONLY', reason='No compiler firmware result for pure A251 source')
            elif project.get('preparationError'):
                result = dict(status='INPUT_PREPARATION_FAILED', error=project['preparationError'])
            else:
                result = matrix.build_mx(config, project, out)
        elif suite.startswith('keil'):
            result = matrix.build_keil(config, project, out)
        else:
            if project['libraries']:
                result = dict(status='UNSUPPORTED_KEIL_OMF', reason=', '.join(project['libraries']))
            else:
                local = dict(config, stcOptions=dict(config['stcOptions'], mathLib=suite.removeprefix('stc-')))
                result = matrix.build_stc(local, project, out)
                result['mathLib'] = local['stcOptions']['mathLib']
    except Exception as error:
        (out/'exception.txt').write_text(traceback.format_exc(), encoding='utf-8')
        result = dict(status='FAILED', error=str(error))
    result.update(id=project['id'], suite=suite, wallSeconds=time.monotonic()-started,
                  hardwareExecuted=False, firmwareExecutionTime=None, inputManifest=project['sourceManifest'])
    save(out/'result.json', result)
    detail = str(result.get('payloadBytes', result.get('error', result.get('reason', ''))))
    print(project['id'], suite, result['status'], detail[:130].replace('\n', ' '), flush=True)
    return result


def report(config):
    root = Path(config['output'])
    rows=[]
    for project in read(root/'inputs.json')['projects']:
        suites={p.parent.name:read(p) for p in sorted((root/'projects'/project['id']).glob('*/result.json'))}
        row=dict(id=project['id'], name=project['name'], group=project['group'], suites=suites)
        mx=suites.get('mx251',{}).get('payloadBytes')
        for suite in ('keil','stc-soft','stc-mdu32'):
            value=suites.get(suite,{}).get('payloadBytes')
            row['mxOver'+suite+'Percent']=100*mx/value if mx and value else None
        rows.append(row)
    save(root/'comparison.json', dict(inventory=read(root/'inventory.json'), rows=rows,
         counts={s:dict(collections.Counter(r['suites'][s]['status'] for r in rows if s in r['suites']))
                 for s in sorted({s for r in rows for s in r['suites']})},
         hardwareExecuted=False, runtimeMeasured=False))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--config', type=Path, required=True)
    ap.add_argument('--ids', default='all')
    ap.add_argument('--suites', default='mx251,keil,stc-soft,stc-mdu32')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--prepare-only', action='store_true')
    ap.add_argument('--report-only', action='store_true')
    args=ap.parse_args()
    config=read(args.config)
    projects=prepare(config)
    if args.report_only:
        report(config)
        return
    if args.prepare_only: return
    if args.ids != 'all':
        ids=set(args.ids.split(','))
        projects=[p for p in projects if p['id'] in ids]
        assert {p['id'] for p in projects} == ids
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures=[pool.submit(one, config, p, s) for p in projects for s in args.suites.split(',')]
        for f in concurrent.futures.as_completed(futures):f.result()
    report(config)


if __name__=='__main__':main()
