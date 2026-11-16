"""Rebuild a frozen SDK cohort with one MX251 policy and two reference compilers.

The project corpus and its pre-existing dialect adapters are external, hash-pinned
inputs. This runner does not optimize or rewrite SDK algorithms.
"""
from pathlib import Path
import argparse
import concurrent.futures
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import time
import traceback
import xml.etree.ElementTree as ET


def read(path):
    return json.loads(Path(path).read_text('utf-8-sig'))


def save(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')


def pin(path):
    path = Path(path).resolve()
    return dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def decode(path):
    data = Path(path).read_bytes()
    for encoding in ('utf-8-sig', 'gb18030', 'cp1252'):
        try:
            return data.decode(encoding).replace('\r\n', '\n')
        except UnicodeDecodeError:
            pass
    raise ValueError('Cannot decode ' + str(path))


def image(path, code_min=0xff0000):
    memory, base, end = {}, 0, False
    for line in Path(path).read_text().splitlines():
        raw = bytes.fromhex(line[1:])
        assert line.startswith(':') and len(raw) == raw[0] + 5 and sum(raw) % 256 == 0
        kind = raw[3]
        if kind == 4:
            base = int.from_bytes(raw[4:6], 'big') << 16
        elif kind == 2:
            base = int.from_bytes(raw[4:6], 'big') << 4
        elif kind == 0:
            at = base + int.from_bytes(raw[1:3], 'big')
            if at < 65536:
                at += 0xff0000
            for i, value in enumerate(raw[4:-1]):
                assert code_min <= at + i < 0x1000000
                assert at + i not in memory or memory[at + i] == value
                memory[at + i] = value
        elif kind == 1:
            end = True
        else:
            raise ValueError('Unsupported HEX record ' + str(kind))
    assert end and memory and 0xff0000 in memory
    span = max(memory) - min(memory) + 1
    return dict(payloadBytes=len(memory), spanBytes=span, holesBytes=span-len(memory), hex=pin(path)), memory


def run(out, label, argv, config, timeout=300, keil=False):
    argv = list(map(str, argv))
    env = {k: v for k, v in os.environ.items()
           if not k.upper().startswith(('MX251_', 'SDCC_'))
           and k.upper() not in ('SDCCLIB', 'SDCCDIR', 'CPATH', 'C_INCLUDE_PATH')}
    env.update(C251LIB=str(Path(config['keil'])/'LIB'), C251INC=str(Path(config['keil'])/'INC'))
    started = time.monotonic()
    with (out/(label+'.stdout.log')).open('xb') as stdout, (out/(label+'.stderr.log')).open('xb') as stderr:
        process = subprocess.Popen(argv, cwd=out, env=env, stdout=stdout, stderr=stderr)
        save(out/(label+'.command.json'), dict(argv=argv, pid=process.pid, state='RUNNING'))
        try:
            process.wait(timeout=timeout)
        finally:
            if process.poll() is None:
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True, timeout=20)
                process.wait(timeout=20)
    record = dict(argv=argv, exitCode=process.returncode, elapsedSeconds=time.monotonic()-started)
    save(out/(label+'.command.json'), record)
    output = (out/(label+'.stdout.log')).read_bytes() + (out/(label+'.stderr.log')).read_bytes()
    if process.returncode and not (keil and process.returncode == 1 and b'0 ERROR(S)' in output):
        raise RuntimeError(label + ': ' + output.decode('utf-8', errors='replace')[-3500:])
    return record


