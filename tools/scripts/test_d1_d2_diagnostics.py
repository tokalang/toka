#!/usr/bin/env python3
"""Direct D1/D2 controls using a local compiler and immutable published libraries."""
import argparse,copy,datetime,json,os,re,selectors,signal,subprocess,time
from pathlib import Path

def validate_document(document, status):
    if document.get('schema')!='toka.diagnostics' or type(document.get('version')) is not int or document['version']!=2:
        raise ValueError('unsupported diagnostic protocol')
    if type(status) is not int or status not in (0,1) or type(document.get('success')) is not bool or document['success']!=(status==0):
        raise ValueError('compiler result contradicts process status')
    if type(document.get('diagnostics')) is not list:
        raise ValueError('missing diagnostic list')
    if status==1 and not any(d.get('severity')=='error' for d in document['diagnostics']):
        raise ValueError('failure has no error facts')
    return document


def main():
    a=argparse.ArgumentParser();a.add_argument('--tokac',type=Path,required=True);a.add_argument('--sdk',type=Path,required=True);a.add_argument('--evidence',type=Path,required=True);a.add_argument('--output',type=Path,required=True);args=a.parse_args();out=args.output.resolve();out.mkdir();sdk=args.sdk.resolve();compiler=args.tokac.resolve();evidence=args.evidence.resolve()
    env=dict(os.environ,TOKA_LIB=str(sdk/'lib'));results=[];events=[]
    def run(name,argv,cwd,marker=None):
        folder=out/name;folder.mkdir();argv=list(map(str,argv));start=time.monotonic_ns();record={'argv':argv,'cwd':str(cwd),'phase':'compile/check' if argv[0] in (str(compiler),str(sdk/'bin/tokac')) else 'run','start_monotonic_ns':start,'wall_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'diagnostic_observed_ns':None}
        data={'stdout':bytearray(),'stderr':bytearray()};chunks=[]
        try:
            child=subprocess.Popen(argv,cwd=cwd,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
            selector=selectors.DefaultSelector()
            for stream in data:pipe=getattr(child,stream);os.set_blocking(pipe.fileno(),False);selector.register(pipe,selectors.EVENT_READ,stream)
            while selector.get_map():
                if time.monotonic_ns()-start>90_000_000_000:os.killpg(child.pid,signal.SIGKILL);child.wait();record['actual_termination']='protection_timeout';raise TimeoutError(name)
                for key,_ in selector.select(0.1):
                    raw=os.read(key.fileobj.fileno(),65536)
                    if not raw:selector.unregister(key.fileobj);key.fileobj.close();continue
                    now=time.monotonic_ns();data[key.data].extend(raw);chunks.append({'monotonic_ns':now,'stream':key.data,'bytes':len(raw)})
                    if marker and record['diagnostic_observed_ns'] is None and marker in data[key.data]:record['diagnostic_observed_ns']=now
            selector.close();code=child.wait();record.update(exit_code=code if code>=0 else None,signal=-code if code<0 else None,returncode=code)
        except OSError as error:record.update(exit_code=None,signal=None,actual_termination='not_started',error=str(error));raise
        finally:
            record['end_monotonic_ns']=time.monotonic_ns()
            for key,value in data.items():(folder/key).write_bytes(value)
            (folder/'receipt.json').write_text(json.dumps(record,indent=2));(folder/'chunks.jsonl').write_text(''.join(json.dumps(x)+'\n' for x in chunks));results.append(record)
        return code,bytes(data['stdout']),bytes(data['stderr']),folder
    def compile(name,source,tool=compiler,check=False):
        d=out/(name+'-input');d.mkdir();file=d/'main.tk';file.write_bytes(source.encode());argv=[tool,'-I',sdk/'lib',file,'-O0','-o',d/'program','--diagnostics-json']
        if check:argv+=['--check-only']
        marker=b'"code":"E04509"' if name.startswith('D1') else b'"code":"E04510"'
        r,stdout,stderr,folder=run(name,argv,d,marker)
        return r,stdout,stderr,folder,file,d/'program'
    def require(ok,folder,contract,expected,actual):
        if not ok:
            (folder/'finding.json').write_text(json.dumps({'contract':contract,'expected':expected,'actual':actual,'logs':str(folder),'next_check':'Inspect DiagnosticEngine cede edits or Sema method argument compatibility at the recorded source location.'},indent=2));raise RuntimeError(contract+'; logs '+str(folder))
    def apply(report,text,file):
        validate_document(report,1)
        diagnostic=next(d for d in report['diagnostics'] if d['code']=='E04509');fix=next(f for f in diagnostic['fixes'] if f['applicability']=='machine-applicable');lines=text.splitlines(keepends=True);offsets=[];n=0
        for line in lines:offsets.append(n);n+=len(line)
        if not fix['edits']:raise ValueError('machine fix has no edits')
        edits=[]
        for edit in fix['edits']:
            assert Path(edit['file'])==file
            span=edit['range'];start=offsets[span['start']['line']]+span['start']['character'];end=offsets[span['end']['line']]+span['end']['character'];assert 0<=start<=end<=len(text);edits.append((start,end,edit['newText']))
        patched=text
        for start,end,value in sorted(edits,reverse=True):patched=patched[:start]+value+patched[end:]
        return patched,fix
    d1=(evidence/'reproductions/D1-original/main.tk').read_text()
    # Old producer's real edit is applied, not reconstructed from a desired fix.
    r,stdout,stderr,folder,file,binary=compile('D1-old',d1,sdk/'bin/tokac',True);require(r!=0,folder,'old failure retained','E04509',r)
    old=json.loads(stdout);patched,_=apply(old,d1,file);r,stdout,stderr,folder,_,_=compile('D1-old-machine-applied',patched,sdk/'bin/tokac',True);require(r!=0 and any(x['code']=='E0461' for x in json.loads(stdout)['diagnostics']),folder,'old edit creates illegal cede suffix','E0461',stdout.decode())
    d1_variants={'adjacent':d1,'bare':d1.replace('read_at(buffer#,','read_at(buffer,'),'space':d1.replace('read_at(buffer#,','read_at(buffer #,'),'newline':d1.replace('read_at(buffer#,','read_at(buffer\n#,'),'comment':d1.replace('read_at(buffer#,','read_at(buffer /*intent*/ #,'),'CRLF':d1.replace('\n','\r\n')}
    for label,text in d1_variants.items():
        r,stdout,stderr,folder,file,_=compile('D1-'+label,text,check=True);report=validate_document(json.loads(stdout),r);require(r!=0 and any(x['code']=='E04509' for x in report['diagnostics']),folder,'missing cede still rejected','E04509',report)
        patched,fix=apply(report,text,file)
        r,stdout,stderr,folder,_,binary=compile('D1-'+label+'-machine-applied',patched);require(r==0,folder,'all producer edits directly compile','exit0',stdout.decode()+stderr.decode())
        r,stdout,stderr,folder=run('D1-'+label+'-run',[binary],binary.parent);require(r==0,folder,'directly fixed program runs','exit0',r)
        events.append({'case':'D1-'+label,'repair_verified_ns':time.monotonic_ns(),'machine_edits':len(fix['edits'])})
        if label=='adjacent':
            rejected=[]
            for case in ('version','version-bool','success','missing-list','missing-edits','unknown-code'):
                mutated=copy.deepcopy(report)
                if case=='version':mutated['version']=99
                elif case=='version-bool':mutated['version']=True
                elif case=='success':mutated['success']=True
                elif case=='missing-list':mutated.pop('diagnostics')
                elif case=='missing-edits':mutated['diagnostics'][0]['fixes'][0].pop('edits')
                else:mutated['diagnostics'][0]['code']='future.code'
                try:apply(mutated,text,file)
                except (ValueError,KeyError,StopIteration):rejected.append(case)
                else:raise RuntimeError('invalid producer report accepted: '+case)
            (out/'consumer-negative-controls.json').write_text(json.dumps({'rejected':rejected,'legacy_single_edit_applied_in_old_control':True},indent=2))
            require(len(fix['edits'])==2,folder,'mutable named source needs suffix removal','2 edits',fix)
            insertion_only=text[:text.index('buffer#,')]+ 'cede '+text[text.index('buffer#,'):]
            r,stdout,stderr,folder,_,_=compile('D1-insertion-only-negative',insertion_only,check=True);require(r!=0 and any(x['code']=='E0461' for x in json.loads(stdout)['diagnostics']),folder,'insertion-only remains rejected','E0461',stdout.decode())
    d2=(evidence/'reproductions/D2-original/main.tk').read_text()
    r,stdout,stderr,folder,_,_=compile('D2-old-check',d2,sdk/'bin/tokac',True);require(r==0,folder,'old frontend gap retained','exit0',stdout.decode())
    r,stdout,stderr,folder,_,_=compile('D2-old-codegen',d2,sdk/'bin/tokac');require(r!=0 and b'LLVM IR Verification Failed' in stderr,folder,'old codegen failure retained','IR verifier failure',stderr.decode())
    for check in (True,False):
        r,stdout,stderr,folder,file,_=compile('D2-i32-'+('check' if check else 'codegen'),d2,check=check);report=validate_document(json.loads(stdout),r);diag=next(x for x in report['diagnostics'] if x['code']=='E04510');span=diag['primary'];start=span['range']['start']
        require(r!=0 and b'LLVM IR Verification Failed' not in stderr and span['file']==str(file) and start['line']==1 and 'i32' in diag['message'] and ('usize' in diag['message'] or 'u64' in diag['message']),folder,'front-end source and type facts','E04510 at probe argument with unsigned expected vs i32 actual',diag)
    for label,source in [('usize',d2.replace('object:i32','object:usize')),('explicit-cast',d2.replace('doc.find(object,','doc.find(object as usize,'))]:
        r,stdout,stderr,folder,_,binary=compile('D2-'+label,source);require(r==0,folder,'legal same-type/explicit-conversion path preserved','exit0',stdout.decode()+stderr.decode());r,stdout,stderr,folder=run('D2-'+label+'-run',[binary],binary.parent);require(r==0,folder,'legal program runs','exit0',r)
    events.append({'case':'D2-known-pattern','repair_verified_ns':time.monotonic_ns()})
    dynamic='trait @Index { fn take(self, value:usize)->usize }\nshape IndexImpl()\nimpl IndexImpl@Index { fn take(self, value:usize)->usize { return value } }\nfn invoke(index:dyn @Index,value:i32)->usize { return index.take(value) }\nfn main()->i32 { auto index=IndexImpl()\n auto n=invoke(index,1)\n if n==1 { return 0 } return 1 }\n'
    r,stdout,stderr,folder,_,_=compile('D2-dynamic-i32',dynamic,check=True);require(r!=0 and any(x['code']=='E04510' for x in json.loads(stdout)['diagnostics']),folder,'dynamic route rejects same signed mismatch','E04510',stdout.decode())
    r,stdout,stderr,folder,_,_=compile('D2-dynamic-usize',dynamic.replace('value:i32','value:usize'),check=True);require(r==0,folder,'dynamic same-type remains legal','exit0',stdout.decode())
    (out/'events.jsonl').write_text(''.join(json.dumps(e)+'\n' for e in events));(out/'result.json').write_text(json.dumps({'result':'pass','commands':len(results),'D1_variants':list(d1_variants),'D2_known_pattern_frontend_rejected':True,'compiler_fix_verified':True,'SDK_rebuilt':False},indent=2));print('D1/D2 controls pass:',len(results),'commands')

if __name__=='__main__':main()
