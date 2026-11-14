"""Compile every STC32G V9.6 uVision project with the packaged core and references.

The archive is extracted separately. Source algorithms remain unchanged; the
existing C251 spelling adapter and numeric vector adapter are recorded inputs.
No vendor binary library or assembly translation unit is silently discarded.
"""
import argparse
import concurrent.futures
import importlib.util
from pathlib import Path
import re
import shutil
import time
import traceback
import xml.etree.ElementTree as ET

import sdk_coverage_matrix as coverage
import unified_sdk_matrix as matrix

read, save, pin = matrix.read, matrix.save, matrix.pin
REPO = Path(__file__).resolve().parents[2]
SUITES = ('mx251', 'keil', 'stc-soft', 'stc-mdu32')


def prepare(root, baseline, corpus, refresh=False):
    manifest = root/'inputs.json'
    if manifest.exists() and not refresh:
        return read(root/'config.json'), read(manifest)['projects']
    previous = {p['id']:p for p in read(manifest)['projects']} if manifest.exists() else {}
    prep, dialect = coverage.adapters(corpus)
    prep.ROOT = (root/'official').resolve()
    spec = importlib.util.spec_from_file_location('vector_aliases', REPO/'core/compat/keil/vector_aliases.py')
    aliases = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(aliases)
    config = dict(output=str(root), core=str(baseline/'core'),
        mxEntry=str(baseline/'Build.ps1'), mxSwitches=['-C251Compatibility'],
        pwsh=str(baseline/'host/powershell/pwsh.exe'),
        cmake=str(baseline/'host/cmake/bin/cmake.exe'), ninja=str(baseline/'host/ninja/ninja.exe'),
        keil='E:/MCU/Keil5/C251', node='E:/nodejs/node.exe',
        keilClasses='CLASSES(EDATA(0-0xFFF),HDATA(0-0xFFF))', codeMin=0xfe0000,
        keilVectorAdapter=str(REPO/'tools/research/c251_vector_adapter.py'),
        keilVectorAdapterIds=[], stcAdapter=str(REPO/'tools/research/stcide_compile.mjs'),
        stcVendor=str(root/'stc-official/vendor'), stcTarget='STC32G12K128',
        stcOptions=dict(eepromSize=0, mathLib='soft', stringEncoding='gb2312', optimize=True, ptrMode='24'))
    rows = []
    for p in sorted(prep.ROOT.rglob('*.uvproj')):
        for target in ET.parse(p).findall('./Targets/Target'):
            files = [dict(path=str((p.parent/f.findtext('FilePath')).resolve()), type=f.findtext('FileType'))
                     for f in target.findall('./Groups/Group/Files/File') if f.findtext('FilePath')]
            rows.append(dict(id=f'g{len(rows):03d}', name=str(p.relative_to(prep.ROOT).parent),
                project=str(p), target=target.findtext('TargetName'),
                device=target.findtext('TargetOption/TargetCommonOption/Device'),
                files=files, cSources=sum(f['type']=='1' for f in files)))
    projects = []
    for row in rows:
        old = previous.get(row['id'])
        if old and not old.get('preparationError') and not old.get('refreshInputs') and row['cSources']:
            config['keilVectorAdapterIds'].append(row['id'])
            projects.append(old)
            continue
        base = root/'projects'/row['id']
        native = base/'native'
        native.mkdir(parents=True, exist_ok=True)
        project = dict(row, historicalKeilBytes=None)
        try:
            target, files, names, flat = prep.collect(row)
            # A few projects expect the device header installed in Keil's INC.
            # Use the header distributed in this same official archive for all tools.
            if not any(n.lower()=='stc32g.h' for n in names.values()) and any(
                    s and re.search(r'#\s*include\s*[<"]stc32g\.h[>"]',s,re.I) for s in flat.values()):
                header = prep.ROOT/'COMM/STC32G.H'
                files[header] = prep.decode(header)
                names[header] = header.name
                flat[header.name] = files[header]
            # Flatten A251 $include paths just as collect() flattens C includes.
            for path in list(files):
                if path.suffix.lower() not in ('.asm','.a51'):continue
                def include(match):
                    dep = (path.parent/match[1].replace('\\','/')).resolve()
                    assert dep.is_relative_to(prep.ROOT) and dep.is_file(), dep
                    files[dep] = prep.decode(dep)
                    names[dep] = dep.name
                    flat[dep.name] = files[dep]
                    return '$include ('+dep.name+')'
                flat[names[path]] = re.sub(r'\$include\s*\(([^)]+)\)',include,flat[names[path]],flags=re.I)
            options = target.find('TargetOption/Target251')
            assert options.findtext('Target251Misc/MemoryModel') == '3'
            for path, source in files.items():
                (native/names[path]).write_bytes(path.read_bytes() if source is None else flat[names[path]].encode('gb18030'))
            meta = dict(id=row['id'], name=row['name'], project=pin(row['project']),
                sources=[names[Path(f['path'])] for f in row['files'] if f['type']=='1'],
                assembly=[names[Path(f['path'])] for f in row['files'] if f['type']=='2'],
                libraries=[names[p] for p in files if p.suffix.lower()=='.lib'],
                files=[dict(name=names[p], source=str(p), sourceSha256=prep.sha(p), copySha256=prep.sha(native/names[p])) for p in files],
                options=ET.tostring(options, encoding='unicode'),
                defines=options.findtext('C251/VariousControls/Define') or '', model='3',
                optLevel=int(options.findtext('C251/Optim')), sizeSpeed=options.findtext('C251/SizSpd'))
            save(base/'original-inputs.json', meta)
            project.update(assembly=meta['assembly'], libraries=meta['libraries'],
                           sourceManifest=pin(base/'original-inputs.json'))
            if row['cSources']:
                config['keilVectorAdapterIds'].append(row['id'])
                mx = base/'mx251'
                src = mx/'src'
                src.mkdir(parents=True, exist_ok=True)
                unknown_libs = [n for n in meta['libraries'] if n.lower() != 'stc32_mdu32_v1.1.lib']
                if unknown_libs:
                    project['mxUnsupportedLibraries'] = unknown_libs
                texts = {p.name:prep.decode(p) for p in native.iterdir() if p.suffix.lower() in ('.c','.h','.asm','.a51')}
                allcode = '\n'.join(prep.code(s) for s in texts.values())
                regs = {n:int(a,0) for n,a in re.findall(r'\bsfr\s+(\w+)\s*=\s*(0x[\da-fA-F]+|\d+)\s*;', allcode)}
                extended = sorted({n for n,r,b in re.findall(r'\bsbit\s+(\w+)\s*=\s*(\w+)\s*\^\s*([0-7])\s*;', allcode) if regs[r]%8})
                converted_names, changes = {}, []
                for name, source in texts.items():
                    newname = name.lower()
                    if name in meta['assembly']:
                        converted = aliases.translate(source, 'mx251_'+Path(name).stem.lower())
                        if converted is not None:
                            newname += '.c'
                        else:
                            converted = dialect['asm_dialect'](source)
                    else:
                        converted = dialect['mx_dialect'](source, regs, extended, [])
                    converted = prep.INC.sub(lambda m:m[1]+m[2].lower()+m[3], converted)
                    (src/newname).write_bytes(converted.encode('gb18030'))
                    converted_names[name] = newname
                    changes.append(dict(source=pin(native/name), adapted=pin(src/newname)))
                if not (src/'intrins.h').exists():
                    (src/'intrins.h').write_text('#define _nop_() do { __asm nop __endasm; } while (0)\n')
                # A compiler runtime selection, not an SDK-specific algorithm edit.
                runtime = 'native-int-mdu32' if meta['libraries'] == ['STC32_MDU32_V1.1.LIB'] else (
                    'native-math' if re.search(r'\b(float|double)\b', allcode) else 'standard')
                settings = dict(schema=2,target='STC32G12K128',model='small',optimization='speed',
                    backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime=runtime,
                    defaultDataSpace='edata',defines=[])
                save(mx/'mx251.json', settings)
                cm = 'cmake_minimum_required(VERSION 3.25)\nproject(stc32g_box NONE)\ninclude("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\n'
                cm += 'target_sources(app INTERFACE '+' '.join('"src/'+converted_names[n]+'"' for n in meta['sources']+meta['assembly'])+')\n'
                cm += 'target_include_directories(app INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")\n'
                cm += 'target_compile_definitions(app INTERFACE __C251__=1 edata=__edata near=__edata '+meta['defines'].replace(',', ' ')+')\nmx251_firmware(app)\n'
                (mx/'CMakeLists.txt').write_text(cm,encoding='utf8')
                save(mx/'dialect.json',dict(files=changes,extendedBits=extended,runtime=runtime,
                    replacedArithmeticLibraries=[n for n in meta['libraries'] if n not in unknown_libs],sourceAlgorithmsChanged=False))
        except Exception as error:
            project['preparationError'] = str(error)
            (base/'preparation-error.txt').write_text(traceback.format_exc(),encoding='utf8')
        projects.append(project)
    save(root/'inventory.json', rows)
    save(root/'config.json',config)
    toolfiles = [Path(config['core'])/'toolchain/bin/sdcc.exe', Path(config['mxEntry']),
        Path(__file__), REPO/'tools/research/unified_sdk_matrix.py', corpus/'build.py',corpus/'prepare.py',
        Path(config['keilVectorAdapter']), REPO/'core/compat/keil/vector_aliases.py']
    toolfiles += list((root/'stc-official/vendor').rglob('*.js'))
    toolfiles += [Path(config['keil'])/'BIN'/n for n in ('C251.EXE','A251.EXE','L251.exe','OH251.EXE')]
    save(manifest,dict(projects=projects,source=read(root/'source.json'),toolPins=[pin(p) for p in toolfiles],
        hardwareExecuted=False,sourceAlgorithmsChanged=False))
    return config, projects


