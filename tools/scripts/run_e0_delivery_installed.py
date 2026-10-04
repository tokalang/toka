#!/usr/bin/env python3
"""Relocate and execute already delivered application bytes; never rebuild/relock."""
import argparse,hashlib,json,platform,subprocess,tarfile,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--receipts',type=Path,required=True);p.add_argument('--fixtures',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);rows=[]
for task,name in [('registry_unicode_consumer','registry_unicode_consumer'),('csv-transform','csv_transform')]:
 source=a.receipts/'candidate-AI_process'/(task+'-cold');session=json.loads((source/'session.json').read_text());archive=source/'delivery.tar.gz';digest=hashlib.sha256(archive.read_bytes()).hexdigest();assert digest==session['delivery_sha256'] and session['completion']=='complete'
 root=out/(task+'-relocated');root.mkdir()
 with tarfile.open(archive) as t:t.extractall(root,filter='data')
 project=root/name;binary=project/'target/debug'/name;before=hashlib.sha256(binary.read_bytes()).hexdigest();argv=[str(binary)]+(['resources/input.csv','relocated-output.csv'] if task=='csv-transform' else []);start=time.monotonic();r=subprocess.run(argv,cwd=project,capture_output=True,timeout=30);elapsed=(time.monotonic()-start)*1000;(root/'stdout').write_bytes(r.stdout);(root/'stderr').write_bytes(r.stderr);assert hashlib.sha256(binary.read_bytes()).hexdigest()==before
 verify=None
 if task=='csv-transform':
  v=subprocess.run(['python3',str(a.fixtures.resolve()/'verify_csv.py'),'relocated-output.csv'],cwd=project,capture_output=True,timeout=10);(root/'verify.stdout').write_bytes(v.stdout);(root/'verify.stderr').write_bytes(v.stderr);verify=v.returncode
 row={'task':task,'source_archive_sha256':digest,'source_run':37191719627,'source_attempt':1,'SDK_source_sha':'59b40e6dd659593871c644fc2f00a696606ab850','binary_sha256':before,'argv':argv,'cwd':str(project),'duration_ms':elapsed,'exit_code':r.returncode if r.returncode>=0 else None,'signal':-r.returncode if r.returncode<0 else None,'output_verifier_exit_code':verify,'recompiled':False,'relocked':False,'completed':r.returncode==0 and (verify is None or verify==0)};(root/'receipt.json').write_text(json.dumps(row,indent=2)+'\n');rows.append(row)
result={'host':{'platform':platform.platform(),'machine':platform.machine()},'tasks':rows,'complete':all(r['completed'] for r in rows)};(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));raise SystemExit(0 if result['complete'] else 2)