def prepare(config):
    root, corpus = Path(config['output']), Path(config['corpus'])
    rows = [r for r in read(config['ledger'])['rows']
            if r['temporaryAcceptanceLabel'] != '体积尚不可按同模型验收，耗时未知']
    assert len(rows) == 75 and len({r['id'] for r in rows}) == 75
    excluded = set(config.get('excludeIds', []))
    assert excluded <= {r['id'] for r in rows}, excluded
    rows = [r for r in rows if r['id'] not in excluded]
    overrides = config.get('inputOverrides', {})
    assert set(overrides) <= {r['id'] for r in rows}
    manifest = root/'inputs.json'
    if manifest.exists():
        assert read(manifest)['configuration'] == config
        return read(manifest)['projects']
    projects = []
    for row in rows:
        key = row['id']
        source, dest = corpus/'projects'/key, root/'projects'/key
        meta = read(source/'inputs.json')
        assert meta['model'] == '3' and not meta['libraries']
        dest.mkdir(parents=True)
        shutil.copy2(source/'inputs.json', dest/'original-inputs.json')
        native = dest/'native'
        native.mkdir()
        originals = []
        for item in meta['files']:
            p = source/'native'/item['name']
            assert pin(p)['sha256'] == item['copySha256'], (key, item['name'])
            shutil.copy2(p, native/p.name)
            originals.append(pin(native/p.name))
        adapted = source/('mx251-braces' if (source/'mx251-braces').is_dir() else 'mx251')
        mx = dest/'mx251'
        shutil.copytree(adapted/'src', mx/'src')
        for name in ('CMakeLists.txt', 'mx251.json', 'dialect.json'):
            shutil.copy2(adapted/name, mx/name)
        changes = []
        if key in overrides:
            files = {p.relative_to(dest).as_posix(): p for folder in (native, mx)
                     for p in folder.rglob('*') if p.is_file()}
            for item in overrides[key]['files']:
                target = files[item['relativePath']]
                replacement = pin(item['path'])
                assert replacement['sha256'] == item['sha256'], item
                before = pin(target)
                shutil.copy2(item['path'], target)
                changes.append(dict(relativePath=item['relativePath'], before=before, after=pin(target),
                                    replacement=replacement))
            save(dest/'input-overrides.json', dict(description=overrides[key]['description'], changes=changes))
            originals = [pin(native/Path(p['path']).name) for p in originals]
        projects.append(dict(id=key, name=row['name'], sourceManifest=pin(dest/'original-inputs.json'),
                             originalFiles=originals, mxInputOrigin=str(adapted),
                             inputOverrides=pin(dest/'input-overrides.json') if changes else None,
                             mxInputs=[pin(p) for p in sorted(mx.rglob('*')) if p.is_file()],
                             historicalKeilBytes=row.get('keilBytes'),
                             historicalCandidateBytes=row.get('candidateBytes'),
                             historicalCorrectnessGap=row.get('correctnessGap'), assembly=meta['assembly']))
    save(manifest, dict(configuration=config, projects=projects, ledger=pin(config['ledger']),
                        toolPins=[pin(p) for p in config['toolFiles']],
                        compiler=read(Path(config['core'])/'core.json')['compiler'],
                        hardwareExecuted=False, sourceAlgorithmsChanged=bool(overrides),
                        sourceInputsChanged=bool(overrides)))
    return projects


def build_mx(config, project, out):
    core = Path(config['core'])
    command = [config['pwsh'], '-NoProfile', '-File', config.get('mxEntry', core/'build/Build-CMake.ps1'),
               '-ProjectPath', out, '-CMake', config['cmake'], '-Ninja', config['ninja'],
               '-PowerShell', config['pwsh']] + config['mxSwitches']
    run(out, 'build', command, config, timeout=config.get('mxTimeoutSeconds', 1800))
    latest = out/'.build/mx251/latest'
    receipt = read(latest/'receipt.json')
    assert receipt['status'] == 'complete'
    assert receipt['compilerSha256'] == read(core/'core.json')['compiler']['sha256']
    stats, _ = image(latest/'firmware.hex')
    return dict(status='BUILT', **stats, receipt=pin(latest/'receipt.json'),
                coreBuild=pin(out/'.build/cmake-mx251-core/core-build.json'), model=receipt['model'],
                selectiveLeafAbi=receipt.get('selectiveLeafAbi'))


