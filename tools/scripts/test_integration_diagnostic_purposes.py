#!/usr/bin/env python3
"""Guard the original rejection purpose, not a cascade from a missing binding."""
import argparse,json,os,re,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
CASES={
 'guard_no_return':'E0465', 'handle_grammar_trait_method_param_illegal':'E0492',
 'assigned_for_no_pass':'E0432', 'callable_consuming_without_cede':'E04591',
 'cede_param_double_unwrap':'E0438', 'cede_param_missing':'E04570',
 'closure_copy_capture_resource':'E04581', 'contextual_numeric_literal_overflow':'E04598',
 'implicit_owned_projection_copy':'E04641', 'match_guard_not_exhaustive':'E0553',
 'match_non_enum_requires_wildcard':'E0553', 'match_or_pattern_binding_mismatch':'E04528',
 'match_or_pattern_not_exhaustive':'E0553', 'match_variant_payload_not_exhaustive':'E0553',
}
def repaired(name,text):
    if name=='guard_no_return': return text.replace('// No return! Should trigger ERR_GUARD_MUST_DIVERGE','return 0')
    if name=='handle_grammar_trait_method_param_illegal': return text.replace('*^x','*x')
    if name=='assigned_for_no_pass': return text.replace('1 + 1 // Error:', 'pass 1 + 1 // Error:')
    if name=='callable_consuming_without_cede': return text.replace('    take() // EXPECT:', '    cede take() // EXPECT:')
    if name=='cede_param_double_unwrap': return text.replace('    res.unwrap()\n','')
    if name=='cede_param_missing': return text.replace('    consume_payload(p)','    consume_payload(cede p)').replace('    return p.val // Rejected calls must preserve the source.','    return 0')
    if name=='closure_copy_capture_resource': return text.replace('[copy env]','[cede env]')
    if name=='contextual_numeric_literal_overflow': return text.replace('accept_u8(256)','accept_u8(255)').replace('accept_i8(128)','accept_i8(127)').replace('accept_u8(-1)','accept_u8(0)')
    if name=='implicit_owned_projection_copy': return text.replace('auto copied = owner.resource','auto copied = cede owner.resource')
    if name=='match_guard_not_exhaustive': return text.replace(' if v > 0','')
    if name=='match_non_enum_requires_wildcard': return text.replace('    }\n    return 0','        _ => { pass 0 }\n    }\n    return 0')
    if name=='match_or_pattern_binding_mismatch': return text.replace('auto Expr::Int(n) | auto Expr::Flag(flag) => {','auto Expr::Int(n) => { pass 1 }\n        auto Expr::Flag(flag) => {')
    if name=='match_or_pattern_not_exhaustive': return text.replace('auto Mode::Read | auto Mode::Write','auto Mode::Read | auto Mode::Write | auto Mode::Execute')
    if name=='match_variant_payload_not_exhaustive': return text.replace('auto Packet::Data(true) =>','auto Packet::Data(_) =>')
    raise AssertionError(name)
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--build-dir',required=True);args=parser.parse_args()
    compiler=Path(args.build_dir).resolve()/'bin/tokac';env=dict(os.environ,TOKA_LIB=str(ROOT/'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-diag-purpose-') as directory:
        work=Path(directory)
        def run(source,*flags):
            scope=ROOT if source.is_relative_to(ROOT) else work
            return subprocess.run([str(compiler),'--workspace-node','toka-tests-v1','--workspace-root',str(scope),str(source),*map(str,flags)],cwd=ROOT,env=env,text=True,capture_output=True,timeout=60)
        for name,code in CASES.items():
            source=ROOT/'tests/fail'/(name+'.tk')
            normal=run(source,'--check-only');shadow=run(source,'--check-only','--non-call-transfer-shadow=json')
            assert normal.returncode==shadow.returncode==1 and normal.stderr==shadow.stderr,(name,normal.stderr,shadow.stderr)
            assert set(re.findall(r'error\[(E\d+)\]',normal.stderr))=={code},(name,normal.stderr)
            json.loads(shadow.stdout)
            if name=='contextual_numeric_literal_overflow': assert normal.stderr.count('error[E04598]')==3,normal.stderr
            for mode,suffix in [('-c','.o'),('--emit-llvm','.ll')]:
                output=work/(name+suffix);failed=run(source,mode,'-o',output)
                assert failed.returncode==1 and not output.exists(),(name,failed.stderr)
            positive=work/(name+'_corrected.tk');positive.write_text(repaired(name,source.read_text()))
            admitted=run(positive,'--check-only')
            assert admitted.returncode==0,(name,admitted.stderr)
            print('PASS '+name+' '+code+' with admitted correction',flush=True)
if __name__=='__main__':main()
