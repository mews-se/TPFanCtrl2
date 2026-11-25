from pathlib import Path
import subprocess
import sys
import tempfile

source=Path(sys.argv[1] if len(sys.argv)>1 else str(Path(__file__).resolve().parents[1]/'fancontrol/fanstuff.cpp')).read_text(encoding='utf-8-sig')
body=source[source.index('bool FANCONTROL::SetFan('):source.index('bool FANCONTROL::SetHdw(')]
raw=source.index('bool FANCONTROL::ReadEcRaw(')
brace=source.index('{',raw)
depth,end=1,brace+1
while depth:
    depth+=(source[end]=='{')-(source[end]=='}')
    end+=1
body+=source[raw:end]
body+=source[source.index("bool FANCONTROL::SampleMatch("):source.index("bool FANCONTROL::LockECAccess(")]
body+=source[source.index("void FANCONTROL::SmartControl("):source.index("bool FANCONTROL::FanSpunUp(")]
if 'bool FANCONTROL::SelectFan(' in source:
    a=source.index('bool FANCONTROL::SelectFan(')
    b=source.index('bool FANCONTROL::SetFan(',a)
    body=source[a:b]+body
preamble=r"""
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <vector>
using std::size_t;
int sprintf_s(char* buf,size_t len,const char* fmt,...){
 va_list a;va_start(a,fmt);int n=vsnprintf(buf,len,fmt,a);va_end(a);return n;
}
void Sleep(int){}
void Beep(int,int){}
void SetDlgItemText(int,int,const char*){}
void PostMessage(int,int,int,int){}
constexpr int WM__GETDATA=1;
constexpr char TP_ECOFFSET_FAN_SWITCH=0x31,TP_ECOFFSET_FAN=0x2f,TP_ECVALUE_SELFAN1=0,TP_ECVALUE_SELFAN2=1;
constexpr int TRUE=1,FALSE=0;
constexpr char TP_ECOFFSET_FANSPEED=0x84,TP_ECOFFSET_TEMP0=0x78,TP_ECOFFSET_TEMP1=0xc0;
bool g_clientMode=false;
bool PortAccess_AllowsPort(int){return false;}
char PortAccess_ReadByte(int){return 0;}
void PortAccess_WriteByte(int,char){}
class FANCONTROL{
public:
 int FanBeepFreq=0,FanBeepDura=0,IndSmartLevel=0,hwndDialog=0;
 struct {int temp2=0;} SmartLevels2[1];
 bool ActiveMode=true,FinalSeen=false,SingleFan=true;
 bool FanCommandPending=false;
 int MaxTemp=60,PreviousMode=2,Lev64Norm=0;
 struct {int temp,fan,hystUp,hystDown;} SmartLevels[3]={{50,5,0,0},{70,7,0,0},{-1,0,0,0}};
 bool FanSpunUp(){return true;}
 void SmartControl();
 struct FCSTATE{
  char FanCtrl=5,Fan2Ctrl=5,Fan1SpeedLo=0,Fan1SpeedHi=0,Fan2SpeedLo=0,Fan2SpeedHi=0,Sensors[12]{};
  int SensorAddr[12]{};
  const char* SensorName[12]{};
 } State;
 bool UseTWR=false,ShowBiasedTemps=false,NoExtSensor=true;
 const char* gSensorNames[12]{};
 struct {int offs=0;} SensorOffset[12];
 std::vector<int> offsets;
 char CurrentStatus[32]="";
 int scenario=0,selector=0,selectorWrites=0,levelWrites=0,reads=0,selectorByte=0xa0;
 char level[2]={5,5};
 bool locked=false;
 void CurrentDateTimeLocalized(char* p,size_t){*p=0;}
 bool LockECAccess(){locked=true;return true;}
 void FreeECAccess(){locked=false;}
 void Trace(const char*){}
 bool WriteByteToEC(int off,char value){
  if(off==TP_ECOFFSET_FAN_SWITCH){++selectorWrites;if(scenario==5)return false;if(scenario==8)return true;selectorByte=(unsigned char)value;selector=selectorByte&1;return true;}
  ++levelWrites;
  if(scenario==9 && selector==1)return false;
  if(scenario==1 || scenario==5)return false;
  level[selector]=value;return true;
 }
 bool ReadByteFromEC(int off,char* value){
  ++reads; offsets.push_back(off);
  if(scenario==2 || (scenario==7 && off==0x85))return false;
  if(off==TP_ECOFFSET_FAN_SWITCH){*value=selectorByte;return true;}
  if(off==0x84){*value=selector?0xa0:0x50;return true;}
  if(off==0x85){*value=selector?0x10:0x12;return true;}
  if(off!=TP_ECOFFSET_FAN){*value=55;return true;}
  *value=((scenario==3 && selector==0) || (scenario==6 && reads==1))?4:level[selector];
  return true;
 }
 bool SelectFan(char);
 bool SetFan(const char*,int,bool=false);
 bool ReadEcRaw(FCSTATE*);
 bool SampleMatch(FCSTATE*,FCSTATE*);
};
"""
main=r"""
int main(){
 int failures=0;
 auto check=[&](bool b,const char* name){printf("%s %s\n",b?"PASS":"FAIL",name);failures+=!b;};
 {FANCONTROL f;check(f.SetFan("Manual",5,false)&&!f.locked,"ordinary manual command");}
 {FANCONTROL f;f.scenario=1;check(!f.SetFan("Manual",5,false),"failed write not confirmed by old matching byte");}
 {FANCONTROL f;f.scenario=2;check(!f.SetFan("Manual",5,false),"failed read not confirmed by stale state");}
 {FANCONTROL f;f.SingleFan=false;f.scenario=3;check(!f.SetFan("Manual",5,false),"both fans must match");}
 {FANCONTROL f;check(f.SetFan("Manual",5,false)&&f.selectorWrites==0,"single fan does not write selector");}
 {FANCONTROL f;f.SingleFan=false;f.scenario=5;check(!f.SetFan("Manual",5,false),"failed selector not confirmed");}
 {FANCONTROL f;f.State.FanCtrl=0x80;f.scenario=2;check(!f.SetFan("BIOS",0x80,true)&&!f.FinalSeen,"failed handback does not mark final");}
 {FANCONTROL f;f.scenario=6;check(f.SetFan("Manual",5,false)&&f.reads==2,"retry after confirmed mismatch");}
 {FANCONTROL f;f.State.FanCtrl=5;f.level[0]=4;check(f.SetFan("BIOS",0x80,true)&&f.FinalSeen&&(unsigned char)f.State.FanCtrl==0x80,"successful fresh handback");}
 {FANCONTROL f;f.ActiveMode=false;check(!f.SetFan("Manual",5,false)&&f.levelWrites==0&&f.selectorWrites==0,"passive command is ignored");}
 {FANCONTROL f;FANCONTROL::FCSTATE state;
  check(f.ReadEcRaw(&state)&&f.selectorWrites==0&&state.Fan1SpeedLo==0x50&&state.Fan1SpeedHi==0x12&&state.Fan2SpeedLo==0x50&&state.Fan2SpeedHi==0x12,"single fan raw read is write-free");
  check(f.offsets.size()>=3&&f.offsets[1]==0x84&&f.offsets[2]==0x85,"tach low before high");}
 {FANCONTROL f;FANCONTROL::FCSTATE state;f.SingleFan=false;
  check(f.ReadEcRaw(&state)&&f.selectorWrites>=1&&state.Fan1SpeedLo==0x50&&state.Fan2SpeedLo==0xa0,"dual fan read selects each fan");}
 {FANCONTROL f;FANCONTROL::FCSTATE state;f.scenario=7;
  check(!f.ReadEcRaw(&state)&&f.reads==3,"failed tach high byte aborts sample");}
 {FANCONTROL f;f.SingleFan=false;check(f.SetFan("Manual",5,false)&&(f.selectorByte&0xfe)==0xa0,"dual selection preserves unrelated bits");}
 {FANCONTROL f;FANCONTROL::FCSTATE state;f.SingleFan=false;f.selector=1;f.selectorByte=0xa1;f.level[0]=4;f.level[1]=5;
  check(f.ReadEcRaw(&state)&&state.FanCtrl==4&&(f.selectorByte&0xfe)==0xa0,"raw state belongs to fan one and unrelated bits survive");}
 {FANCONTROL f;f.SingleFan=false;f.scenario=2;check(!f.SetFan("Manual",5,false)&&f.levelWrites==0&&!f.locked,"selector read failure aborts before command");}
 {FANCONTROL f;f.SingleFan=false;f.scenario=8;f.selector=1;f.selectorByte=0xa1;
  check(!f.SetFan("Manual",5,false)&&f.levelWrites==0&&!f.locked,"unconfirmed selector change aborts before fan write");}
 {FANCONTROL f;f.SingleFan=false;f.scenario=9;f.MaxTemp=75;
  bool failed=!f.SetFan("Smart",7,false);
  f.scenario=0;
  bool polled=f.ReadEcRaw(&f.State);
  bool partial=f.level[0]==7&&f.level[1]==5;
  f.SmartControl();
  check(failed&&polled&&partial&&f.level[0]==7&&f.level[1]==7,"smart retries partial dual command after fan1 poll");}
 {FANCONTROL f;f.SingleFan=false;f.scenario=9;f.MaxTemp=60;
  bool failed=!f.SetFan("Smart",7,false);
  f.scenario=0;bool polled=f.ReadEcRaw(&f.State);
  f.SmartLevels[1].hystDown=20; // hold current level inside hysteresis
  f.SmartControl();
  check(failed&&polled&&f.level[0]==7&&f.level[1]==7,"smart reconciles pending command inside hysteresis");}
 {FANCONTROL f;f.SingleFan=false;f.level[1]=0;
  bool polled=f.ReadEcRaw(&f.State);f.SmartControl();
  check(polled&&f.level[0]==5&&f.level[1]==5,"initial dual mismatch reconciled");}
 {FANCONTROL f;f.SingleFan=false;bool polled=f.ReadEcRaw(&f.State);f.SmartControl();
  check(polled&&f.levelWrites==0,"matching dual levels need no writes");
  f.level[1]=0;polled=f.ReadEcRaw(&f.State);f.SmartControl();
  check(polled&&f.level[1]==5,"external fan2 drift reconciled");}
 {FANCONTROL f;f.SingleFan=false;FANCONTROL::FCSTATE a,b;
  bool ok=f.ReadEcRaw(&a);f.level[1]=0;ok=ok&&f.ReadEcRaw(&b);
  check(ok&&!f.SampleMatch(&a,&b),"sample matching includes second control level");}
 return failures?1:0;
}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test.cpp';p.write_text(preamble+body+main)
 exe=p.with_suffix('')
 subprocess.run(['g++','-std=c++17','-funsigned-char',str(p),'-o',str(exe)],check=True)
 result=subprocess.run([str(exe)])
 raise SystemExit(result.returncode)