def build_keil(config, project, out):
    base = out.parent
    meta, kit = read(base/'original-inputs.json'), Path(config['keil'])
    for p in (base/'native').iterdir():
        shutil.copy2(p, out/p.name)
    opts = ET.fromstring(meta['options'])
    flags = ['XSMALL', 'ROM(LARGE)', f'OPTIMIZE({meta["optLevel"]},SPEED)', 'CODE', f'INCDIR(.,{kit/"INC"})']
    intr2 = opts.findtext('Target251Misc/uFrame4') == '0'
    if intr2:
        flags.append('INTR2')
    if opts.findtext('C251/uRentF') == '1':
        flags.append('FUNCTIONS(REENTRANT)')
    misc = opts.findtext('C251/VariousControls/MiscControls')
    if misc:
        flags.append(misc)
    if meta['defines']:
        flags.append('DEFINE('+meta['defines']+')')
    adapter = None
    if project['id'] in config['keilVectorAdapterIds']:
        spec = importlib.util.spec_from_file_location('vector_adapter', config['keilVectorAdapter'])
        adapter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(adapter)
    objects, entries = [], []
    # uVision permits per-file reentrancy overrides (for example putchar).
    project_xml = ET.parse(meta['project']['path'])
    file_reentrancy = {Path(f.findtext('FilePath').replace('\\', '/')).name.lower():
                      f.findtext('FileOption/File251/C251/uRentF')
                      for f in project_xml.findall('.//File') if f.findtext('FilePath')}
    for i, name in enumerate(meta['sources']):
        extra, found = [], []
        unit_flags = list(flags)
        reentrancy = file_reentrancy.get(name.lower())
        if reentrancy in ('0', '1'):
            unit_flags = [f for f in flags if not f.startswith('FUNCTIONS(')]
            if reentrancy == '1': unit_flags.append('FUNCTIONS(REENTRANT)')
        if adapter:
            run(out, f'preprocess-{i}', [kit/'BIN/C251.EXE', name] + unit_flags +
                [f'PREPRINTONLY(unit{i}.i)', f'PRINT(preprocess{i}.lst)'], config, keil=True)
            preprocessed = (out/f'unit{i}.i').read_bytes().decode('latin1')
            if re.search(r'\binterrupt\b', adapter.mask(preprocessed)):
                changed, found = adapter.translate(preprocessed)
            else:
                changed, found = preprocessed, []
            if found:
                shutil.copy2(out/name, out/(name+'.original'))
                (out/name).write_bytes(changed.encode('latin1'))
                extra.append('NOINTVECTOR')
        obj = f'unit{i}.obj'
        objects.append(obj)
        run(out, f'compile-{i}', [kit/'BIN/C251.EXE', name] + unit_flags + extra +
            [f'OBJECT({obj})', f'PRINT(unit{i}.lst)'], config, keil=True)
        listing = (out/f'unit{i}.lst').read_text('latin1')
        for entry in found:
            # C251 decorates reentrant definitions with ?_; use its listing.
            pattern = r'FUNCTION\s+('+re.escape(entry['symbol'])+r'(?:\?_)?)\s+\(BEGIN\)'
            symbols = set(re.findall(pattern, listing))
            assert len(symbols) == 1, entry
            entry['symbol'] = symbols.pop()
        entries.extend(found)
    for i, name in enumerate(meta['assembly']):
        obj = f'asmunit{i}.obj'
        objects.append(obj)
        run(out, f'asm-{i}', [kit/'BIN/A251.EXE', name, 'MODSRC', f'OBJECT({obj})', f'INCDIR({kit/"INC"})'] +
            (['INTR2'] if intr2 else []), config, keil=True)
    if entries:
        assert len({e['number'] for e in entries}) == len(entries)
        asm = []
        for e in entries:
            asm += ['EXTRN CODE ('+e['symbol']+')', 'CSEG AT 0%04XH' % (3+8*e['number']), 'LJMP '+e['symbol']]
        (out/'vectors.asm').write_text('\n'.join(asm+['END', '']), encoding='ascii')
        run(out, 'vectors', [kit/'BIN/A251.EXE', 'vectors.asm', 'MODSRC', 'CASE', 'OBJECT(vectors.obj)'] +
            (['INTR2'] if intr2 else []), config, keil=True)
        objects.append('vectors.obj')
    linkflags = ['CASE', config.get('keilClasses', 'CLASSES(EDATA(0-0x7FF),HDATA(0-0x7FF))')]
    misc = opts.findtext('Lx51/MiscControls') or ''
    if misc and not misc.startswith('//'):
        linkflags.append(misc)
    run(out, 'link', [kit/'BIN/L251.EXE', ','.join(objects + meta.get('libraries', [])), 'TO', 'firmware'] + linkflags, config, keil=True)
    run(out, 'hex', [kit/'BIN/OH251.EXE', 'firmware', 'HEXFILE(firmware.hex)'], config, keil=True)
    stats, memory = image(out/'firmware.hex', config.get('codeMin', 0xff0000))
    if entries:
        mapping = (out/'FIRMWARE.MAP').read_text('latin1')
        for e in entries:
            addresses = re.findall(r'^\s*([0-9A-F]+)H\s+CODE\s+---\s+'+re.escape(e['symbol'])+r'\s*$', mapping, re.M)
            assert len(addresses) == 1, e
            target, vector = int(addresses[0], 16), 0xff0003+8*e['number']
            assert [memory.get(vector+i) for i in range(3)] == [2, (target>>8)&255, target&255], e
            e.update(vector=vector, target=target)
        save(out/'vector-adaptation.json', dict(entries=entries, adapter=pin(config['keilVectorAdapter']),
                                              mechanism='NOINTVECTOR plus A251 original-address vectors'))
    return dict(status='BUILT', **stats, model='XSMALL', optimization='SPEED', optLevel=meta['optLevel'],
                vectorAdapter=bool(adapter), historicalBytesMatch=stats['payloadBytes']==project['historicalKeilBytes'])


