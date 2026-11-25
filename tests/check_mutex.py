"""Exercise the real lock wrappers against deterministic Windows ownership semantics."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]/"fancontrol"
def extract(file, name):
    source = (root/file).read_text(encoding="utf-8-sig")
    start = source.index(name)
    brace = source.index("{", start)
    depth, end = 1, brace+1
    while depth:
        depth += (source[end]=="{")-(source[end]=="}")
        end += 1
    return source[start:end]
lock = extract("winstuff.cpp","int\nMUTEXSEM::Lock(")
unlock = extract("winstuff.cpp","void\nMUTEXSEM::Unlock(")
acquire = extract("fanstuff.cpp","bool FANCONTROL::LockECAccess(")
release = extract("fanstuff.cpp","void FANCONTROL::FreeECAccess(")
signature = "int Lock(int, bool* abandoned=nullptr);" if "bool* abandoned" in lock else "int Lock(int);"
code = r"""
#include <cstdio>
#include <cstring>
using DWORD=unsigned long;
constexpr int TRUE=1,FALSE=0,WAIT_OBJECT_0=0,WAIT_ABANDONED=128,WAIT_TIMEOUT=258,WAIT_FAILED=-1;
int owner=0, depth=0, thread=1, waits=0, logs=0;
bool abandoned=false, failed=false;
int WaitForSingleObject(int,int){
 ++waits;
 if(failed)return WAIT_FAILED;
 if(owner && owner!=thread)return WAIT_TIMEOUT;
 owner=thread;++depth;
 if(abandoned){abandoned=false;return WAIT_ABANDONED;}
 return WAIT_OBJECT_0;
}
int ReleaseMutex(int){if(owner!=thread||depth==0)return 0;if(--depth==0)owner=0;return 1;}
void Sleep(int){}
void exitThread(){if(owner==thread){owner=depth=0;abandoned=true;}}
class MUTEXSEM{int hmux=1;public:
""" + signature + r"""
 void Unlock();
};
class FANCONTROL{public:MUTEXSEM EcAccess;bool LockECAccess();void FreeECAccess();
 void Trace(const char* s){if(strstr(s,"abandoned")||strstr(s,"Abandoned"))++logs;}};
""" + lock + unlock + acquire + release + r"""
void reset(){owner=depth=waits=logs=0;thread=1;abandoned=failed=false;}
int main(){
 int failures=0;
 auto check=[&](bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);failures+=!ok;};
 FANCONTROL f;
 reset();bool ok=f.LockECAccess();if(ok)f.FreeECAccess();
 check(ok&&depth==0&&waits==1&&logs==0,"normal ownership balanced");
 reset();abandoned=true;ok=f.LockECAccess();if(ok)f.FreeECAccess();
 check(ok&&depth==0&&waits==1&&logs==1,"abandoned acquisition balanced and logged");
 exitThread();thread=2;ok=f.LockECAccess();if(ok)f.FreeECAccess();
 check(ok&&!abandoned&&depth==0&&logs==1&&waits==2,"worker exit does not perpetuate abandonment");
 reset();abandoned=true;ok=f.LockECAccess();if(ok)f.FreeECAccess();
 thread=2;ok=f.LockECAccess();if(ok)f.FreeECAccess();
 check(ok&&depth==0&&waits==2&&logs==1,"dialog acquisition does not block next worker");
 reset();owner=2;depth=1;ok=f.LockECAccess();
 check(!ok&&owner==2&&depth==1&&waits==10,"timeout never grants ownership");
 reset();failed=true;ok=f.LockECAccess();
 check(!ok&&depth==0&&waits==10,"failed wait never grants ownership");
 reset();abandoned=true;ok=f.LockECAccess();bool nested=f.LockECAccess();
 if(nested)f.FreeECAccess();if(ok)f.FreeECAccess();
 check(ok&&nested&&depth==0&&waits==2&&logs==1,"intentional recursive acquisition balanced");
 return failures?1:0;
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/"mutex.cpp";p.write_text(code)
 exe=p.with_suffix("")
 subprocess.run(["g++","-std=c++17",str(p),"-o",str(exe)],check=True)
 raise SystemExit(subprocess.run([str(exe)]).returncode)
