"""Explicit input/configuration followups for the official SDK coverage study."""
from pathlib import Path
import argparse
import concurrent.futures
import hashlib
import re
import shutil
import sys
import sdk_coverage_matrix as matrix


def clone_project(source, dest, **settings):
    if dest.exists():
        return
    shutil.copytree(source/'src', dest/'src')
    shutil.copy2(source/'CMakeLists.txt', dest/'CMakeLists.txt')
    config=matrix.read(source/'mx251.json')
    config.update(settings)
    matrix.save(dest/'mx251.json', config)


def native_core(root, config):
    original=Path(config['core'])
    core=root/'native-math-core'
    if not core.exists():
        core.mkdir()
        for folder in ('build','profiles','toolchain'):
            shutil.copytree(original/folder,core/folder)
        manifest=matrix.read(original/'core.json')
        manifest.update(buildId='sdk-math-coverage-experiment',status='COMPARISON_ONLY',
            qualification='AI8051U ordinary native-math enabled for offline comparison; no new full-project hardware qualification')
        matrix.save(core/'core.json',manifest)
        contract=core/'build/qualified/TargetContracts.json'
        value=matrix.read(contract)
        value['targets']['AI8051U-34K64-32']['allowedRuntimes'].append('native-math')
        matrix.save(contract,value)
        for path in (core/'profiles').glob('*/profile.lock.json'):
            value=matrix.read(path)
            value['targetContract']['registrySha256']=matrix.pin(contract)['sha256']
            matrix.save(path,value)
    result=dict(config,core=str(core),mxSwitches=[s for s in config['mxSwitches'] if s!='-NativeIntegerRuntime'])
    matrix.save(root/'native-config.json',result)
    matrix.save(root/'native-core-provenance.json',dict(parent=matrix.pin(original/'core.json'),
        comparison=matrix.pin(core/'core.json'), originalContract=matrix.pin(original/'build/qualified/TargetContracts.json'),
        comparisonContract=matrix.pin(core/'build/qualified/TargetContracts.json'),
        change='Only allow AI8051U ordinary native-math in comparison copy and refresh profile contract hashes',
        compilerUnchanged=matrix.pin(core/'toolchain/bin/sdcc.exe')['sha256']==matrix.pin(original/'toolchain/bin/sdcc.exe')['sha256'],
        hardwareExecuted=False))
    return result


