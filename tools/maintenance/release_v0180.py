"""Package the qualified v0.180 compiler without rebuilding or replacing it."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

REPO = Path(__file__).resolve().parents[2]
VERSION = '0.180'
IDE_VERSION = '0.0180'  # StudioX's existing compiler identity requires four digits.
COMPONENT_VERSION = '0.0.180'


def sha(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def read(p):
    return json.loads(p.read_text(encoding='utf-8-sig'))


def save(p, value):
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def files(root):
    return {p.relative_to(root).as_posix(): sha(p) for p in sorted(root.rglob('*')) if p.is_file()}


def stage(a):
    frozen = read(a.candidate.parent/'stage.json')
    for item in frozen['files']:
        assert sha(a.candidate/item['path']) == item['sha256'], item['path']
    shutil.copytree(a.candidate, a.output)
    core = a.output/'core'
    shutil.copytree(a.host, a.output/'host')
    # Examples come from Git, never from previous generated build directories.
    names = subprocess.check_output(['git','ls-files','core','examples'],cwd=REPO,text=True).splitlines()
    added_sources = 0
    for name in names:
        dest = a.output/name
        if not dest.is_file():
            dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(REPO/name,dest)
            if name.startswith('core/'): added_sources += 1
    for name in ['Build.cmd','LICENSE','THIRD_PARTY.md']:
        shutil.copy2(REPO/name,a.output/name)
    shutil.copy2(REPO/'core/build/Release.cmake',core/'build/Release.cmake')
    build_changes = ['build/qualified/Build.ps1','build/qualified/CompileReuse.ps1',
        'build/qualified/BuildLatest.ps1','build/Build-CMake.ps1']
    for name in build_changes:
        shutil.copy2(REPO/'core'/name,core/name)
    (a.output/'VERSION').write_text(VERSION+'\n')
    for profile in (core/'profiles').iterdir():
        lock = profile/'profile.lock.json'
        data = read(lock)
        data['mx251Version'] = IDE_VERSION
        save(lock,data)
    old = read(core/'core.json')
    source_manifest=core/'sources/compiler-files.json'
    save(source_manifest,dict(files=[dict(path=p.relative_to(core).as_posix(),sha256=sha(p))
        for p in sorted((core/'sources/compiler').rglob('*')) if p.is_file()]))
    metadata = {k:old[k] for k in ['schema','name','buildChain','host','entry','buildScript',
        'toolchain','profiles','compiler','assembler','mathLibrary','sourceManifest','sourceManifestSha256']}
    metadata.update(version=VERSION,sourceVersion=VERSION,buildId='mx251-v0.180',
        displayName='MX251 v0.180',releaseChannel='engineering',status='ENGINEERING_RELEASE',
        compilerSourceCommit=frozen['sourceCommit'],sourceCommit=frozen['sourceCommit'],
        projectModule='build/Release.cmake',repository='https://github.com/XieJunHui9566/MX251',
        ideCompatibilityVersion=IDE_VERSION,qualificationRecord='../docs/qualification.json',
        qualification='See qualification.json for measured scope; Keil runtime 110% is not yet qualified.',
        lstQualified=False)
    metadata['sourceManifestSha256']=sha(source_manifest)
    save(core/'core.json',metadata)
    # All executable and library bytes must match the qualified candidate.
    changed=[]
    for item in frozen['files']:
        if sha(a.output/item['path']) != item['sha256']:
            changed.append(Path(item['path']).as_posix())
    assert set(changed)=={'core/core.json','core/sources/compiler-files.json',
        *['core/'+name for name in build_changes],
        *[f'core/profiles/{p.name}/profile.lock.json' for p in (core/'profiles').iterdir()]}
    save(a.output/'packaging.json',dict(version=VERSION,compilerSha256=frozen['compilerSha256'],
        implementationCommit=frozen['sourceCommit'],metadataOnlyChanges=[x for x in changed if x.endswith('.json')],
        buildTimeoutChange=dict(file='core/build/qualified/Build.ps1',fromSeconds=120,toSeconds=600,
            compilerAndFlagsUnchanged=True),
        ideSessionFix='CompileReuse excludes IDE session leases; compiler inputs/options unchanged',
        totalBuildTimeoutSeconds=3600,sourceAndLicenseFilesAdded=added_sources,
        additionalBuildEntry='core/build/Release.cmake',binariesRebuilt=False))
    for name in ('README.md','CHANGELOG.md'):
        shutil.copy2(REPO/name,a.output/name)
    docs=a.output/'docs'
    docs.mkdir(exist_ok=True)
    for name in ('RELEASE-v0.180.md','BUILD.md','ARCHITECTURE.md','EDATA.md',
                 'EDATA-SCALAR-BACKEND.md','REGISTER-AFFINITY-20261116.md'):
        shutil.copy2(REPO/'docs'/name,docs/name)
    for name in ('INTEGER-SEMANTICS-20261115.json','INTEGER-SEMANTICS-20261115.md'):
        if (a.output/name).is_file():
            (a.output/name).rename(docs/name)
    print('STAGED',a.output,flush=True)


def ide(a):
    a.output.mkdir(parents=True,exist_ok=False)
    component=a.output/'component'
    shutil.copytree(a.distribution/'core',component/'sdk')
    shutil.copytree(a.distribution/'host/powershell',component/'powershell')
    shutil.copytree(a.distribution/'docs',component/'docs')
    for name in ['VERSION','LICENSE','THIRD_PARTY.md','README.md']:
        shutil.copy2(a.distribution/name,component/name)
    sdk=component/'sdk'
    targets={}
    for profile in (sdk/'profiles').iterdir():
        lock=read(profile/'profile.lock.json')
        targets[lock['target']]=dict(profile='profiles/'+profile.name,
            profileLockSha256=sha(profile/'profile.lock.json'),compilerSha256=lock['compilerOverlay']['sha256'])
    save(sdk/'sdk.json',dict(schema=1,name='MX251',version=IDE_VERSION,publicVersion=VERSION,
        status='engineering-release',host='win-x64',buildChain=['CMake','Ninja','MX251'],
        projectModule='build/Release.cmake',buildScript='build/qualified/Build.ps1',
        targets=targets,files=files(sdk)))
    save(component/'toolset.json',dict(formatVersion=1,id='studiox.mx251',version=COMPONENT_VERSION,
        host='win-x64',compilerId='mx251-v'+IDE_VERSION,displayName='MX251 v'+VERSION,purpose='mx251',
        componentVersions={'mx251':VERSION},executables=dict(pwsh='powershell/pwsh.exe',
            sdcc='sdk/profiles/AI8051U-34K64-32/compiler/sdcc.exe',sdar='sdk/toolchain/bin/sdar.exe',
            sdas251='sdk/toolchain/bin/sdas251.exe',sdld='sdk/toolchain/bin/sdld.exe',
            build251='sdk/build/qualified/Build.ps1'),
        resourceDirectories={'mx251-sdk':'sdk','sdcc':'sdk/toolchain',
            **{'profile-'+k:'sdk/'+v['profile'] for k,v in targets.items()}},sha256=files(component)))
    records=[]
    for source in sorted(a.packs.iterdir()):
        if not (source/'manifest.json').is_file():
            continue
        pack=a.output/'pack-sources'/source.name
        shutil.copytree(source,pack)
        manifest=read(pack/'manifest.json')
        manifest.update(version='0.2.180',displayName=manifest['id']+' / MX251 v'+VERSION)
        for dev in manifest['devices']:
            dev.update(toolsetVersion=COMPONENT_VERSION,compilerId='mx251-v'+IDE_VERSION)
            for dep in dev.get('developmentComponents',[]):
                if dep['id']=='studiox.mx251':
                    dep.update(version=COMPONENT_VERSION,compilerId='mx251-v'+IDE_VERSION)
        save(pack/'manifest.json',manifest)
        provenance=read(pack/'provenance.json')
        save(pack/'provenance.json',dict(version=VERSION,
            compilerSha256=read(sdk/'core.json')['compiler']['sha256'],
            deviceSources=provenance,hardwareExecuted=False))
        # The versioned compiler supplies defaults; preserve project-level controls.
        cmake=pack/'CMakeLists.txt'
        text=cmake.read_text(encoding='utf-8-sig')
        text=text.replace('set(MX251_WIDE_VALUE_LIMIT 512 CACHE','set(MX251_WIDE_VALUE_LIMIT 1024 CACHE')
        cmake.write_text(text,encoding='utf-8')
        (pack/'README.txt').write_text('MX251 v0.180 配套器件描述及模板。\n使用 studiox.mx251 0.0.180 与 studiox.cmake 4.4.0。\n',encoding='utf-8')
        save(pack/'compiler-release.json',dict(version=VERSION,compilerSha256=read(sdk/'core.json')['compiler']['sha256'],
            parentManifestSha256=sha(source/'manifest.json'),deviceSourcesUnchanged=True))
        archive=a.output/(manifest['id']+'-0.2.180.mcupack')
        hashes=files(pack)
        with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for name in hashes:
                z.write(pack/name,name)
            z.writestr('files.sha256.json',json.dumps(hashes,ensure_ascii=False,indent=2)+'\n')
        records.append(dict(file=archive.name,sha256=sha(archive)))
    save(a.output/'delivery.json',dict(version=VERSION,componentVersion=COMPONENT_VERSION,
        compilerId='mx251-v'+IDE_VERSION,packs=records))
    print('STUDIOX STAGED',a.output,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='command',required=True)
    s=sub.add_parser('stage')
    for n in ['candidate','host','output']:s.add_argument('--'+n,type=Path,required=True)
    s=sub.add_parser('ide')
    for n in ['distribution','packs','output']:s.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args()
    (stage if a.command=='stage' else ide)(a)
