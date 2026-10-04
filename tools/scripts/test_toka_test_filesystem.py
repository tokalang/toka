#!/usr/bin/env python3
"""Darwin metadata controls: reject file-kind placeholders and failed queries."""
import argparse,json,subprocess,sys
from pathlib import Path
from toka_test_filesystem import query,validate

def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);assert sys.platform=='darwin'
 target=out/'fixture directory';target.mkdir();info=query(target,out/'query');records=[]
 for name,change in [('directory-indicator',{'filesystem_type':'/'}),('empty-type',{'filesystem_type':''}),('wrong-path',{'resolved_path':str(out)}),('wrong-device',{'device':-1})]:
  data=dict(info['data'],**change)
  try:validate(data,target)
  except AssertionError:records.append({'control':name,'rejected':True})
  else:raise AssertionError('invalid filesystem metadata accepted: '+name)
 regular=out/'ordinary-file';regular.write_text('control')
 for name,path in [('missing-path',out/'missing'),('non-directory',regular)]:
  argv=[str(out/'query/statfs-query'),str(path)];r=subprocess.run(argv,capture_output=True,timeout=10);assert r.returncode==2 and not r.stdout and r.stderr
  records.append({'control':name,'argv':argv,'exit_code':r.returncode,'stderr':r.stderr.decode(),'rejected':True})
 (out/'result.json').write_text(json.dumps({'result':'pass','positive_directory':info,'negative_controls':records,'controls':7},indent=2)+'\n')
if __name__=='__main__':main()
