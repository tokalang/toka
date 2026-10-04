#!/usr/bin/env python3
"""Generic command recorder for independent E0 actors; no project repair logic."""
import argparse,hashlib,json,os,signal,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--records',type=Path,required=True);p.add_argument('--actor',required=True);p.add_argument('--sdk',type=Path,required=True);p.add_argument('--stage',required=True);p.add_argument('argv',nargs=argparse.REMAINDER);a=p.parse_args()
argv=a.argv[1:] if a.argv[:1]==['--'] else a.argv
assert argv
out=a.records.resolve();out.mkdir(parents=True,exist_ok=True);index=len(list(out.glob('*.json')))+1;stem=f'{index:03d}-{a.stage}'
env={k:v for k,v in os.environ.items() if not k.startswith('TOKA')};env['PATH']=str(a.sdk.resolve()/'bin')+os.pathsep+env['PATH'];env['PYTHONDONTWRITEBYTECODE']='1'
command=argv
if os.uname().sysname=='Darwin':command=['/usr/bin/sandbox-exec','-p','(version 1) (allow default) (deny file-read* (subpath "/Users/zhyi/GitDP/tokalang"))',*argv]
start=time.monotonic();child=subprocess.Popen(command,cwd=Path.cwd(),env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True);guard=None
try:stdout,stderr=child.communicate(timeout=900)
except subprocess.TimeoutExpired:
 guard='measurement_guard';os.killpg(child.pid,signal.SIGTERM)
 try:stdout,stderr=child.communicate(timeout=5)
 except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);stdout,stderr=child.communicate()
duration=(time.monotonic()-start)*1000
(out/(stem+'.stdout')).write_bytes(stdout);(out/(stem+'.stderr')).write_bytes(stderr)
row={'actor':a.actor,'argv':argv,'actual_command':command,'cwd':str(Path.cwd()),'duration_ms':duration,'exit_code':child.returncode if child.returncode>=0 else None,'signal':-child.returncode if child.returncode<0 else None,'guard':guard,'SDK_source_sha':json.loads((a.sdk/'preview-sdk.json').read_text())['candidate_sha'],'stdout_sha256':hashlib.sha256(stdout).hexdigest(),'stderr_sha256':hashlib.sha256(stderr).hexdigest(),'add_timing_kind':'aggregate' if argv[:2]==['toka','add'] else None,'add_internal_network_ms':None}
(out/(stem+'.json')).write_text(json.dumps(row,indent=2)+'\n');print(json.dumps(row));raise SystemExit(child.returncode if child.returncode>=0 else 128-child.returncode)
