// Optional host benchmark. This does not measure ESP32 heap or physical latency.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict');
const {spawnSync}=require('node:child_process');
const reference=require('./baseline-9e4434c.json');
const output=fs.mkdtempSync(path.join(os.tmpdir(),'rfid-benchmark-'));
const results={baseline:{},optimized:{}};
const method={...reference.method,measured:'Pure status body expression (commonStatus + reader attendanceJson + closing brace), consumed via volatile length; no WebServer/socket transport timing',note:'Same final fake headers for both commits; std::string adapter, host-only evidence, not ESP32 heap/fragmentation or physical performance.'};
for(const variant of ['baseline','optimized'])for(const mode of ['lector']){
 let sketch=path.resolve(__dirname,'..',mode,mode+'.ino');
 if(variant==='baseline'){
  const original=spawnSync('git',['show',reference.commit+':'+mode+'/'+mode+'.ino'],{cwd:path.resolve(__dirname,'..'),encoding:'utf8'});
  assert.equal(original.status,0,'Baseline needs Git history: '+original.stderr);
  sketch=path.join(output,'baseline-'+mode+'.ino');fs.writeFileSync(sketch,original.stdout);
 }
 const include=JSON.stringify(sketch);
 const source=`
#include <chrono>
#include <iostream>
#include <string>
#include <LittleFS.h>
#include ${include}
volatile size_t byteSink=0;
void prepare(int count,bool dense){
 Fake::reset();FakeFS::reset();FakeHTTP::reset();
 stage="INICIO";message="Iniciando RC522";trace="";clearCard();
 readerConnected=false;readerVersion=0;lastProbeMs=lastPollMs=lastDetectionErrorMs=0;
 lastCardUid="";lastCardSeenMs=0;suppressAttendance=false;
#ifdef BENCH_OPTIMIZED
 operationState=OperationState::IDLE;historyRevision=stateRevision=0;bootId="";
#else
 pending=diagnosticPending=writing=false;
#endif
 attemptHead=attemptCount=activeSlot=0;attemptSerial=0;attemptOpen=false;
 for(auto& a:attempts)a=Attempt();
 waitingNotice=false;requestedId="";armedAtMs=0;
#ifndef WRITER_FIRMWARE
 currentRecord={};storageReady=sessionValid=false;
#ifdef BENCH_OPTIMIZED
 attendanceState=AttendanceState::CLOSED;
#else
 sessionActive=closePending=false;
#endif
 sessionPath="";attendanceMessage="[CERRADO] Pase tarjeta de docente.";
 openedMs=lastCloseAttempt=clockAtMs=0;clockEpoch=0;
#endif
 setup();
 for(int i=0;i<count;++i){
  beginAttempt("TEST");
  if(dense){for(int line=0;line<35;++line)report("TEST","abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 bounds");}
  else report("TEST","historical error preserved");
  endAttempt();
 }
}
String statusBody(){
#ifdef BENCH_OPTIMIZED
 return commonStatus("rfid")+",\\\"attendance\\\":"+attendanceJson()+"}";
#else
 return commonStatus("lector")+",\\\"attendance\\\":"+attendanceJson()+"}";
#endif
}
int main(){
 for(bool dense:{false,true})for(int count:{0,20}){
  if(dense&&count==0)continue;
  prepare(count,dense);const size_t bytes=statusBody().length();
  for(int i=0;i<100;++i){String payload=statusBody();byteSink+=payload.length();}
  for(int sample=0;sample<5;++sample){
   auto start=std::chrono::steady_clock::now();
   for(int i=0;i<10000;++i){String payload=statusBody();byteSink+=payload.length();}
   const double elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
   size_t peopleBytes=0,historyBytes=0;
#ifdef BENCH_OPTIMIZED
   sendPeople();peopleBytes=FakeHTTP::wire.empty()?server.body.length():FakeHTTP::decoded().first.length();
   sendAttempts();historyBytes=FakeHTTP::wire.empty()?server.body.length():FakeHTTP::decoded().first.length();
#endif
   std::cout<<"{\\"attempts\\":"<<count<<",\\"dense\\":"<<(dense?"true":"false")<<",\\"statusBytes\\":"<<bytes
    <<",\\"peopleBytes\\":"<<peopleBytes<<",\\"historyBytes\\":"<<historyBytes<<",\\"usPerStatus\\":"<<elapsed/10000<<"}"<<std::endl;
  }
 }
}
`;
 const cpp=path.join(output,variant+'-'+mode+'.cpp'),binary=path.join(output,variant+'-'+mode);
 fs.writeFileSync(cpp,source);
 const args=['-std=c++17','-O2','-DNDEBUG','-I'+path.join(__dirname,'fakes'),cpp,'-o',binary];
 if(variant==='optimized')args.push('-DBENCH_OPTIMIZED');
 const build=spawnSync('g++',args,{encoding:'utf8'});assert.equal(build.status,0,build.stderr);
 const run=spawnSync(binary,[],{encoding:'utf8'});assert.equal(run.status,0,run.stderr);
 const rows=run.stdout.trim().split('\n').map(JSON.parse),groups=[];
 for(const row of rows){
  let group=groups.find(g=>g.attempts===row.attempts&&g.dense===row.dense);
  if(!group){group={attempts:row.attempts,dense:row.dense,statusBytes:row.statusBytes,peopleBytes:row.peopleBytes,historyBytes:row.historyBytes,statusUs:[]};groups.push(group);}
  group.statusUs.push(row.usPerStatus);
 }
 results[variant][mode]=groups;
}
console.log(JSON.stringify({method,originalHandlerBaseline:reference,...results,artifacts:output},null,2));
