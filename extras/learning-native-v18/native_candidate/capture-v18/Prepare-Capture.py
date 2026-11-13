#!/usr/bin/env python3
"""Prepare local candidate configs; no process, database or network operation."""
import argparse, hashlib, json, os, re
from pathlib import Path

def sha(data): return hashlib.sha256(data).hexdigest()
def set_values(text,values):
    keys=set(values);kept=[]
    for line in text.splitlines():
        m=re.match(r'^\s*([A-Za-z0-9_.]+)\s*=',line)
        if m and m.group(1) in keys:continue
        kept.append(line)
    kept.extend(['','## Native V18 candidate settings'])
    for key,value in values.items():
        rendered=str(value) if isinstance(value,int) else json.dumps(str(value),ensure_ascii=False)
        kept.append(key+' = '+rendered)
    return '\n'.join(kept)+'\n'

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--base-runtime',required=True,type=Path)
    ap.add_argument('--data-dir',required=True,type=Path);ap.add_argument('--output',required=True,type=Path)
    ap.add_argument('--profile',choices=['boot','observe','first-five'],default='boot')
    ap.add_argument('--dungeon-id',type=int,default=464);ap.add_argument('--prepare',action='store_true')
    a=ap.parse_args();base=a.base_runtime.resolve();out=a.output.resolve();data=a.data_dir.resolve()
    if base==out or base in out.parents:raise ValueError('Candidate configs must be outside the formal runtime directory')
    if os.name=='nt' and out.drive.lower()=='c:':raise ValueError('Use E for the capture directory')
    if not data.is_dir():raise ValueError('Existing native DataDir is missing; do not copy or recreate game data')
    source=list(sorted(base.glob('*.conf')))
    if not source or len(source)>64 or sum(p.stat().st_size for p in source)>4*1024*1024:raise ValueError('Unexpected config set size')
    names={p.name.lower() for p in source}
    if not {'worldserver.conf','playerbots.conf'}<=names:raise ValueError('Actual worldserver.conf and playerbots.conf are required')
    profile=json.loads((Path(__file__).parent/'profiles.json').read_text(encoding='utf-8'))
    vals=dict(profile['defaults']);vals.update(profile['profiles'][a.profile]);vals['AiPlayerbot.BackgroundTraining.DungeonId']=a.dungeon_id
    vals['RaidAI.ShadowCollectorV18.Directory']=(out/'raw').as_posix()
    prepared=[]
    for p in source:
        raw=p.read_bytes();text=raw.decode('utf-8-sig')
        settings=dict(vals) if p.name.lower() in ['worldserver.conf','playerbots.conf'] else {}
        if p.name.lower()=='worldserver.conf':settings.update({'DataDir':data.as_posix(),'LogsDir':(out/'logs').as_posix()})
        body=set_values(text,settings).encode('utf-8') if settings else raw
        prepared.append((p,out/p.name,raw,body))
    report={'schema':'native-v18-capture-config-v1','profile':a.profile,'base_runtime':str(base),'data_dir':str(data),
        'output':str(out),'world_config':str(out/'worldserver.conf'),'raw_directory':str(out/'raw'),
        'config_files':[{'name':p.name,'source_sha256':sha(raw),'candidate_sha256':sha(body)} for p,_,raw,body in prepared],
        'settings':vals,'prepared':False,'model_control':False,'server_started':False,
        'contains_local_credentials':True,'do_not_upload_actual_configs':True}
    if not a.prepare:print(json.dumps({k:v for k,v in report.items() if k!='config_files'},ensure_ascii=False));return
    if out.exists() and any(out.iterdir()):raise ValueError('Use an empty candidate run directory; do not overwrite another capture')
    out.mkdir(parents=True,exist_ok=True);(out/'raw').mkdir();(out/'logs').mkdir()
    for p,target,raw,body in prepared:target.write_bytes(body)
    for p,_,raw,_ in prepared:
        if p.read_bytes()!=raw:raise ValueError('Base config changed during preparation')
    report['prepared']=True
    (out/'config-manifest.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print(json.dumps({'prepared_configs':len(prepared),'profile':a.profile,'manifest':str(out/'config-manifest.json'),
                      'model_control':False,'server_started':False,'database_accessed':False}))
if __name__=='__main__':main()
