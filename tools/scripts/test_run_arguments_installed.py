#!/usr/bin/env python3
"""Direct argv and raw application exit checks on the actual installed manager."""
import argparse,json,os,subprocess
from pathlib import Path


def main():
    p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();sdk=a.sdk.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    env={k:v for k,v in os.environ.items() if not k.startswith('TOKA')};env.update(PATH=str(sdk/'bin')+os.pathsep+os.environ['PATH'],PYTHONDONTWRITEBYTECODE='1');rows=[]
    source='''import std/env::{args_count,get_arg}
fn main() -> i32 {
    if args_count() != 4 { return 3 }
    if !get_arg(1).eq(string::from("with space")) { return 4 }
    if !get_arg(2).eq(string::from("--forge")) { return 5 }
    if !get_arg(3).eq(string::from(";touch SHOULD_NOT_EXIST")) { return 6 }
    return 7
}
'''
    def run(name,argv,cwd,code):
        r=subprocess.run(argv,cwd=cwd,env=env,capture_output=True,timeout=180);(out/(name+'.stdout')).write_bytes(r.stdout);(out/(name+'.stderr')).write_bytes(r.stderr)
        rows.append({'name':name,'argv':argv,'exit_code':r.returncode});assert r.returncode==code,(name,r.stdout,r.stderr)
    run('create',['toka','new','app'],out,0);project=out/'app';(project/'src/main.tk').write_text(source);(out/'single.tk').write_text(source)
    args=['with space','--forge',';touch SHOULD_NOT_EXIST']
    run('project-argv',['toka','run','--',*args],project,7)
    run('single-argv',['toka','run','single.tk','--',*args],out,7)
    assert not (out/'SHOULD_NOT_EXIST').exists() and not (project/'SHOULD_NOT_EXIST').exists()
    run('missing-separator',['toka','run','one.csv','two.csv'],project,2)
    (out/'plain.tk').write_text('fn main() -> i32 { return 0 }\n');run('single-no-args',['toka','run','plain.tk'],out,0)
    (project/'src/main.tk').write_text('fn main() -> i32 { return 0 }\n');run('project-no-args',['toka','run'],project,0)
    (out/'result.json').write_text(json.dumps({'result':'pass','records':rows,'scenarios':len(rows)},indent=2)+'\n')


if __name__=='__main__':main()
