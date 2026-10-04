#!/usr/bin/env python3
"""Independent acquisition probe using the frozen SDK's unmodified downloader."""
import argparse,hashlib,importlib,json,os,platform,signal,sys,time
from pathlib import Path
class Limit(BaseException):pass
def alarm(*_):raise Limit()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--input-plan',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();sdk=a.sdk.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 plan=json.loads(a.input_plan.read_text());expected=plan['published_dependency'];descriptor=json.loads((sdk/'preview-sdk.json').read_text());assert descriptor['candidate_sha']=='59b40e6dd659593871c644fc2f00a696606ab850'
 helper=sdk/'lib/toolchain/toka_package.py';assert sha(helper)==descriptor['components']['lib/toolchain/toka_package.py']['sha256'];sys.path.insert(0,str(helper.parent));module=importlib.import_module('toka_package');signal.signal(signal.SIGALRM,alarm);rows=[]
 def get(label,url,path):
  start=time.monotonic_ns();signal.alarm(120);error=None;status='completed'
  try:module._download(url,path)
  except Limit:status='truncated';error='120 second measurement protection'
  except Exception as e:status='failed';error=repr(e)
  finally:signal.alarm(0)
  elapsed=(time.monotonic_ns()-start)/1e6;row={'label':label,'url':url,'duration_ms':elapsed,'state':status,'error':error,'file':path.name,'bytes':path.stat().st_size if path.exists() else None,'sha256':sha(path) if path.exists() else None,'boundary':'before frozen SDK _download request until full response consumption and destination close; includes transport and file write; excludes hash/parse/extract','protection_ms':120000};rows.append(row);(out/(label+'.json')).write_text(json.dumps(row,indent=2)+'\n');return row
 for sample in (1,2):
  catalog=out/f'catalog-{sample}.json';row=get(f'catalog-request-{sample}',module.DEFAULT_REGISTRY_URL+'/catalog.json',catalog)
  if row['state']!='completed':break
  data=json.loads(catalog.read_text());package=next(v for v in data['packages'] if v['name']=='unicode');assert package['installable'] is True;release=next(v for v in package['versions'] if v['version'].lstrip('v')=='0.1.2');assert release['sha256']==expected['archive_sha256']
  archive=out/f'unicode-0.1.2-request-{sample}.tar.gz';row=get(f'package-request-{sample}',release['tarball_url'],archive)
  if row['state']!='completed':break
  assert row['sha256']==expected['archive_sha256']
 result={'SDK_source_sha':descriptor['candidate_sha'],'helper_sha256':sha(helper),'input_plan_sha256':sha(a.input_plan),'measurement_kind':'separate SDK downloader probe; not add internal attribution','cache_condition':'two independent HTTP requests with fresh output files; SDK project cache untouched; OS/CDN cache uncontrolled','host':{'platform':platform.platform(),'machine':platform.machine(),'python':platform.python_version()},'samples':rows,'complete':len(rows)==4 and all(r['state']=='completed' for r in rows),'add_total_timing_kind':'aggregate','add_internal_network_ms':None}
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'complete':result['complete'],'samples':len(rows)}));return 0 if result['complete'] else 2
if __name__=='__main__':raise SystemExit(main())
