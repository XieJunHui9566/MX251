from pathlib import Path
import argparse, urllib.request, urllib.parse, re, json, hashlib
parser=argparse.ArgumentParser(description='Fetch the public STC-IDE worker and record its hashes.')
parser.add_argument('--output',type=Path,required=True)
R=parser.parse_args().output.resolve()
R.mkdir(parents=True,exist_ok=False)
base='https://stcide.stcaimcu.com/'
queue=['ide.worker.js']; seen=set(); records=[]
while queue:
    rel=queue.pop(0)
    if rel in seen: continue
    seen.add(rel)
    assert not rel.startswith(('/', '..')) and '\\' not in rel
    url=urllib.parse.urljoin(base,rel)
    data=urllib.request.urlopen(url,timeout=30).read()
    p=R/'vendor'/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
    records.append(dict(url=url,path=str(p.relative_to(R)),bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
    text=data.decode('utf8')
    for dep in re.findall(r'["\'](\.{1,2}/[^"\']+\.js)["\']',text):
        full=urllib.parse.urljoin(url,dep)
        assert full.startswith(base)
        queue.append(full[len(base):])
    print(rel,len(data),flush=True)
(R/'vendor-manifest.json').write_text(json.dumps(records,indent=2),encoding='utf8')
(R/'vendor/package.json').write_text('{"type":"module"}\n',encoding='utf8')
