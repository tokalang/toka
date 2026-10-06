#!/usr/bin/env python3
"""Reproduce published-SDK D1/D2 without modifying SDK or compiler."""
import argparse,datetime,json,os,subprocess,time
from pathlib import Path

def main():
    a=argparse.ArgumentParser();a.add_argument('--sdk',type=Path,required=True);a.add_argument('--output',type=Path,required=True);args=a.parse_args();out=args.output.resolve();out.mkdir();sdk=args.sdk.resolve()
    def run(name,source,check=False,json_output=False):
        d=out/name;d.mkdir();src=d/'main.tk';src.write_text(source)
        argv=[str(sdk/'bin/tokac'),'-I',str(sdk/'lib'),str(src),'-o',str(d/'program'),'-O0']
        if check:argv+=['--check-only']
        if json_output:argv+=['--diagnostics-json']
        t=time.monotonic_ns();r=subprocess.run(argv,cwd=d,env=dict(os.environ,TOKA_LIB=str(sdk/'lib')),capture_output=True,timeout=60);end=time.monotonic_ns()
        (d/'stdout').write_bytes(r.stdout);(d/'stderr').write_bytes(r.stderr);(d/'receipt.json').write_text(json.dumps({'argv':argv,'cwd':str(d),'exit_code':r.returncode if r.returncode>=0 else None,'signal':-r.returncode if r.returncode<0 else None,'started_monotonic_ns':t,'result_monotonic_ns':end,'execution_ms':(end-t)/1e6},indent=2))
        return r,src
    d1='import std/data_file::{ReadDataFile}\nimport std/vec::{Vec}\nfn main()->i32 {\n auto file = ReadDataFile::open(string::from("/dev/null")).unwrap()\n auto buffer# = Vec<u8>::new()\n buffer#.resize(1,0)\n auto part = file.read_at(buffer#,0:u64,1:usize)\n return 0\n}\n'
    r,src=run('D1-original',d1,check=True,json_output=True);report=json.loads(r.stdout);diag=next(x for x in report['diagnostics'] if x['code']=='E04509');fix=next(x for x in diag['fixes'] if x['applicability']=='machine-applicable')
    # Apply producer edits directly, with independently bounded line/column ranges.
    lines=d1.splitlines(keepends=True);offsets=[];n=0
    for line in lines:offsets.append(n);n+=len(line)
    edits=[]
    for e in fix['edits']:
        assert Path(e['file'])==src
        r=e['range'];start=offsets[r['start']['line']]+r['start']['character'];end=offsets[r['end']['line']]+r['end']['character'];edits.append((start,end,e['newText']))
    applied=d1
    for start,end,text in sorted(edits,reverse=True):applied=applied[:start]+text+applied[end:]
    r,_=run('D1-machine-applied',applied,check=True,json_output=True);after=json.loads(r.stdout)
    d1_reproduced=r.returncode!=0 and any(x['code']=='E0461' for x in after['diagnostics'])
    # Known source workaround is a control, not a compiler fix.
    r,_=run('D1-known-workaround',d1.replace('read_at(buffer#,','read_at(cede buffer,'),check=True,json_output=True);workaround_pass=r.returncode==0
    d2='import stdx/serde/json_document::{parse_document, Document}\nfn probe(doc:Document, object:i32)->usize { return doc.find(object,"x") }\nfn main()->i32 {\n auto doc = parse_document("{} ").unwrap()\n auto id = probe(doc,1)\n if id == 0 { return 0 }\n return 1\n}\n'
    r,_=run('D2-original',d2);d2_reproduced=b'LLVM IR Verification Failed' in r.stderr and r.returncode!=0
    r,_=run('D2-check-only',d2,check=True,json_output=True);frontend_missed=r.returncode==0
    r,_=run('D2-usize-control',d2.replace('object:i32','object:usize'));usize_pass=r.returncode==0
    result={'D1_reproduced':d1_reproduced,'D1_workaround_pass':workaround_pass,'D2_reproduced':d2_reproduced,'D2_check_only_missed':frontend_missed,'D2_usize_control_pass':usize_pass,'compiler_changed':False,'repair_proposed_at':None,'repair_verified_at':None}
    (out/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
    if not all([d1_reproduced,workaround_pass,d2_reproduced,usize_pass]):raise AssertionError('candidate differs; inspect original receipts')

if __name__=='__main__':main()
