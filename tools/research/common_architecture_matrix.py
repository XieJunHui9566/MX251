"""Reuse frozen compiler inputs/references to measure common architecture policy."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import importlib.util
from pathlib import Path
import shutil
import subprocess

import stc32g_box_matrix as box
import unified_sdk_matrix as matrix

REPO = Path(__file__).resolve().parents[2]


def prepare(root, baseline, source):
    root.mkdir(parents=True, exist_ok=True)
    package = root/'candidate'
    core = package/'core'
    if not core.exists():
        for name in ('build', 'profiles', 'toolchain', 'compat'):
            shutil.copytree(baseline/'core'/name, core/name)
        shutil.copytree(REPO/'core/build', core/'build', dirs_exist_ok=True)
        shutil.copytree(REPO/'core/compat', core/'compat', dirs_exist_ok=True)
        shutil.copy2(REPO/'Build-Core.ps1', package/'Build.ps1')
        registry = matrix.pin(core/'build/qualified/TargetContracts.json')
        for path in (core/'profiles').glob('*/profile.lock.json'):
            value = matrix.read(path)
            value['targetContract']['registrySha256'] = registry['sha256']
            matrix.save(path, value)
        value = matrix.read(baseline/'core/core.json')
        value.update(parentCore=matrix.pin(baseline/'core/core.json'),
                     buildId='mx251-common-architecture-20261114', status='DEVELOPMENT_CANDIDATE')
        matrix.save(core/'core.json', value)
        matrix.save(root/'candidate-files.json', dict(files=[matrix.pin(p) for folder in ('build','compat','profiles')
                    for p in sorted((core/folder).rglob('*')) if p.is_file()],
                    sourceCommit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=REPO,text=True).strip()))
    cfg = dict(matrix.read(source/'config.json'), output=str(root), core=str(core),
               mxEntry=str(package/'Build.ps1'))
    matrix.save(root/'config.json', cfg)
    host = str(baseline/'host').replace("'", "''")
    (root/'Build-Optimized.ps1').write_text("""#requires -Version 7.0
param([Parameter(Mandatory=$true)][string]$ProjectPath,[switch]$C251Compatibility)
$ErrorActionPreference='Stop'
$hostTools='"""+host+"""'
& (Join-Path $PSScriptRoot 'candidate/Build.ps1') -ProjectPath $ProjectPath `
 -C251Compatibility:$C251Compatibility -CMake (Join-Path $hostTools 'cmake/bin/cmake.exe') `
 -Ninja (Join-Path $hostTools 'ninja/ninja.exe') -PowerShell (Join-Path $hostTools 'powershell/pwsh.exe')
""", encoding='utf-8')
    spec = importlib.util.spec_from_file_location('vector_aliases', REPO/'core/compat/keil/vector_aliases.py')
    aliases = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(aliases)
    projects = [p for p in matrix.read(source/'inputs.json')['projects'] if p['cSources']]
    for project in projects:
        src = source/'projects'/project['id']
        dst = root/'projects'/project['id']
        if (dst/'mx251').exists():
            continue
        shutil.copytree(src/'mx251/src', dst/'mx251/src')
        for name in ('mx251.json', 'CMakeLists.txt', 'dialect.json'):
            shutil.copy2(src/'mx251'/name, dst/'mx251'/name)
        meta = matrix.read(src/'original-inputs.json')
        changes = []
        cmake = dst/'mx251/CMakeLists.txt'
        text = cmake.read_text('utf-8')
        for name in meta['assembly']:
            old = dst/'mx251/src'/name.lower()
            if not old.exists():
                continue
            original = src/'native'/name
            translated = aliases.translate(original.read_text('gb18030'), 'mx251_'+Path(name).stem.lower())
            if translated is None:
                continue
            new = old.with_name(old.name+'.c')
            new.write_text(translated, encoding='utf-8')
            text = text.replace('"src/'+old.name+'"', '"src/'+new.name+'"')
            changes.append(dict(original=matrix.pin(original), before=matrix.pin(old), after=matrix.pin(new)))
            # Old source retained as evidence; only the translated source is built.
        cmake.write_text(text, encoding='utf-8')
        matrix.save(dst/'input-provenance.json', dict(frozenSource=str(src), redirects=changes,
                    inputs=[matrix.pin(p) for p in sorted((dst/'mx251/src').iterdir()) if p.is_file()],
                    sourceAlgorithmsChanged=False))
    matrix.save(root/'inputs.json', dict(projects=projects, reference=str(source),
                sourceAlgorithmsChanged=False, hardwareExecuted=False))
    return cfg, projects


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', required=True, type=Path)
    p.add_argument('--baseline', required=True, type=Path)
    p.add_argument('--reference', required=True, type=Path)
    p.add_argument('--ids', default='all')
    p.add_argument('--jobs', type=int, default=4)
    a = p.parse_args()
    config, projects = prepare(a.root, a.baseline, a.reference)
    selected = [p for p in projects if a.ids=='all' or p['id'] in a.ids.split(',')]
    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        tasks = {pool.submit(box.one,config,p,'mx251'): p for p in selected}
        for f in as_completed(tasks):
            r=f.result()
            print(tasks[f]['id'],r['status'],r.get('payloadBytes',''),flush=True)


if __name__=='__main__':
    main()
