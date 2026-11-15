"""Refresh three ABI2 standard-library members in an explicit candidate core.

The original C sources are unchanged. Other archive members are preserved.
This command builds and pins a candidate; it does not qualify target execution.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


MEMBERS = ('__memcpy', '_memset', 'strlen')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf8', newline='\n')


def archive_members(data):
    assert data.startswith(b'!<arch>\n')
    pos, names, members = 8, b'', {}
    while pos < len(data):
        head = data[pos:pos+60]
        assert head[58:60] == b'`\n'
        size = int(head[48:58])
        name = head[:16].decode().strip()
        body = data[pos+60:pos+60+size]
        pos += 60 + size + size % 2
        if name == '//':
            names = body
            continue
        if name in ('/', '/SYM64/'):
            continue
        if name.startswith('#1/'):
            length = int(name[3:])
            name, body = body[:length].decode().rstrip('\0'), body[length:]
        elif name.startswith('/'):
            name = names[int(name[1:]):].split(b'\n', 1)[0].decode().rstrip('/')
        else:
            name = name.rstrip('/')
        assert name not in members
        members[name] = body
    return members


def object_info(data):
    text = data.decode('ascii')
    areas = re.findall(r'^A \S+ size ([0-9A-F]+) flags ([0-9A-F]+)', text, re.M)
    return dict(codeBytes=sum(int(n, 16) for n, flags in areas if int(flags, 16) & 0x20),
                exports=sorted(set(re.findall(r'^S (\S+) Def', text, re.M))))


def package(core, source, report):
    recipe = core/'sources/standard-library-builder'
    recipe.mkdir(parents=True, exist_ok=True)
    destination = recipe/Path(__file__).name
    if destination.resolve() != Path(__file__).resolve():
        shutil.copy2(__file__, destination)
    sources = core/'sources/compiler/device/lib'
    sources.mkdir(parents=True, exist_ok=True)
    for name in MEMBERS:
        if (source/(name+'.c')).resolve() != (sources/(name+'.c')).resolve():
            shutil.copy2(source/(name+'.c'), sources/(name+'.c'))
    write(recipe/'manifest.json', report)
    manifest_path = core/'sources/compiler-files.json'
    manifest = read(manifest_path)
    for name in MEMBERS:
        relative = 'sources/compiler/device/lib/'+name+'.c'
        row = next((r for r in manifest['files'] if r['path'] == relative), None)
        if row is None:
            row = dict(path=relative)
            manifest['files'].append(row)
        row['sha256'] = sha((core/relative).read_bytes())
    write(manifest_path, manifest)
    meta_path = core/'core.json'
    meta = read(meta_path)
    meta['standardLibrary'] = dict(recipe='sources/standard-library-builder/'+Path(__file__).name,
        recipeSha256=sha(destination.read_bytes()), manifest='sources/standard-library-builder/manifest.json',
        manifestSha256=sha((recipe/'manifest.json').read_bytes()), qualification=report['qualification'])
    meta['sourceManifestSha256'] = sha(manifest_path.read_bytes())
    for field in ('sourceChanges', 'runtimeSourceChanges'):
        for row in meta.get(field, []):
            row['sha256'] = sha((core/row['path']).read_bytes())
    write(meta_path, meta)


def build(core, source, output):
    core, source, output = core.resolve(), source.resolve(), output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    toolchain = core/'toolchain'
    compiler, ar = toolchain/'bin/sdcc.exe', toolchain/'bin/sdar.exe'
    env = dict(os.environ, SDCC_HOME=str(toolchain), SOURCE_DATE_EPOCH='0')
    env['PATH'] = str(toolchain/'bin') + os.pathsep + env['PATH']
    startup = read(core/'build/qualified/StartupRuntimeContract.json')
    formats = read(core/'build/qualified/FormatRuntimeContract.json')
    report = dict(schema=1, qualification='COMPILE_ONLY',
                  compilerSha256=sha(compiler.read_bytes()), archives=[])
    for model in ('small', 'large'):
        relative = f'lib/mcs251-{model}-stack-auto/libsdcc.lib'
        archive = toolchain/relative
        original = archive.read_bytes()
        old = archive_members(original)
        assert startup['archives'][relative]['sha256'] == sha(original)
        assert formats['archives'][relative] == sha(original)
        directory = output/model
        directory.mkdir()
        pending = directory/'libsdcc.lib'
        pending.write_bytes(original)
        rows = []
        for name in MEMBERS:
            cfile = source/(name+'.c')
            shutil.copy2(cfile, directory/cfile.name)
            command = [str(compiler), '-mmcs251', '--model-'+model, '--stack-auto',
                       '--mcs251-wide-ordinary', '--mcs251-machine-ir', '--opt-code-size',
                       '--std-sdcc11', '--nostdinc', '-I'+str(toolchain/'include'),
                       '-c', cfile.name, '-o', name+'.rel']
            run = subprocess.run(command, cwd=directory, env=env, capture_output=True)
            (directory/(name+'.log')).write_bytes(run.stdout+run.stderr)
            run.check_returncode()
            member = name+'.rel'
            fresh = (directory/member).read_bytes()
            before, after = object_info(old[member]), object_info(fresh)
            assert before['exports'] == after['exports'], (member, before, after)
            rows.append(dict(member=member, sourceSha256=sha(cfile.read_bytes()),
                             oldSha256=sha(old[member]), sha256=sha(fresh),
                             before=before, after=after, command=command))
        subprocess.run([str(ar), '-rcsD', pending.name, *[n+'.rel' for n in MEMBERS]],
                       cwd=directory, env=env, check=True, capture_output=True)
        fresh_archive = pending.read_bytes()
        new = archive_members(fresh_archive)
        assert old.keys() == new.keys()
        changed = {name for name in old if old[name] != new[name]}
        assert changed == {n+'.rel' for n in MEMBERS}
        for row in rows:
            assert sha(new[row['member']]) == row['sha256']
        report['archives'].append(dict(path=relative, oldSha256=sha(original),
            sha256=sha(fresh_archive), unchangedMembers=len(old)-len(changed), members=rows))

    # Publish both model builds together after assembling and checking their members.
    for row, model in zip(report['archives'], ('small', 'large')):
        relative = row['path']
        shutil.copy2(output/model/'libsdcc.lib', toolchain/relative)
        startup['archives'][relative]['sha256'] = row['sha256']
        for member in row['members']:
            startup['archives'][relative]['members'][member['member']] = member['sha256']
        formats['archives'][relative] = row['sha256']
    write(core/'build/qualified/StartupRuntimeContract.json', startup)
    write(core/'build/qualified/FormatRuntimeContract.json', formats)
    hashes = {row['path']: row['sha256'] for row in report['archives']}
    for path in (core/'profiles').glob('*/profile.lock.json'):
        lock = read(path)
        for item in lock['toolchain']:
            if item['path'] in hashes:
                item['sha256'] = hashes[item['path']]
        write(path, lock)
    write(output/'manifest.json', report)
    package(core, source, report)
    print(json.dumps([dict(model=model, members=[dict(name=r['member'],
        before=r['before']['codeBytes'], after=r['after']['codeBytes']) for r in row['members']])
        for row, model in zip(report['archives'], ('small', 'large'))]))
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core', type=Path, required=True, help='Explicit candidate core to update')
    p.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1]/'compiler/device/lib')
    p.add_argument('--output', type=Path, required=True, help='New build evidence directory')
    a = p.parse_args()
    build(a.core, a.source, a.output)
