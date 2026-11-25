"""Compile selected pinned upstream functions with fake ports, never hardware."""
import argparse
import re
import subprocess
import tempfile
from pathlib import Path

UPSTREAM = "3acd20ca52c018f30e3a2d376bbe9bd3d1cb52f8"

PREAMBLE = r"""
#include <cstdint>
#include <cstdio>
#include <cstring>
using UCHAR=unsigned char; using USHORT=unsigned short;
using DWORD=uint32_t; using ULONG64=uint64_t; using HANDLE=int;
constexpr HANDLE INVALID_HANDLE_VALUE=-1;
enum {PORTACCESS_NONE,PORTACCESS_PAWNIO,PORTACCESS_TVICPORT};
int g_backend=PORTACCESS_PAWNIO;
constexpr UCHAR ACPI_EC_FLAG_OBF=1, ACPI_EC_FLAG_IBF=2;
constexpr UCHAR ACPI_EC_COMMAND_READ=0x80, ACPI_EC_COMMAND_WRITE=0x81;
constexpr int DEFAULT_TIMEOUT_MS=1000, DEFAULT_SLEEP_TICKS=10;
constexpr int RECOVERY_DRAIN_READS=8;
static DWORD tick=0;
DWORD GetTickCount(){return tick;}
void Sleep(int n){tick+=n;}
struct EcOpContext{USHORT ctrlPort,dataPort; UCHAR ecOffset; int ecData;};
static int writes, reads, attempts, fail_at, ready_after;
static UCHAR response;
static bool is_read;
HANDLE PawnIoHandleFor(USHORT){return 1;}
UCHAR g_tvicReadPort(USHORT){return 0;}
void g_tvicWritePort(USHORT,UCHAR){}
bool PawnIoExecute(HANDLE,const char* name,const ULONG64* in,DWORD,ULONG64* out,DWORD){
 ++attempts;
 if(attempts==fail_at)return false;
 if(std::strcmp(name,"ioctl_pio_write")==0){++writes;return true;}
 if(in[0]==0x1604){*out=(is_read && writes>=2 && ready_after>=0 && tick>=DWORD(ready_after))?1:0;}
 else{++reads;*out=(ready_after>=0 && tick>=DWORD(ready_after))?response:0x42;}
 return true;
}
void TraceEcTimeout(const EcOpContext&,int,bool){}
void TraceEcFailure(const EcOpContext&,int){}
void reset(bool read,int ready,int fail=0,UCHAR value=0x55){
 writes=reads=attempts=0;tick=0;fail_at=fail;ready_after=ready;response=value;is_read=read;
}
"""
MAIN = r"""
static int failures=0;
void check(bool result,const char* name){
 std::printf("%s %s\n",result?"PASS":"FAIL",name);
 failures+=!result;
}
int main(){
 EcOpContext ctx{0x1604,0x1600,0x84,5}; char data=0;
 reset(true,30); check(READ_EC(ctx,data,false) && (UCHAR)data==0x42 && tick==0,"legacy opt-out reads without OBF wait");
 reset(true,30); check(READ_EC(ctx,data,true) && (UCHAR)data==0x55,"delayed OBF returns fresh reply");
 reset(true,-1); data=0x33; check(!READ_EC(ctx,data,true) && data==0x33 && reads==0,"absent OBF leaves output untouched");
 reset(true,0,0,0xff); check(READ_EC(ctx,data,true) && (UCHAR)data==0xff,"valid FF is data");
 reset(true,0); check(READ_EC(ctx,data,true),"normal read succeeds");
 int read_ops=attempts;
 reset(true,0);g_backend=PORTACCESS_NONE;data=0x33;
 check(!READ_EC(ctx,data,true) && data==0x33,"no backend cannot read");g_backend=PORTACCESS_PAWNIO;
 for(int fail=1;fail<=read_ops;++fail){
  reset(true,0,fail);data=0x33;
  check(!READ_EC(ctx,data,true) && data==0x33 && attempts==fail,"read transport failure is not success");
 }
 reset(false,0);check(ExecuteEcWrite(ctx),"normal write succeeds");
 int write_ops=attempts;
 for(int fail=1;fail<=write_ops;++fail){
  reset(false,0,fail);
  check(!ExecuteEcWrite(ctx) && attempts==fail,"write transport failure is not success");
 }
 return failures?1:0;
}
"""


def extract(source, name):
    match = re.search(r"(?m)^(?:static )?(?:bool|void|UCHAR)\s+" + name + r"\(", source)
    if match is None:
        raise ValueError(f"missing definition: {name}")
    brace = source.index("{", match.end())
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path, nargs="?", default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    checkout = args.checkout.resolve()
    head = subprocess.check_output(["git", "-C", str(checkout), "rev-parse", "HEAD"], text=True).strip()
    subprocess.run(["git", "-C", str(checkout), "merge-base", "--is-ancestor", UPSTREAM, head], check=True)
    print(f"Testing checkout {head}", flush=True)
    io = (checkout / "fancontrol/portio.cpp").read_text(encoding="utf-8-sig")
    access = (checkout / "fancontrol/portaccess.cpp").read_text(encoding="utf-8-sig")
    strict = re.search(r"constexpr bool STRICT_EC_READ_OBF_HANDLING = (true|false);", io)
    opt_in = "bool strictObf" in io
    if not opt_in and strict is None:
        raise SystemExit("missing strict flag or runtime setting")
    names = ["PortAccess_TryReadByte", "PortAccess_TryWriteByte"] if "PortAccess_TryReadByte(" in access else []
    names += ["PortAccess_ReadByte", "PortAccess_WriteByte"]
    functions = [extract(access, name) for name in names]
    functions += [extract(io, name) for name in ("WaitForAllClear", "WaitForAnySet", "WaitForControllerReady",
                                                  "SendEcCommandAndAddress", "ExecuteEcRead", "ExecuteEcWrite")]
    # Source functions call the injected logging stub, not real Windows IO.
    adapter = "#define READ_EC(ctx,data,strict) ExecuteEcRead(ctx,data,strict)" if opt_in else "#define READ_EC(ctx,data,strict) ExecuteEcRead(ctx,data)"
    code = "\n".join([PREAMBLE, "" if opt_in else strict.group(0), *functions, adapter, MAIN])
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "transport.cpp"
        binary = Path(tmp) / "transport"
        source.write_text(code, encoding="utf-8")
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
