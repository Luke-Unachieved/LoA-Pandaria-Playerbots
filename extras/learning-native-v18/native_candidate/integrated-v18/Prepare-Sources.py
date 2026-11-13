#!/usr/bin/env python3
"""Prepare reviewed sparse native text sources; never edits original inputs."""
import argparse, hashlib, json, os, shutil, subprocess
from pathlib import Path

def digest(data): return hashlib.sha256(data).hexdigest()
def checked(p, expected):
    data=p.read_bytes()
    if digest(data)!=expected.lower(): raise ValueError('Input changed: '+str(p))
    return data

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--output', required=True, type=Path)
    ap.add_argument('--source-map', type=Path, help='Optional explicit source-path map; every raw hash remains mandatory')
    ap.add_argument('--git', help='Existing git executable; not installed by this script')
    ap.add_argument('--apply', action='store_true', help='Without this switch, read-only input check')
    args=ap.parse_args();here=Path(__file__).resolve().parent
    manifest=json.loads((here/'inputs.json').read_text(encoding='utf-8'))
    assert manifest['schema']=='native-v18-source-inputs-v1'
    patch=here/'native-source.patch';checked(patch,manifest['patch_sha256'])
    overrides=json.loads(args.source_map.read_text(encoding='utf-8')) if args.source_map else {}
    target=args.output.resolve()
    if os.name=='nt' and target.drive.lower()=='c:': raise ValueError('Choose the E drive for native source staging')
    formal=Path('E:/WoW548/Server')
    if os.name=='nt' and (target==formal or formal in target.parents): raise ValueError('Never stage in formal server directory')
    staged=[]
    for row in manifest['native_sources']:
        old=Path(overrides.get(row['target'],row['source']))
        raw=checked(old,row['source_sha256']);normalized=raw.decode('utf-8').replace('\r\n','\n').encode('utf-8')
        if digest(normalized)!=row['normalized_source_sha256']: raise ValueError('Normalization mismatch')
        rel=Path(row['target'])
        if rel.is_absolute() or '..' in rel.parts: raise ValueError('Unsafe relative source target')
        dest=target/rel
        if dest.resolve()==old.resolve(): raise ValueError('Output aliases original input')
        staged.append((row,dest,normalized))
    for row in manifest['new_sources']: checked(here/row['path'],row['sha256'])
    if not args.apply:
        print(json.dumps({'inputs_verified':len(staged),'helper_files_verified':len(manifest['new_sources']),
            'written':False,'compiled':False,'linked':False,'deployed':False}));return
    git=args.git or shutil.which('git')
    if not git: raise RuntimeError('Existing git not found. Locate GitHub Desktop embedded git; do not install another tool blindly')
    completed=[dest.is_file() and digest(dest.read_bytes())==row['prepared_sha256'] for row,dest,_ in staged]
    if all(completed):
        print('All nine reviewed sources already prepared; no file changed.');return
    if any(completed): raise ValueError('Mixed prepared states; preserve current files and inspect, do not overwrite')
    for row,dest,normalized in staged:
        if dest.exists() and digest(dest.read_bytes())!=row['normalized_source_sha256']:
            raise ValueError('Existing staging differs: '+str(dest))
    for _,dest,normalized in staged:
        dest.parent.mkdir(parents=True,exist_ok=True)
        if not dest.exists(): dest.write_bytes(normalized)
    # Git's standard unified patch parser; exact original hashes plus final hashes
    # protect the input and result. No reset, clean, checkout or original edit.
    env=dict(os.environ);env['GIT_CONFIG_NOSYSTEM']='1';env['GIT_CONFIG_GLOBAL']=os.devnull
    env.pop('GIT_DIR',None);env.pop('GIT_WORK_TREE',None)
    common=[git,'-c','core.autocrlf=false','apply','--whitespace=nowarn']
    subprocess.run(common+['--check',str(patch)],cwd=target,env=env,check=True)
    subprocess.run(common+[str(patch)],cwd=target,env=env,check=True)
    for row,dest,_ in staged:checked(dest,row['prepared_sha256'])
    receipt={'schema':'native-v18-source-preparation-receipt-v1','prepared_sources':len(staged),
        'original_sources_unchanged':True,'native_compile':False,'native_link':False,'deployed':False}
    # Confirm original raw bytes again after preparation.
    for row,_,_ in staged:checked(Path(overrides.get(row['target'],row['source'])),row['source_sha256'])
    (target/'preparation-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(receipt))
if __name__=='__main__':main()
