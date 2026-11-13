#!/usr/bin/env python3
"""Explicit bounded background candidate run; no formal EXE replacement."""
import argparse,hashlib,json,os,re,stat,subprocess,time
from pathlib import Path
FORMAL_PATH=Path(r'E:\WoW548\Server\worldserver.exe')
FORMAL_SHA='f509e005ddf59360f8c3f7eedb4c1396a2b145f6ad4a17c54fe553fb1dbc0223'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def require_hash(path,expected):
    if not isinstance(expected,str) or not re.fullmatch(r'[a-fA-F0-9]{64}',expected):raise ValueError('Invalid SHA256: '+str(path))
    if digest(path)!=expected.lower():raise ValueError('Input hash changed: '+str(path))
def local_path(value):
    path=Path(os.path.abspath(value))
    for part in (path,*path.parents):
        info=part.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:raise ValueError('Capture path traverses a symlink/reparse point: '+str(part))
    if path.resolve(strict=True)!=path:raise ValueError('Capture path resolves outside its selected location: '+str(path))
    return path
def exact_path(value,expected):
    path=local_path(value)
    if path!=expected:raise ValueError('Capture path does not match selected location: '+str(path))
    return path
def validate_link(receipt_path,receipt):
    validation_path=receipt_path.parent/'validation-receipt.json'
    require_hash(validation_path,receipt.get('validation_sha256'))
    validation=json.loads(validation_path.read_text(encoding='utf-8-sig'))
    jobs=validation.get('source_objects',[]);locks=validation.get('input_locks',[])
    if validation.get('schema')!='native-v18-link-validation-v1' or len(jobs)!=15 or len({x['job'] for x in jobs})!=15 or not locks:
        raise ValueError('Expected the complete native 15-job validation and input locks')
    locked={}
    for row in locks:
        path=Path(row['path'])
        if not path.is_absolute() or path in locked:raise ValueError('Invalid or duplicate input lock: '+str(path))
        require_hash(path,row['sha256']);locked[path]=row['sha256'].lower()
    for row in jobs:
        for key in ('source','object'):
            if locked.get(Path(row[key]))!=row[key+'_sha256'].lower():raise ValueError('Native job is not hash-bound to input locks: '+row['job'])
    formal_rows=[x for x in locks if x.get('kind')=='formal-exe']
    if len(formal_rows)!=1 or Path(formal_rows[0]['path'])!=FORMAL_PATH or formal_rows[0]['sha256'].lower()!=FORMAL_SHA:
        raise ValueError('Expected the exact protected F509 formal path and lock')
    formal=exact_path(formal_rows[0]['path'],FORMAL_PATH)
    response=exact_path(receipt['response'],receipt_path.parent/'Link.rsp')
    require_hash(response,receipt.get('response_sha256'))
    require_hash(response,validation.get('transformed_response_sha256'))
    return formal
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--link-receipt',required=True,type=Path)
    ap.add_argument('--run-directory',required=True,type=Path);ap.add_argument('--start',action='store_true')
    ap.add_argument('--localization-clear',action='store_true',help='Parent has actually confirmed DB/localization coordination')
    ap.add_argument('--seconds',type=int,default=120);a=ap.parse_args()
    receipt_path=local_path(a.link_receipt);receipt=json.loads(receipt_path.read_text(encoding='utf-8-sig'))
    if receipt.get('schema')!='native-v18-link-receipt-v1' or receipt.get('candidate_accepted') is not True:raise ValueError('No accepted actual native link receipt')
    if receipt.get('formal_unchanged') is not True or receipt.get('input_locks_unchanged') is not True:raise ValueError('Native input/formal verification incomplete')
    if receipt.get('native_link_executed') is not True or receipt.get('linked') is not True or receipt.get('exit')!=0:raise ValueError('No successful actual native LINK')
    formal=validate_link(receipt_path,receipt)
    exe_rows=[x for x in receipt.get('outputs',[]) if Path(x['path']).suffix.lower()=='.exe']
    if len(exe_rows)!=1:raise ValueError('Missing or ambiguous candidate executable')
    exe=exact_path(exe_rows[0]['path'],receipt_path.parent/'worldserver-v18.exe')
    require_hash(exe,exe_rows[0]['sha256'])
    run=local_path(a.run_directory);manifest_path=exact_path(run/'config-manifest.json',run/'config-manifest.json')
    manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest.get('schema')!='native-v18-capture-config-v1' or manifest.get('prepared') is not True or manifest.get('model_control') is not False:raise ValueError('Wrong capture configuration')
    exact_path(manifest['output'],run)
    world=exact_path(manifest['world_config'],run/'worldserver.conf')
    raw=exact_path(manifest['raw_directory'],run/'raw');exact_path(run/'logs',run/'logs')
    exact_path(manifest['settings']['RaidAI.ShadowCollectorV18.Directory'],raw)
    data=local_path(manifest['data_dir'])
    if not data.is_dir():raise ValueError('Native DataDir is missing')
    configs=manifest['config_files'];names=set()
    if not 2<=len(configs)<=64:raise ValueError('Unexpected config manifest set size')
    for x in configs:
        name=x['name']
        if Path(name).name!=name or Path(name).suffix.lower()!='.conf' or name.lower() in names:raise ValueError('Invalid or duplicate config name: '+name)
        names.add(name.lower());require_hash(exact_path(run/name,run/name),x['candidate_sha256'])
    if not {'worldserver.conf','playerbots.conf'}<=names:raise ValueError('Actual worldserver.conf and playerbots.conf are required')
    base=exact_path(manifest['base_runtime'],formal.parent)
    if digest(formal)!=FORMAL_SHA or exe.samefile(formal):raise ValueError('Formal baseline changed or candidate aliases formal')
    seconds=max(30,min(a.seconds,600))
    if not a.start:
        print(json.dumps({'validated':True,'candidate':str(exe),'profile':manifest['profile'],'seconds_after_process_start':seconds,
                          'server_started':False,'localization_clear':a.localization_clear}));return
    if os.name!='nt':raise RuntimeError('Native candidate startup is Windows-only')
    if not a.localization_clear:raise ValueError('Localization/DB coordination is not confirmed; do not start service')
    if any((run/name).exists() for name in ('run-receipt.json','console.stdout.log','console.stderr.log')) or any(raw.glob('raw-engine-shadow-v18-*.jsonl')):
        raise ValueError('Capture directory contains prior launch evidence; prepare a fresh run directory')
    # Native process inventory only. No login/session/UI operation, and no other process is stopped.
    inventory=subprocess.run(['tasklist.exe','/FO','CSV','/NH'],capture_output=True,text=True,check=True)
    if any(name in inventory.stdout.lower() for name in ['"worldserver.exe"','"worldserver-v18.exe"']):raise ValueError('Existing worldserver found; coordinate instead of running two instances')
    env=dict(os.environ);env['PATH']=str(base)+os.pathsep+env.get('PATH','')
    start=time.monotonic();forced=False;command_sent=False
    with (run/'console.stdout.log').open('xb') as stdout,(run/'console.stderr.log').open('xb') as stderr:
        proc=subprocess.Popen([str(exe),'-c',str(world)],cwd=run,env=env,stdin=subprocess.PIPE,stdout=stdout,stderr=stderr,
                              creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            while proc.poll() is None and time.monotonic()-start<seconds:time.sleep(1)
        finally:
            if proc.poll() is None:
                try:proc.stdin.write(b'server shutdown 0\n');proc.stdin.flush();command_sent=True
                except (BrokenPipeError,OSError):pass
                try:proc.wait(timeout=60)
                except subprocess.TimeoutExpired:proc.kill();proc.wait();forced=True
    counts={};bad=0;stop_seen=False
    for file in sorted(raw.glob('raw-engine-shadow-v18-*.jsonl')):
        for line in file.open(encoding='utf-8'):
            try:row=json.loads(line)
            except (ValueError,UnicodeError):bad+=1;continue
            kind=row.get('type','unknown');counts[kind]=counts.get(kind,0)+1
            stop_seen|=kind=='stop'
    result={'schema':'native-v18-controlled-run-receipt-v1','profile':manifest['profile'],'candidate':str(exe),
        'candidate_sha256':digest(exe),'pid':proc.pid,'seconds':time.monotonic()-start,'exit':proc.returncode,
        'shutdown_command_sent':command_sent,'forced_stop':forced,'raw_types':counts,'invalid_json_lines':bad,
        'writer_stop_seen':stop_seen,'formal_unchanged':digest(formal)==FORMAL_SHA,'model_control':False,
        'complete_training_episode':False,'success_of_dungeon_or_raid':None}
    (run/'run-receipt.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))
if __name__=='__main__':main()
