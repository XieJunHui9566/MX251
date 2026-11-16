"""Retain research inputs/results while removing archived build products."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f,'sha256').hexdigest()


def keep(name):
    path=PurePosixPath(name.replace('\\','/'))
    parts=[p.lower() for p in path.parts]
    if any(p in {'candidate','core','host','toolchain','compiler-build','__pycache__'} for p in parts):
        return False
    return path.suffix.lower() in {'.json','.jsonl','.md','.txt','.csv','.tsv','.log',
        '.hex','.bin','.c','.h','.inc','.py','.ps1','.cmake'} or path.name=='CMakeLists.txt'


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--remove-originals',action='store_true')
    a=ap.parse_args();root=a.root.resolve();output=a.output.resolve()
    assert output.is_relative_to(root) and not output.exists()
    # Snapshot the explicit archive directory before creating the new archive.
    inputs=sorted(root.rglob('*.zip'))
    assert inputs
    records=[];retained=0
    with zipfile.ZipFile(output,'x',zipfile.ZIP_DEFLATED,compresslevel=1) as dest:
        for source in inputs:
            assert source.resolve().is_relative_to(root) and not source.is_symlink()
            record=dict(path=source.relative_to(root).as_posix(),bytes=source.stat().st_size,
                sha256=sha(source),kept=0,discarded=0,keptBytes=0,discardedBytes=0)
            prefix=source.relative_to(root).with_suffix('').as_posix()
            with zipfile.ZipFile(source) as old:
                for entry in old.infolist():
                    if entry.is_dir():continue
                    if keep(entry.filename):
                        data=old.read(entry)  # Verifies the original member CRC.
                        dest.writestr(prefix+'/'+entry.filename.replace('\\','/'),data)
                        record['kept']+=1;record['keptBytes']+=len(data)
                    else:
                        record['discarded']+=1;record['discardedBytes']+=entry.file_size
            records.append(record);retained+=record['kept']
            print('COMPACT',record['path'],record['kept'],'evidence files',flush=True)
        dest.writestr('retirement-index.json',json.dumps(records,indent=2)+'\n')
    with zipfile.ZipFile(output) as check:
        assert check.testzip() is None
        assert len(check.infolist())==retained+1
    report=dict(archive=output.name,sha256=sha(output),bytes=output.stat().st_size,
        sourceArchives=records,retainedFiles=retained,removedOriginals=False)
    report_path=output.with_suffix('.json')
    if a.remove_originals:
        for record in records:
            path=(root/record['path']).resolve()
            assert path.is_relative_to(root) and sha(path)==record['sha256']
            path.unlink()
        report['removedOriginals']=True
        report['freedBytes']=sum(r['bytes'] for r in records)-output.stat().st_size
    report_path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8',newline='\n')
    print('COMPLETE',retained,'evidence files;',report.get('freedBytes',0),'bytes freed',flush=True)


if __name__=='__main__':main()