def one(config, project, suite):
    out = Path(config['output'])/'projects'/project['id']/suite
    out.mkdir(exist_ok=True)
    if (out/'result.json').exists():
        return read(out/'result.json')
    started = time.monotonic()
    try:
        if project.get('preparationError') and (suite=='mx251' or not project.get('sourceManifest')):
            result = dict(status='INPUT_PREPARATION_FAILED', error=project['preparationError'])
        elif suite=='mx251':
            if project.get('mxUnsupportedLibraries'):
                result = dict(status='UNSUPPORTED_KEIL_OMF', reason=', '.join(project['mxUnsupportedLibraries']))
            elif not project['cSources']:
                # Exercise the shipped assembler once for every official source.
                for p in (out.parent/'native').iterdir():shutil.copy2(p,out/p.name)
                matrix.run(out,'assemble',[Path(config['core'])/'toolchain/bin/sdas251.exe','-plosgff',project['assembly'][0]],config)
                result = dict(status='ASSEMBLED_ONLY',reason='No linked C firmware in a pure A251 project')
            else:
                result = matrix.build_mx(config,project,out)
        elif suite=='keil':
            result = matrix.build_keil(config,project,out)
        elif any(n.lower() != 'stc32_mdu32_v1.1.lib' for n in project['libraries']):
            result = dict(status='UNSUPPORTED_KEIL_OMF',reason=', '.join(project['libraries']))
        else:
            local = dict(config,stcOptions=dict(config['stcOptions'],mathLib=suite.removeprefix('stc-')))
            result = matrix.build_stc(local,project,out)
            result['replacedArithmeticLibraries'] = project['libraries']
    except Exception as error:
        (out/'exception.txt').write_text(traceback.format_exc(),encoding='utf8')
        result = dict(status='FAILED',error=str(error))
    result.update(id=project['id'],suite=suite,wallSeconds=time.monotonic()-started,
        hardwareExecuted=False,firmwareExecutionTime=None,inputManifest=project.get('sourceManifest'))
    save(out/'result.json',result)
    print(project['id'],suite,result['status'],result.get('payloadBytes',str(result.get('error',''))[-100:]),flush=True)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root',required=True,type=Path)
    ap.add_argument('--baseline',required=True,type=Path)
    ap.add_argument('--corpus',required=True,type=Path)
    ap.add_argument('--ids',default='all')
    ap.add_argument('--suites',default=','.join(SUITES))
    ap.add_argument('--jobs',type=int,default=3)
    ap.add_argument('--refresh-preparation',action='store_true')
    args = ap.parse_args()
    config, projects = prepare(args.root,args.baseline,args.corpus,args.refresh_preparation)
    print('inventory',len(projects),'C',sum(bool(p['cSources']) for p in projects),
          'preparation errors',[(p['id'],p['preparationError']) for p in projects if p.get('preparationError')],flush=True)
    selected = projects if args.ids=='all' else [p for p in projects if p['id'] in args.ids.split(',')]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(one,config,p,s) for p in selected for s in args.suites.split(',') if s]
        for future in concurrent.futures.as_completed(futures):future.result()


if __name__=='__main__':
    main()
