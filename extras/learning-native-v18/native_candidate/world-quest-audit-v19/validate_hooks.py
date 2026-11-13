#!/usr/bin/env python3
"""Focused source composition and standalone evidence checks, not native gameplay tests."""
from pathlib import Path
import hashlib,json,os,subprocess,tempfile
HERE=Path(__file__).resolve().parent
def sha(data):return hashlib.sha256(data).hexdigest()
subprocess.run(['python3',str(HERE/'build_hooks.py'),'--check'],check=True)
manifest=json.loads((HERE/'hook-inputs.json').read_text())
env=dict(os.environ);env.pop('GIT_DIR',None);env.pop('GIT_WORK_TREE',None)
env['GIT_CONFIG_NOSYSTEM']='1';env['GIT_CONFIG_GLOBAL']=os.devnull
with tempfile.TemporaryDirectory(prefix='v19-patch-') as directory:
    base=Path(directory)
    for row in manifest['native_sources']:
        source=Path(row['source']);raw=source.read_bytes();assert sha(raw)==row['source_sha256']
        target=base/row['target'];target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(raw.decode().replace('\r\n','\n'))
    command=['git','apply','--whitespace=nowarn']
    subprocess.run(command+['--check',str(HERE/'native-hooks.patch')],cwd=base,env=env,check=True)
    subprocess.run(command+[str(HERE/'native-hooks.patch')],cwd=base,env=env,check=True)
    for row in manifest['native_sources']:
        assert sha((base/row['target']).read_bytes())==row['prepared_sha256']
        assert sha(Path(row['source']).read_bytes())==row['source_sha256']
    binary=base/'evidence-test'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-I'+str(HERE),str(HERE/'tests/test_evidence.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
receipt=dict(schema='v19-focused-checks-v1',exact_native_seams=len(manifest['native_sources']),git_patch_apply_and_final_hashes=True,
    source_inputs_unchanged=True,standalone_evidence_and_recovery_test=True,native_compile=False,native_link=False,runtime=False,
    evidence_scope='Primitive progress/invalid-evidence handling, bounded legal recovery and login backoff only; no native gameplay or login proof')
(HERE/'validation-report.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt))
