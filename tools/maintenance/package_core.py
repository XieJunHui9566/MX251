"""Stage a portable development core from qualified binaries and Git sources."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

REPO = Path(__file__).resolve().parents[2]


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8', newline='\n')


def stage(candidate, host, output, build_id):
    output.mkdir(parents=True, exist_ok=False)
    core = output/'core'
    shutil.copytree(candidate, core)
    tracked = subprocess.check_output(['git', 'ls-files', '-z', 'core', 'examples'], cwd=REPO).decode().split('\0')
    for name in filter(None, tracked):
        if name == 'core/core.json' or name.endswith('/profile.lock.json'):
            continue
        destination = output/name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(REPO/name, destination)
    shutil.copytree(host, output/'host')
    for source, name in [('Build-Core.ps1', 'Build.ps1'), ('Build.cmd', 'Build.cmd'),
                         ('LICENSE', 'LICENSE'), ('THIRD_PARTY.md', 'THIRD_PARTY.md')]:
        shutil.copy2(REPO/source, output/name)
    (output/'docs').mkdir()
    for name in ['ABI4-BACKEND-CHOICE.md', 'ABI4-BACKEND-CHOICE-RESULTS.json']:
        shutil.copy2(REPO/'docs'/name, output/'docs'/name)
    manifest = dict(files=[dict(path=p.relative_to(core).as_posix(), sha256=sha(p))
                          for p in sorted((core/'sources/compiler').rglob('*')) if p.is_file()])
    save(core/'sources/compiler-files.json', manifest)
    meta = read(core/'core.json')
    parent = dict(buildId=meta['buildId'], compilerSha256=meta['compiler']['sha256'],
                  implementationCommit=meta['compilerSourceCommit'])
    meta.update(buildId=build_id, displayName='MX251 packaged development core 2026-11-14',
                status='PACKAGED_DEVELOPMENT_BASELINE', parentCore=parent,
                sourceCommit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=REPO,text=True).strip(),
                sourceManifest='sources/compiler-files.json',
                sourceManifestSha256=sha(core/'sources/compiler-files.json'),
                report='../docs/ABI4-BACKEND-CHOICE.md')
    meta.pop('sourceCheckout', None)
    save(core/'core.json', meta)
    # Read the same locks consumed by the build, including the assembler and runtime.
    cache = {}
    for profile in (core/'profiles').iterdir():
        lock = read(profile/'profile.lock.json')
        for root, entries in [(core/'toolchain',lock['toolchain']),(profile,lock['profile'])]:
            for entry in entries:
                p = root/entry['path']
                if p not in cache: cache[p] = sha(p)
                assert cache[p] == entry['sha256'], p
    assert sha(core/meta['compiler']['path']) == meta['compiler']['sha256']
    save(output/'package-check.json', dict(status='PASS', buildId=build_id,
         compilerSha256=meta['compiler']['sha256'], sourceFiles=len(manifest['files']),
         lockedFiles=len(cache), binariesRebuilt=False))
    (output/'README.md').write_text('''# MX251 新开发核心 · 2026-11-14

本包整合截至 ABI4 整函数后端择优的成果，支持 AI8051U-34K64-32 与 STC32G12K128。
公开版本仍为 0.0170-test；本机核心身份见 core/core.json。

解压后执行：

```bat
Build.cmd -ProjectPath ".\\examples\\edata"
Build.cmd -ProjectPath ".\\examples\\math-casts-ai"
Build.cmd -ProjectPath ".\\examples\\math-casts-stc"
```

自带编译器、汇编/链接器、两套 Target Profile、整数/浮点运行库、完整源码、CMake、Ninja 和 PowerShell。
常规 small + ordinary-wide + standard 工程自动选用原生整数库；native-math/MDU 工程沿用自身运行库配置。
需要保持普通 standard 库时追加 -NativeIntegerRuntime OFF；Keil 位取反语义用 -C251Compatibility。
生成的固件与 receipt.json 位于工程的 .build/mx251/latest。

上一轮七个工程重编和 512 个实板用例的范围见 docs/ABI4-BACKEND-CHOICE.md；本包未重新验收全部工程与 Keil 运行时间。
未修改 IDE 默认核心或发布远程 Release。源码与组件许可见 LICENSE、THIRD_PARTY.md 和 core/licenses。
''',encoding='utf-8',newline='\n')
    print(json.dumps(read(output/'package-check.json')))


def seal(root, archive):
    files = [p for p in sorted(root.rglob('*')) if p.is_file()]
    save(root/'package-files.json', dict(schema=1, files=[dict(path=p.relative_to(root).as_posix(),
          bytes=p.stat().st_size, sha256=sha(p)) for p in files if p.name!='package-files.json']))
    archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as out:
        for p in sorted(root.rglob('*')):
            if p.is_file(): out.write(p, root.name+'/'+p.relative_to(root).as_posix())
    with zipfile.ZipFile(archive) as saved:
        assert saved.testzip() is None
    save(archive.with_suffix('.json'), dict(schema=1, file=archive.name, bytes=archive.stat().st_size,
         sha256=sha(archive), buildId=read(root/'core/core.json')['buildId']))
    print(json.dumps(read(archive.with_suffix('.json'))))


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    commands=p.add_subparsers(dest='command', required=True)
    s=commands.add_parser('stage');s.add_argument('--candidate',type=Path,required=True)
    s.add_argument('--host',type=Path,required=True);s.add_argument('--output',type=Path,required=True)
    s.add_argument('--build-id',required=True)
    s=commands.add_parser('seal');s.add_argument('--root',type=Path,required=True)
    s.add_argument('--archive',type=Path,required=True)
    a=p.parse_args()
    if a.command=='stage': stage(a.candidate,a.host,a.output,a.build_id)
    else: seal(a.root,a.archive)