def run(root, mode, jobs):
    config=matrix.read(root/'config.json')
    projects=matrix.read(root/'inputs.json')['projects']
    tasks=[]
    if mode=='native':
        config=native_core(root,config)
        for project in projects:
            if project['group']=='math9' and not project['libraries']:
                base=root/'projects'/project['id']
                clone_project(base/'mx251',base/'mx251-native',runtime='native-math')
                tasks.append((config,project,'mx251-native'))
    elif mode=='large':
        config=dict(config,mxSwitches=[s for s in config['mxSwitches'] if s!='-NativeIntegerRuntime'])
        for project in projects:
            if not project['cSources'] or project['libraries']:continue
            base=root/'projects'/project['id']
            log=base/'mx251/.build/mx251/latest/link.stderr.log'
            if log.exists() and 'consecutive bytes in internal RAM' in log.read_text('utf-8',errors='replace'):
                clone_project(base/'mx251',base/'mx251-large',model='large')
                tasks.append((config,project,'mx251-large'))
        matrix.save(root/'large-config.json',config)
    elif mode=='keil':
        dest=root/'keil-adapter'
        dest.mkdir(exist_ok=True)
        source=Path(config['keilVectorAdapter'])
        text=source.read_text('utf-8')
        assert 'SAVE|RESTORE|PARM251|' in text
        text=text.replace('SAVE|RESTORE|PARM251|','SAVE|RESTORE|PARM251|PARM4|')
        text=text.replace('FUNCTIONS\\((?:STATIC|REENTRANT)\\)',
                          'FUNCTIONS\\s*\\(\\s*(?:STATIC|REENTRANT)\\s*\\)')
        text=text.replace("', pragma):", "', pragma, re.I):")
        (dest/'adapter.py').write_text(text,encoding='utf-8')
        matrix.save(dest/'provenance.json',dict(source=matrix.pin(source),adapted=matrix.pin(dest/'adapter.py'),
            change='Preserve the PARM4 calling-convention pragma emitted by Keil math.h; same vector adapter otherwise'))
        config=dict(config,keilVectorAdapter=str(dest/'adapter.py'))
        matrix.save(root/'keil-config.json',config)
        for project in projects:
            result=root/'projects'/project['id']/'keil/result.json'
            if result.exists() and 'Unclassified pragma:' in matrix.read(result).get('error',''):
                tasks.append((config,project,'keil-fixed'))
        for original in projects:
            if original['id'] not in ('p124','p125','p126','p144','p145'):continue
            project=dict(original)
            output=root/'header-supplement'
            base=output/'projects'/project['id']
            if not (base/'native').exists():
                source=root/'projects'/project['id']
                shutil.copytree(source/'native',base/'native')
                prep,_=matrix.adapters(Path(config['corpus']))
                header=prep.ROOT/'COMM/AI8051U.H'
                shutil.copy2(header,base/'native/AI8051U.H')
                meta=matrix.read(source/'original-inputs.json')
                meta['files'].append(dict(name='AI8051U.H',source=str(header),sourceSha256=matrix.pin(header)['sha256'],copySha256=matrix.pin(header)['sha256']))
                matrix.save(base/'original-inputs.json',meta)
                matrix.save(base/'header-supplement.json',dict(header=matrix.pin(header),sourceAlgorithmsChanged=False))
            project['sourceManifest']=matrix.pin(base/'original-inputs.json')
            local=dict(config,output=str(output))
            tasks.append((local,project,'keil-fixed'))
    elif mode=='edata':
        for project in projects:
            if project['id'] not in ('p022','p135','p141'):continue
            base=root/'projects'/project['id']
            clone_project(base/'mx251',base/'mx251-edata')
            path=base/'mx251-edata/CMakeLists.txt'
            text=path.read_text('utf-8')
            if 'edata=__edata' not in text:
                path.write_text(text.replace('mx251_firmware(app)',
                    'target_compile_definitions(app INTERFACE edata=__edata)\nmx251_firmware(app)'),encoding='utf-8')
            tasks.append((config,project,'mx251-edata'))
    elif mode=='headers':
        prep,dialect=matrix.adapters(Path(config['corpus']))
        header=prep.ROOT/'COMM/AI8051U.H'
        # Supply the same unmodified, official common header to all compilers.
        # The original missing-input results remain in the first matrix.
        output=root/'header-supplement'
        config=dict(config,output=str(output),keilVectorAdapter=str(root/'keil-adapter/adapter.py'))
        prepared=[]
        for original in projects:
            if original['id'] not in ('p072','p073','p144','p145'):continue
            project=dict(original)
            project.pop('preparationError',None)
            base=output/'projects'/project['id']
            source=root/'projects'/project['id']
            native=base/'native'
            if not native.exists():
                shutil.copytree(source/'native',native)
                shutil.copy2(header,native/'AI8051U.H')
                meta=matrix.read(source/'original-inputs.json')
                meta['files'].append(dict(name='AI8051U.H',source=str(header),
                    sourceSha256=matrix.pin(header)['sha256'],copySha256=matrix.pin(native/'AI8051U.H')['sha256']))
                matrix.save(base/'original-inputs.json',meta)
                mx=base/'mx251'
                (mx/'src').mkdir(parents=True)
                texts={p.name:prep.decode(p) for p in native.iterdir() if p.suffix.lower() in ('.c','.h','.asm')}
                allcode='\n'.join(prep.code(s) for s in texts.values())
                regs={n:int(a,0) for n,a in re.findall(r'\bsfr\s+(\w+)\s*=\s*(0x[\da-fA-F]+|\d+)\s*;',allcode)}
                ext=sorted({n for n,r,b in re.findall(r'\bsbit\s+(\w+)\s*=\s*(\w+)\s*\^\s*([0-7])\s*;',allcode) if regs[r]%8})
                for name,text in texts.items():
                    converted=dialect['asm_dialect'](text) if name in meta['assembly'] else dialect['mx_dialect'](text,regs,ext,[])
                    converted=prep.INC.sub(lambda m:m[1]+m[2].lower()+m[3],converted)
                    (mx/'src'/name.lower()).write_bytes(converted.encode('gb18030'))
                (mx/'src/intrins.h').write_text('#define _nop_() do { __asm nop __endasm; } while (0)\n')
                model='large' if project['id'] in ('p144','p145') else 'small'
                matrix.save(mx/'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model=model,optimization='speed',backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[]))
                cm='cmake_minimum_required(VERSION 3.25)\nproject(official_box NONE)\ninclude("${MX251_PROJECT_MODULE}")\nadd_library(app INTERFACE)\n'
                cm+='target_sources(app INTERFACE '+' '.join('"src/'+n.lower()+'"' for n in meta['sources']+meta['assembly'])+')\n'
                cm+='target_include_directories(app INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")\n'
                cm+='target_compile_definitions(app INTERFACE __C251__=1 edata=__edata '+meta['defines'].replace(',',' ')+')\nmx251_firmware(app)\n'
                (mx/'CMakeLists.txt').write_text(cm,encoding='utf-8')
            project['sourceManifest']=matrix.pin(base/'original-inputs.json')
            project['originalFiles']=[matrix.pin(p) for p in sorted(native.iterdir())]
            project['mxInputs']=[matrix.pin(p) for p in sorted((base/'mx251').rglob('*')) if p.is_file() and '.build' not in p.parts]
            local=dict(config)
            if project['id'] in ('p144','p145'):
                local['mxSwitches']=[s for s in config['mxSwitches'] if s!='-NativeIntegerRuntime']
            for suite in ('mx251','keil','stc-soft','stc-mdu32'):tasks.append((local,project,suite))
            prepared.append(project)
        matrix.save(output/'inputs.json',dict(configuration=config,projects=prepared,
            supplement=matrix.pin(header),reason='Original four project include paths omit the official COMM header',
            sourceAlgorithmsChanged=False))
    elif mode=='braces':
        sys.path.insert(0,config['corpus'])
        import initializer_adapter
        project=next(p for p in projects if p['id']=='p130')
        base=root/'projects'/project['id']
        for suite,runtime in [('mx251-braces','standard'),('mx251-native-braces','native-math')]:
            target=base/suite
            clone_project(base/'mx251',target,runtime=runtime)
            changes=[]
            for p in (target/'src').glob('*.h'):
                text=matrix.matrix.decode(p)
                updated,details=initializer_adapter.normalize(text)
                if details:
                    before=matrix.pin(p)
                    p.write_bytes(updated.encode('gb18030'))
                    changes.append(dict(before=before,after=matrix.pin(p),arrays=details))
            matrix.save(target/'initializer-adapter.json',dict(adapter=matrix.pin(Path(config['corpus'])/'initializer_adapter.py'),changes=changes))
            local=native_core(root,config) if runtime=='native-math' else config
            tasks.append((local,project,suite))
    elif mode=='assembly':
        inventory={r['id']:r for r in matrix.read(Path(config['corpus'])/'inventory.json')}
        includes=[]
        for project in projects:
            if project['cSources']:continue
            base=root/'projects'/project['id']
            row=inventory[project['id']]
            for entry in row['files']:
                if entry['type']!='2':continue
                original=Path(entry['path'])
                text=matrix.matrix.decode(original)
                for rel in re.findall(r'^\s*\$include\s*\(([^)]+)\)',text,re.I|re.M):
                    source=(original.parent/rel.strip()).resolve()
                    target=(base/'keil-asm'/rel.strip()).resolve()
                    assert target.is_relative_to(root.resolve())
                    target.parent.mkdir(parents=True,exist_ok=True)
                    if target.exists():assert matrix.pin(target)['sha256']==matrix.pin(source)['sha256']
                    else:shutil.copy2(source,target)
                    includes.append(dict(id=project['id'],source=matrix.pin(source),copy=matrix.pin(target)))
            tasks.append((config,project,'keil-asm'))
        matrix.save(root/'assembly-includes.json',includes)
    elif mode=='extended-vectors':
        adapter=root/'keil-adapter/adapter-65.py'
        text=(root/'keil-adapter/adapter.py').read_text('utf-8')
        assert 'def translate(text, maximum=63):' in text
        adapter.write_text(text.replace('def translate(text, maximum=63):','def translate(text, maximum=65):'),encoding='utf-8')
        config=dict(config,keilVectorAdapter=str(adapter))
        for original in projects:
            if original['id'] not in ('p123','p124','p125','p126'):continue
            project=dict(original)
            local=dict(config)
            if project['id']!='p123':
                local['output']=str(root/'header-supplement')
                project['sourceManifest']=matrix.pin(Path(local['output'])/'projects'/project['id']/'original-inputs.json')
            tasks.append((local,project,'keil-extended'))
    elif mode=='vector-case':
        source=root/'header-supplement/projects/p145/keil-fixed'
        out=source.parent/'keil-symbols'
        if (out/'result.json').exists():return
        out.mkdir()
        pins=[]
        for p in source.iterdir():
            if p.suffix.lower() in ('.obj','.lib') or p.name=='vectors.asm':
                shutil.copy2(p,out/p.name)
                pins.append(matrix.pin(p))
        old_map=(source/'FIRMWARE.MAP').read_text('latin1')
        asm=(out/'vectors.asm').read_text()
        bindings={}
        for name in re.findall(r'EXTRN CODE \((\w+)\)',asm):
            matches=re.findall(r'^.*?CODE\s+---\s+('+re.escape(name)+r'(?:\?_)?)\s*$',old_map,re.M)
            assert len(matches)==1,(name,matches)
            bindings[name]=matches[0]
            asm=asm.replace('('+name+')','('+matches[0]+')').replace('LJMP '+name+'\n','LJMP '+matches[0]+'\n')
        (out/'vectors.asm').write_text(asm)
        matrix.save(out/'reused-inputs.json',dict(objects=pins,sourceResult=matrix.pin(source/'result.json'),bindings=bindings,
                    change='Bind vector entries to the actual C251 public names in the link map and preserve case; reuse compiled C objects'))
        kit=Path(config['keil'])
        matrix.matrix.run(out,'vectors',[kit/'BIN/A251.EXE','vectors.asm','MODSRC','CASE','OBJECT(vectors.obj)','INTR2'],config,keil=True)
        matrix.matrix.run(out,'link',matrix.read(source/'link.command.json')['argv'],config,keil=True)
        matrix.matrix.run(out,'hex',[kit/'BIN/OH251.EXE','firmware','HEXFILE(firmware.hex)'],config,keil=True)
        stats,memory=matrix.matrix.image(out/'firmware.hex')
        mapping=(out/'FIRMWARE.MAP').read_text('latin1')
        vectors=[]
        for name,offset in re.findall(r'EXTRN CODE \(([\w?$]+)\)\s+CSEG AT 0([0-9A-F]+)H', (out/'vectors.asm').read_text()):
            matches=re.findall(r'^\s*([0-9A-F]+)H\s+CODE\s+---\s+'+re.escape(name)+r'\s*$',mapping,re.M)
            assert len(matches)==1,name
            target=int(matches[0],16);at=0xff0000+int(offset,16)
            assert [memory.get(at+i) for i in range(3)]==[2,(target>>8)&255,target&255]
            vectors.append(dict(symbol=name,vector=at,target=target))
        matrix.save(out/'result.json',dict(status='BUILT',id='p145',suite='keil-symbols',**stats,model='XSMALL',
            hardwareExecuted=False,firmwareExecutionTime=None,inputManifest=matrix.pin(out.parent/'original-inputs.json'),verifiedVectors=vectors))
        print('p145 keil-symbols BUILT',stats['payloadBytes'],flush=True)
        return
    else:
        raise ValueError(mode)
    matrix.save(root/(mode+'-followups.json'),dict(mode=mode,configuration=config,
        jobs=[dict(id=p['id'],suite=s) for c,p,s in tasks],runner=matrix.pin(__file__)))
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures=[pool.submit(matrix.one,*t) for t in tasks]
        for f in concurrent.futures.as_completed(futures):f.result()


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root',type=Path,required=True)
    ap.add_argument('--mode',choices=('native','large','keil','edata','headers','braces','assembly','extended-vectors','vector-case'),required=True)
    ap.add_argument('--jobs',type=int,default=2)
    args=ap.parse_args()
    run(args.root,args.mode,args.jobs)