def build_stc(config, project, out):
    meta = read(out.parent/'original-inputs.json')
    if meta['assembly']:
        return dict(status='UNSUPPORTED_ASSEMBLY_INPUT', reason='Official worker accepts C/header inputs; original absolute vector assembly is required.',
                    assembly=meta['assembly'])
    names = meta['sources'] + [f['name'] for f in meta['files'] if Path(f['name']).suffix.lower() == '.h']
    files = []
    for name in names:
        source = decode(out.parent/'native'/name)
        if name in meta['sources']:
            source = '#define __C251__ 1\n' + ''.join('#define '+s.strip().replace('=', ' ', 1)+'\n'
                          for s in meta['defines'].split(',') if s.strip()) + source
        files.append(dict(name=name, code=source))
    save(out/'request.json', dict(files=files, target=config.get('stcTarget', 'AI8051U'), opts=config['stcOptions']))
    run(out, 'build', [config['node'], config['stcAdapter'], out/'request.json', out/'response.json', config['stcVendor']], config)
    response = read(out/'response.json')
    result = response.get('result', {})
    if not result.get('success'):
        raise RuntimeError(str(result.get('error') or response.get('error') or result.get('errors')))
    (out/'firmware.hex').write_text(result['hex']+'\n', encoding='ascii')
    (out/'firmware.asm').write_text(result['asm'], encoding='utf-8')
    stats, _ = image(out/'firmware.hex')
    return dict(status='BUILT', **stats, buildVer=response.get('buildVer'), memory=result.get('memReport'),
                diagnostics={k: v for k, v in result.items() if re.search('warn|error|note|diag', k, re.I)},
                equivalence='NOT_ESTABLISHED', model='vendor-24-bit-pointer', fullMs=response.get('fullMs'))


def one(config, project, suite):
    out = Path(config['output'])/'projects'/project['id']/suite
    out.mkdir(exist_ok=True)
    result_file = out/'result.json'
    if result_file.exists():
        return read(result_file)
    started = time.monotonic()
    try:
        result = {'mx251': build_mx, 'keil': build_keil, 'stc': build_stc}[suite](config, project, out)
    except Exception as error:
        (out/'exception.txt').write_text(traceback.format_exc(), encoding='utf-8')
        result = dict(status='FAILED', error=str(error))
    result.update(id=project['id'], suite=suite, wallSeconds=time.monotonic()-started,
                  hardwareExecuted=False, firmwareExecutionTime=None, inputManifest=project['sourceManifest'])
    save(result_file, result)
    print(project['id'], suite, result['status'], result.get('payloadBytes', str(result.get('error', ''))[-180:]), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--ids', default='all')
    parser.add_argument('--suites', default='mx251,keil,stc')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    config = read(args.config)
    projects = prepare(config)
    if args.ids != 'all':
        selected = args.ids.split(',')
        projects = [p for p in projects if p['id'] in selected]
        assert {p['id'] for p in projects} == set(selected)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(one, config, project, suite) for project in projects for suite in args.suites.split(',')]
        for future in concurrent.futures.as_completed(futures):
            future.result()


if __name__ == '__main__':
    main()
