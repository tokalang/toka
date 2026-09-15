#!/usr/bin/env python3
"""Qualify explicit new initializers; factory-call forwarding is still pending."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FILE = 'import std/io::{File}\n'
OPEN = 'auto file=File::open(string::from("/dev/null"),"r")\n'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    positive = {
        'file-box': FILE + 'shape Box(file:File)\nfn main()->i32 {\n' + OPEN +
            'auto ^box=new Box(file=cede file)\nassert(box.file.is_valid(),"live File")\nreturn 0}',
        'drop-once': FILE + 'auto drops#=0:i32\nshape Tracked(file:File)\n'
            'impl Tracked@Encap {pub file\nfn drop(self#){drops+=1}}\n'
            'shape Box(value:Tracked)\nfn main()->i32 {{\n' + OPEN +
            'auto tracked=Tracked(file=cede file)\n'
            'auto ^box=new Box(value=cede tracked)\nassert(drops==0,"not dropped early")\n'
            'assert(box.value.file.is_valid(),"File retained")\n}\nassert(drops==1,"one owning payload drop")\nreturn 0}',
        'old-default-spread': 'shape Pair(x:i32=1,y:i32=2)\nfn main()->i32 {\n'
            'auto base=Pair(..)\nauto ^first=new Pair(..)\nauto second=Pair(x=1,base.*)\n'
            'assert(first.x==1 && second.y==2,"old independent paths preserved")\nreturn 0}',
    }
    negative = {
        'default': FILE + 'shape Box(file:File,tag:i32=1)\nfn main()->i32 {\n' + OPEN +
            'auto ^box=new Box(file=cede file)\nauto valid=file.is_valid()\nreturn 0}',
        'elision': FILE + 'shape Box(file:File,tag:i32=1)\nfn main()->i32 {\n' + OPEN +
            'auto ^box=new Box(file=cede file,..)\nauto valid=file.is_valid()\nreturn 0}',
        'spread': FILE + 'shape Box(file:File,tag:i32)\nfn main()->i32 {\n' + OPEN +
            'auto base=Box(file=cede file,tag=0)\nauto ^box=new Box(tag=1,cede base.*)\nreturn 0}',
        'generic-default': FILE + 'shape Box<T>(value:T,tag:i32=1)\n'
            'fn build<T>(cede value:T)->i32 {auto ^box=new Box<T>(value=cede value,..)\nreturn 0}\n'
            'fn main()->i32 {\n' + OPEN + 'return build<File>(cede file)}',
        'duplicate': FILE + 'shape Box(first:File,second:File)\nfn main()->i32 {\n' + OPEN +
            'auto ^box=new Box(first=cede file,second=cede file)\nreturn 0}',
        'borrowed': 'shape Box(&value:i32)\nfn escape()->^Box {auto local=7:i32\n'
            'auto ^box=new Box(&value=&local)\nreturn ^box}\nfn main()->i32{return 0}',
        'raw': 'shape Box(*value:i32)\nfn main()->i32 {auto local=7:i32\n'
            'auto *p=unsafe (&local as *i32)\nauto ^box=new Box(*value=*p)\nreturn 0}',
        'type-rollback': FILE + 'shape Box(file:File,count:i32)\nfn main()->i32 {\n' + OPEN +
            'auto ^box=new Box(file=cede file,count=string::from("wrong"))\nauto valid=file.is_valid()\nreturn 0}',
        'permission': 'shape Value(n:i32)\nshape Box(~value#:Value)\nfn main()->i32 {\n'
            'auto ~owner=new Value(n=7)\nauto ^box=new Box(~value=~owner)\nreturn 0}',
    }
    with tempfile.TemporaryDirectory(prefix='toka-new-init-') as directory:
        work = Path(directory)
        def compile(source, *flags):
            return subprocess.run([str(compiler), '--workspace-node', 'new-init',
                                   '--workspace-root', str(work), str(source), *map(str, flags)],
                                  cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
        for name, code in positive.items():
            source = work / (name + '.tk'); source.write_text(code + '\n')
            normal = compile(source, '--check-only')
            shadow = compile(source, '--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == 0, (name, normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr
            binary = work / name
            built = compile(source, '-o', binary)
            assert built.returncode == 0, (name, built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            assert ran.returncode == 0, (name, ran.stdout, ran.stderr)
            print('PASS ' + name, flush=True)
        for name, code in negative.items():
            source = work / (name + '.tk'); source.write_text(code + '\n')
            normal = compile(source, '--check-only')
            shadow = compile(source, '--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == 1, (name, normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr
            if name in ('default', 'elision', 'type-rollback'):
                assert 'E0438' not in normal.stderr and 'E0410' not in normal.stderr, normal.stderr
            for flag, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
                target = work / (name + suffix)
                result = compile(source, flag, '-o', target)
                assert result.returncode == 1 and not target.exists(), (name, result.stderr)
            print('PASS rejected ' + name, flush=True)

if __name__ == '__main__':
    main()
