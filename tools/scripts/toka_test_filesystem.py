"""Filesystem evidence for the actual fixture directory; no Darwin format guesses."""
import json,os,re,subprocess,sys
from pathlib import Path
SOURCE=r'''#include <sys/types.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
static void string_json(const char *s){
 putchar('"');for(const unsigned char *p=(const unsigned char *)s;*p;p++){
  if(*p=='"'||*p=='\\'){putchar('\\');putchar(*p);}
  else if(*p<32)printf("\\u%04x",*p);else putchar(*p);
 }putchar('"');
}
int main(int argc,char **argv){
 char path[PATH_MAX];struct statfs fs;struct stat st;
 if(argc!=2){fprintf(stderr,"one fixture directory required\n");return 2;}
 if(!realpath(argv[1],path)){perror("realpath");return 2;}
 if(stat(path,&st)){perror("stat");return 2;}
 if(!S_ISDIR(st.st_mode)){fprintf(stderr,"fixture path is not a directory\n");return 2;}
 if(statfs(path,&fs)){perror("statfs");return 2;}
 printf("{\"schema\":\"toka.darwin.statfs.v1\",\"query_path\":");string_json(argv[1]);
 printf(",\"resolved_path\":");string_json(path);
 printf(",\"filesystem_type\":");string_json(fs.f_fstypename);
 printf(",\"mount_point\":");string_json(fs.f_mntonname);
 printf(",\"mount_source\":");string_json(fs.f_mntfromname);
 printf(",\"fsid\":[%d,%d],\"filesystem_type_number\":%u,\"mount_flags\":%u,\"device\":%llu}\n",
 (int)fs.f_fsid.val[0],(int)fs.f_fsid.val[1],(unsigned)fs.f_type,(unsigned)fs.f_flags,(unsigned long long)st.st_dev);
 return ferror(stdout)?2:0;
}
'''
def validate(data,path):
    resolved=path.resolve(strict=True)
    assert data['schema']=='toka.darwin.statfs.v1'
    assert data['query_path']==str(path) and data['resolved_path']==str(resolved)
    assert re.fullmatch(r'[A-Za-z][A-Za-z0-9_.+-]*',data['filesystem_type']),data
    assert data['device']==resolved.stat().st_dev
    assert isinstance(data['fsid'],list) and len(data['fsid'])==2 and all(type(v) is int for v in data['fsid'])
    mount=Path(data['mount_point']);assert mount.is_absolute() and mount.is_dir() and mount.stat().st_dev==data['device']
    assert data['mount_source'] and type(data['filesystem_type_number']) is int and data['filesystem_type_number']>0
    return data

def query(path,evidence):
    path=path.resolve(strict=True);assert path.is_dir()
    evidence.mkdir(parents=True,exist_ok=True)
    if sys.platform=='darwin':
        source=evidence/'statfs.c';exe=evidence/'statfs-query'
        if not exe.exists():
            source.write_text(SOURCE)
            argv=['cc','-Wall','-Wextra','-Werror','-O2',str(source),'-o',str(exe)]
            result=subprocess.run(argv,capture_output=True,timeout=30)
            (evidence/'compile.stdout').write_bytes(result.stdout);(evidence/'compile.stderr').write_bytes(result.stderr)
            (evidence/'compile.json').write_text(json.dumps({'argv':argv,'exit_code':result.returncode})+'\n')
            assert result.returncode==0,result.stderr
        argv=[str(exe),str(path)];result=subprocess.run(argv,capture_output=True,timeout=10)
        assert result.returncode==0,result.stderr
        data=validate(json.loads(result.stdout),path)
    else:
        argv=['stat','-f','-c','%T',str(path)];result=subprocess.run(argv,capture_output=True,timeout=10)
        assert result.returncode==0,result.stderr
        name=result.stdout.decode().strip();assert re.fullmatch(r'[A-Za-z][A-Za-z0-9_.+-]*',name)
        data={'schema':'toka.posix.statfs-command.v1','query_path':str(path),'resolved_path':str(path),'filesystem_type':name,'device':path.stat().st_dev}
    return {'argv':argv,'exit_code':result.returncode,'stdout':result.stdout.decode('utf-8'),'stderr':result.stderr.decode('utf-8'),'data':data,'validated':True}
