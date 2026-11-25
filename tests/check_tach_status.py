"""Offline IPC tach status tests using the actual header and publisher functions."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'fancontrol/sharedstate.cpp').read_text(encoding='utf-8-sig')
def extract(name):
    start = source.index('void ' + name + '(')
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
code = r"""
#include <cstdint>
#include <cstddef>
#include <cstdio>
using LONG = int32_t;
#include "sharedstate.h"
static FCSHARED state{};
static FCSHARED* s_view = &state;
static bool s_hasTachFields = true;
LONG InterlockedExchange(volatile LONG* p,LONG v){LONG old=*p;*p=v;return old;}
""" + extract('SharedState_PublishTach') + '\n' + extract('SharedState_MarkTachStale') + r"""
static_assert(offsetof(FCSHARED,traceLines)==116,"legacy trace offset");
static_assert(offsetof(FCSHARED,tachVersion)==7796,"legacy prefix size");
int main(){
 int failures=0;
 auto check=[&](bool b,const char* n){printf("%s %s\n",b?"PASS":"FAIL",n);failures+=!b;};
 check(state.tachSample1==0&&state.tachSample2==0,"initial unknown");
 SharedState_PublishTach(0xae,0xae,0x50,0x12,true);
 check(state.tachSample1==PackTachSample(0xaeae,TACH_INVALID),"invalid 44718 is explicit");
 check(state.tachSample2==PackTachSample(0x1250,TACH_FRESH_OBF),"strict successful fan2");
 SharedState_PublishTach(0,0,0xff,0x1f,false);
 check(state.tachSample1==PackTachSample(0,TACH_FRESH_UNVERIFIED),"zero is valid stopped fan");
 check(state.tachSample2==PackTachSample(0x1fff,TACH_FRESH_UNVERIFIED),"legacy ceiling is accepted but unverified");
 SharedState_MarkTachStale();
 check(state.tachSample2==PackTachSample(0x1fff,TACH_STALE),"failed sample retains raw data but marks stale");
 SharedState_PublishTach(0x50,0x12,0xff,0xff,true);
 check(state.tachSample1==PackTachSample(0x1250,TACH_FRESH_OBF),"successful sample recovers freshness");
 check(state.tachSample2==PackTachSample(0xffff,TACH_INVALID),"FFFF is invalid tach not a transport error");
 auto old=state.tachSample1; s_hasTachFields=false;
 SharedState_PublishTach(0,0,0,0,true);SharedState_MarkTachStale();
 check(state.tachSample1==old,"legacy mapping extension untouched");
 s_view=nullptr;s_hasTachFields=true;
 SharedState_PublishTach(0,0,0,0,true);SharedState_MarkTachStale();
 check(state.tachSample1==old,"absent mapping is harmless");
 return failures?1:0;
}
"""
with tempfile.TemporaryDirectory() as d:
    p=Path(d)/'tach.cpp';p.write_text(code)
    exe=p.with_suffix('')
    subprocess.run(['g++','-std=c++17','-I',str(root/'fancontrol'),str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
