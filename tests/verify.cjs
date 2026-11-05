// Runs the actual .ino handlers with a stateful RC522 test double.
// Optional Chromium tests exercise the real pages and HTTP requests.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const assert = require('node:assert/strict');
const http = require('node:http');
const readline = require('node:readline');
const {spawn, spawnSync} = require('node:child_process');
const CARDS={ALU_01:'A7F3C901',ALU_02:'A7F3C902',ALU_03:'A7F3C903',ALU_04:'A7F3C904',ALU_05:'A7F3C905',ALU_06:'A7F3C906',PROF_01:'B8D4E101',PROF_02:'B8D4E102'};
const output = fs.mkdtempSync(path.join(os.tmpdir(), 'rfid-regression-'));
const failures = [];
let passed = 0;
const hex = value => Buffer.from(value).toString('hex') || '-';
const cardBlock = id => Buffer.concat([Buffer.from(id), Buffer.alloc(16 - Buffer.byteLength(id))]).toString('hex');

class Firmware {
  constructor(mode) {
    const binary = path.join(output, mode);
    const args = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-g',
      '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
      '-I'+path.join(__dirname, 'fakes'), path.join(__dirname, 'driver.cpp'), '-o', binary];
    const result = spawnSync('g++', args, {encoding:'utf8'});
    assert.equal(result.status, 0, result.stderr);
    this.pending = [];
    this.errors = '';
    this.child = spawn(binary, [], {stdio:['pipe','pipe','pipe']});
    this.child.stderr.on('data', data => {this.errors += data;});
    readline.createInterface({input:this.child.stdout}).on('line', line => {
      const request = this.pending.shift();
      if(!request) throw Error('Unrequested firmware output');
      try {request.resolve(JSON.parse(line));} catch(error) {request.reject(error);}
    });
    this.child.on('exit', code => {
      this.exited = true;
      for(const item of this.pending.splice(0)) item.reject(Error('Firmware exited '+code+': '+this.errors));
    });
  }
  command(line) {
    if(this.exited) return Promise.reject(Error('Firmware is not running: '+this.errors));
    return new Promise((resolve,reject) => {
      this.pending.push({resolve,reject});
      this.child.stdin.write(line+'\n');
    });
  }
  reset() {return this.command('RESET');}
  config(key,value) {return this.command('CONFIG '+key+' '+Number(value));}
  card(present,bytes='-') {return this.command('CARD '+Number(present)+' '+bytes);}
  tick(ms=101) {return this.command('TIME '+ms);}
  get(route='/api/status') {return this.command('HTTP GET '+route);}
  async people(){return JSON.parse((await this.get('/api/people')).response).people;}
  async history(){return JSON.parse((await this.get('/api/attempts')).response);}
  post(route,id='',confirm='') {
    return this.command('HTTP POST '+route+' '+hex(id)+' '+hex(confirm));
  }
  arm(id='ALU_01',confirm='yes') {return this.post('/api/write',id,confirm);}
  async stop() {
    if(this.exited) return;
    this.child.stdin.end();
    await new Promise(resolve => this.child.once('exit',resolve));
    assert.equal(this.errors, '', this.errors);
  }
}

async function check(name, action) {
  try {await action();++passed;}
  catch(error) {failures.push(name+': '+error.message);console.error('FAIL '+name+': '+error.message);}
}
function expectStage(result, stage) {
  assert.equal(result.status.stage, stage);
  assert.equal(result.safe, true, 'A protected card block was modified');
}
async function nativeTests(writer,reader) {
  await check('no automatic write at boot or while idle',async()=>{
    await writer.reset();await writer.card(true);
    const r=await writer.tick(10000);assert.equal(r.writes,0);assert.equal(r.attempts,0);
  });
  for(const [name,id,confirm] of [
    ['empty ID','','yes'],['17 bytes','12345678901234567','yes'],
    ['space','A B','yes'],['accent','Á','yes'],['HTML','<img>','yes'],
    ['missing consent','ALU_01',''],['wrong consent','ALU_01','no']
  ]) await check('reject '+name,async()=>{
    await writer.reset();const r=await writer.arm(id,confirm);
    assert.equal(r.code,400);assert.equal(r.status.pending,false);assert.equal(r.attempts,0);
  });
  for(const id of Object.keys(CARDS)) await check('write valid ID '+id,async()=>{
    await writer.reset();await writer.card(true);
    assert.equal((await writer.arm(id)).code,202);
    const r=await writer.tick();expectStage(r,'ESCRITURA_OK');
    assert.equal(r.status.cardId,CARDS[id]);assert.equal(r.writes,1);
    assert.equal(r.block.replaceAll(' ',''),cardBlock(CARDS[id]).toUpperCase());
    assert.equal((await writer.tick()).writes,1,'A request wrote twice');
  });
  await check('overwrite a held card and zero old tail',async()=>{
    await writer.reset();await writer.card(true,cardBlock('1234567890123456'));await writer.arm('ALU_01');
    expectStage(await writer.tick(),'ESCRITURA_OK');
    await writer.arm('ALU_02');const r=await writer.tick();
    expectStage(r,'ESCRITURA_OK');assert.equal(r.writes,2);
    assert.equal(r.block.replaceAll(' ',''),cardBlock(CARDS.ALU_02).toUpperCase());
    await reader.reset();await reader.card(true,r.block.replaceAll(' ',''));
    expectStage(await reader.tick(),'LECTURA_OK');
    const independent=await reader.get();
    assert.equal(independent.status.cardId,CARDS.ALU_02);assert.equal(independent.attempts,0);
  });
  for(const [fault,stage,writes] of [
    ['unsupported','TIPO_NO_COMPATIBLE',0],['authFail','ERROR_AUTENTICACION',0],
    ['readFail','ERROR_LECTURA',0],['writeFail','ERROR_ESCRITURA',0],
    ['verifyFail','ERROR_VERIFICACION',1],['corrupt','ERROR_VERIFICACION',1],
    ['removeOnWrite','ERROR_ESCRITURA',0],['lostAck','ERROR_ESCRITURA',1],
    ['selectFail','ERROR_SELECCION',0],['detectFail','ERROR_DETECCION',0]
  ]) await check('handle '+fault,async()=>{
    await writer.reset();await writer.config(fault,true);await writer.card(true);
    await writer.arm();const r=await writer.tick();expectStage(r,stage);
    assert.equal(r.writes,writes);assert.equal(r.status.pending,false);
  });
  await check('short read is not described as success',async()=>{
    await writer.reset();await writer.config('shortRead',true);await writer.card(true);await writer.arm();
    const r=await writer.tick();expectStage(r,'ERROR_LECTURA');
    assert(!r.status.message.includes('Success'));assert.equal(r.attempts,0);
  });
  await check('retry selection failure without removing card',async()=>{
    await writer.reset();await writer.card(true);await writer.config('selectFail',true);await writer.arm();
    expectStage(await writer.tick(),'ERROR_SELECCION');
    await writer.config('selectFail',false);await writer.arm();
    expectStage(await writer.tick(),'ESCRITURA_OK');
  });
  await check('invalid prepare preserves the last successful result',async()=>{
    await writer.reset();await writer.card(true);await writer.arm();expectStage(await writer.tick(),'ESCRITURA_OK');
    const r=await writer.arm('invalid space');assert.equal(r.code,400);expectStage(r,'ESCRITURA_OK');
  });
  await check('duplicate prepare cannot replace pending ID',async()=>{
    await writer.reset();await writer.arm('ALU_01');const r=await writer.arm('ALU_02');
    assert.equal(r.code,409);assert.equal(r.status.requestedId,CARDS.ALU_01);
  });
  await check('cancel before detection prevents future writing',async()=>{
    await writer.reset();await writer.arm();const c=await writer.post('/api/cancel');
    expectStage(c,'CANCELADO');await writer.card(true);assert.equal((await writer.tick()).attempts,0);
  });
  await check('late cancel preserves completed write',async()=>{
    await writer.reset();await writer.card(true);await writer.arm();await writer.tick();
    const r=await writer.post('/api/cancel');assert.equal(r.code,409);expectStage(r,'ESCRITURA_OK');
  });
  await check('no card, waiting notice, and exact timeout',async()=>{
    await writer.reset();await writer.arm();let r=await writer.tick(5000);
    expectStage(r,'ESPERANDO_TARJETA');assert.equal(r.status.remainingSeconds,55);
    r=await writer.tick(54999);assert.equal(r.status.pending,true);
    r=await writer.tick(1);expectStage(r,'TIEMPO_AGOTADO');assert.equal(r.attempts,0);
  });
  await check('no late write when card arrives at timeout',async()=>{
    await writer.reset();await writer.arm();await writer.card(true);
    const r=await writer.tick(60000);expectStage(r,'TIEMPO_AGOTADO');assert.equal(r.attempts,0);
  });
  for(const version of [0,255])await check('reader disconnected register '+version,async()=>{
    await writer.reset();await writer.config('version',version);
    const r=await writer.arm();assert.equal(r.code,503);assert.equal(r.status.connected,false);
  });
  await check('disconnect cancels a pending operation',async()=>{
    await writer.reset();await writer.arm();await writer.config('version',0);
    const r=await writer.tick(2001);expectStage(r,'ERROR_LECTOR');
    assert.equal(r.status.pending,false);assert.equal(r.attempts,0);
  });
  await check('millis wrap preserves timeout',async()=>{
    await writer.reset();await writer.command('CLOCK 4294967280');await writer.arm();
    let r=await writer.tick(1000);assert.equal(r.status.pending,true);
    r=await writer.tick(59000);expectStage(r,'TIEMPO_AGOTADO');assert.equal(r.attempts,0);
  });
  await check('reader suppresses held card, rereads after removal',async()=>{
    await reader.reset();await reader.card(true,cardBlock('PRUEBA_01'));
    let r=await reader.tick();expectStage(r,'LECTURA_OK');assert.equal(r.reads,1);
    r=await reader.tick(1000);assert.equal(r.reads,1);
    await reader.card(false);await reader.tick(500);await reader.card(true);
    r=await reader.tick();assert.equal(r.reads,2);assert.equal(r.status.cardId,'PRUEBA_01');
  });
  await check('reader reconnect rereads card still present',async()=>{
    await reader.reset();await reader.card(true,cardBlock('KEEP'));
    await reader.tick();await reader.config('version',0);await reader.tick(2001);
    await reader.config('version',146);const r=await reader.tick(2001);
    expectStage(r,'LECTURA_OK');assert.equal(r.reads,2);
  });
  await check('reader recovers selection without removing card',async()=>{
    await reader.reset();await reader.card(true,cardBlock('RETRY'));
    await reader.config('selectFail',true);expectStage(await reader.tick(),'ERROR_SELECCION');
    await reader.config('selectFail',false);
    const r=await reader.tick();expectStage(r,'LECTURA_OK');assert.equal(r.status.cardId,'RETRY');
  });
  for(const [fault,stage]of [['unsupported','TIPO_NO_COMPATIBLE'],['authFail','ERROR_AUTENTICACION'],['readFail','ERROR_LECTURA']])
    await check('reader '+fault,async()=>{
      await reader.reset();await reader.card(true);await reader.config(fault,true);
      const r=await reader.tick();expectStage(r,stage);assert.equal(r.attempts,0);
    });
  await check('blank and binary cards are visible without false ID',async()=>{
    await reader.reset();await reader.card(true,'00'.repeat(16));
    let r=await reader.tick();expectStage(r,'LECTURA_OK');assert.equal(r.status.cardId,'');
    await reader.card(false);await reader.tick(500);await reader.card(true,'01'.repeat(16));
    r=await reader.tick();assert.equal(r.status.cardId,'');assert(r.status.blockHex.length>0);
  });
  await check('unified writer rejects unconfirmed writes',async()=>{
    await reader.reset();await reader.card(true);
    const r=await reader.post('/api/write','HACK','yes');
    assert.equal(r.code,400);assert.equal(r.attempts,0);
  });
  for(const firmware of [reader,writer])await check('bounded escaped JSON log '+(firmware===reader?'reader':'writer'),async()=>{
    await firmware.reset();const r=await firmware.command('LOG 5000');
    assert(r.status.trace.length<=3500);assert(r.status.trace.includes('quotes " and newline\n'));
    assert.equal((await firmware.get()).cache,'no-store');
    assert.equal((await firmware.get('/')).cache,'no-store');
  });
}


async function scan(firmware,id){
 await firmware.card(false);await firmware.tick(501);
 await firmware.card(true,cardBlock(CARDS[id]||id));
 return firmware.tick();
}
async function openClass(reader,teacher='PROF_01'){
 await reader.reset();
 assert.equal((await reader.post('/api/time','1800000000')).code,200);
 const r=await scan(reader,teacher);assert(r.status.attendance.active);return r;
}
async function newFeatureTests(writer,reader){
 await check('throttled radio errors cannot reuse a successful result',async()=>{
  await reader.reset();await reader.card(true,cardBlock('TEST'));
  await reader.config('detectFail',true);expectStage(await reader.tick(),'ERROR_DETECCION');
  await reader.config('detectFail',false);expectStage(await reader.tick(),'LECTURA_OK');
  await reader.config('detectFail',true);const r=await reader.tick();
  expectStage(r,'ERROR_DETECCION');assert.equal((await reader.history()).attempts[0].stage,'ERROR_DETECCION');
 });
 await check('fixed roster is identical and immutable',async()=>{
  await writer.reset();await reader.reset();
  const a=await writer.people(),b=await reader.people();
  assert.deepEqual(a,b);assert.equal(a.length,8);assert.equal(a.filter(x=>x.teacher).length,2);
  assert.equal(new Set(a.map(x=>x.cardId)).size,8);
  for(const f of [writer,reader])for(const route of ['/api/admin/student','/api/admin/teacher','/api/admin/class'])
   assert.equal((await f.post(route,'NEW')).code,404);
 });
 for(const f of [writer,reader])await check('read-only diagnostic '+(f===writer?'writer':'reader'),async()=>{
  await f.reset();const before=(await f.get()).status.attendance;
  assert.equal((await f.post('/api/diagnose')).code,202);
  await f.card(true,cardBlock(CARDS.PROF_01));const r=await f.tick();
  expectStage(r,'LECTURA_OK');assert.equal(r.attempts,0);assert(!r.status.diagnosticPending);
  if(before)assert.equal(r.status.attendance.active,false);
  await f.tick();if(before)assert.equal((await f.get()).status.attendance.active,false);
  assert.equal((await f.history()).attempts[0].kind,'DIAGNOSTICO');
 });
 for(const f of [writer,reader])await check('20 attempts and copy report '+(f===writer?'writer':'reader'),async()=>{
  await f.reset();const r=await f.command('ATTEMPTS 35');
  const h=await f.history();assert.equal(h.attempts.length,20);assert.equal(h.attempts[0].number,35);assert.equal(h.attempts[19].number,16);
  const report=(await f.get('/api/diagnostic/report')).response;
  assert(report.includes('Intento 35'));assert(!report.includes('Intento 15 /'));
  assert.equal((await f.post('/api/reader/reset')).code,200);
  assert.equal((await f.history()).attempts.length,20);
 });
 await check('reset and diagnostic rejected during pending write',async()=>{
  await writer.reset();await writer.arm();
  assert.equal((await writer.post('/api/reader/reset')).code,409);
  assert.equal((await writer.post('/api/diagnose')).code,409);
  assert((await writer.get()).status.pending);
 });
 for(const f of [writer,reader])await check('diagnostic timeout and cancel without writing '+(f===writer?'writer':'reader'),async()=>{
  await f.reset();await f.post('/api/diagnose');let r=await f.tick(30000);
  expectStage(r,'TIEMPO_AGOTADO');assert(!r.status.diagnosticPending);assert.equal(r.attempts,0);
  await f.post('/api/diagnose');expectStage(await f.post('/api/cancel'),'CANCELADO');
 });
 await check('clock prerequisite and validation',async()=>{
  await reader.reset();let r=await scan(reader,'PROF_01');assert(!r.status.attendance.active);
  for(const invalid of ['abc','0','1800000000bad'])assert.equal((await reader.post('/api/time',invalid)).code,400);
  assert.equal((await reader.post('/api/time','1800000000')).code,200);
 });
 await check('cancelling diagnosis cannot count the card left on the reader',async()=>{
  await openClass(reader);await reader.post('/api/diagnose');await reader.card(true,cardBlock(CARDS.ALU_01));
  await reader.post('/api/cancel');let r=await reader.tick();assert.equal(r.status.attendance.session.presentStudents.length,0);
  r=await scan(reader,'ALU_01');assert.equal(r.status.attendance.session.presentStudents.length,1);
 });
 for(const teacher of ['PROF_01','PROF_02'])await check('class membership and duplicates '+teacher,async()=>{
  await openClass(reader,teacher);
  const own=teacher==='PROF_01'?['ALU_01','ALU_02','ALU_03']:['ALU_04','ALU_05','ALU_06'];
  const other=teacher==='PROF_01'?'ALU_04':'ALU_01';
  let r=await scan(reader,own[0]);assert.equal(r.status.attendance.session.presentStudents.length,1);
  r=await scan(reader,own[0]);assert.equal(r.status.attendance.session.presentStudents.length,1);assert(r.status.attendance.message.includes('duplic'));
  r=await scan(reader,other);assert(r.status.attendance.message.includes('otra clase'));
  r=await scan(reader,teacher==='PROF_01'?'PROF_02':'PROF_01');assert.equal(r.status.attendance.session.teacherId,teacher);
  r=await scan(reader,'UNKNOWN');assert(r.status.attendance.message.includes('no registrada'));
  await scan(reader,own[1]);r=await scan(reader,own[2]);assert.equal(r.status.attendance.session.presentStudents.length,3);
  assert.equal((await reader.post('/api/time','1800000010')).code,409);
  assert.equal((await reader.post('/api/reader/reset')).code,200);
  assert((await reader.get()).status.attendance.active);assert.equal((await reader.get()).status.attendance.session.presentStudents.length,3);
  r=await reader.tick(600000);assert(!r.status.attendance.active);assert.equal(r.status.attendance.session.closeReason,'TIMEOUT');
  assert.equal(r.status.attendance.session.presentStudents.length,3);
  const exported=(await reader.get('/api/attendance/export')).response.trim().split('\n').map(JSON.parse);
  assert.equal(exported[0].presentStudents.length,3);
  r=await reader.command('REBOOT');assert.equal(r.status.attendance.session.presentStudents.length,3);
  assert.equal(r.status.attendance.session.closeReason,'TIMEOUT');assert(!r.status.attendance.clockReady);assert.equal(r.formats,0);
 });
 await check('reboot preserves scans and closes an interrupted session',async()=>{
  await openClass(reader);await scan(reader,'ALU_01');
  const r=await reader.command('REBOOT');assert(!r.status.attendance.active);
  assert.equal(r.status.attendance.session.presentStudents.length,1);
  assert.equal(r.status.attendance.session.closeReason,'DEVICE_RESTART');assert.equal(r.formats,0);
 });
 for(const failure of ['fsWrite','fsRename'])await check('failed persistence rolls back scan '+failure,async()=>{
  await openClass(reader);await scan(reader,'ALU_01');await reader.config(failure,true);
  let r=await scan(reader,'ALU_02');assert.equal(r.status.attendance.session.presentStudents.length,1);
  assert(r.status.attendance.message.includes('No se pudo guardar'));
  await reader.config(failure,false);r=await reader.command('REBOOT');assert.equal(r.status.attendance.session.presentStudents.length,1);
 });
 await check('failed final save closes intake and retries',async()=>{
  await openClass(reader);await scan(reader,'ALU_01');await reader.config('fsRename',true);
  let r=await reader.tick(600000);assert(r.status.attendance.closePending);assert(!r.status.attendance.active);
  r=await scan(reader,'ALU_02');assert.equal(r.status.attendance.session.presentStudents.length,1);
  await reader.config('fsRename',false);r=await reader.tick(5000);
  assert(!r.status.attendance.closePending);assert.equal(r.status.attendance.session.closeReason,'TIMEOUT');
 });
 await check('cannot open a class when its first save fails',async()=>{
  await reader.reset();await reader.post('/api/time','1800000000');await reader.config('fsWrite',true);
  const r=await scan(reader,'PROF_01');assert(!r.status.attendance.active);assert.equal(r.status.attendance.session,null);
 });
 await check('multiple class sessions retained in export',async()=>{
  await openClass(reader);await scan(reader,'ALU_01');await reader.tick(600000);
  await scan(reader,'PROF_02');await scan(reader,'ALU_04');
  let r=await reader.get('/api/attendance/export');let rows=r.response.trim().split('\n').map(JSON.parse);
  assert.equal(rows.length,2);assert(rows.every(x=>x.presentStudents.length===1));
  await reader.command('REBOOT');r=await reader.get('/api/attendance/export');rows=r.response.trim().split('\n').map(JSON.parse);
  assert.equal(rows.length,2);assert(rows.some(x=>x.closeReason==='DEVICE_RESTART'));
 });
 await check('no automatic format, explicit first initialization only',async()=>{
  await reader.reset();await reader.config('fsMount',false);let r=await reader.command('REBOOT');
  assert(!r.status.attendance.storageReady);assert.equal(r.formats,0);
  assert.equal((await reader.post('/api/storage/init','no')).code,400);
  r=await reader.post('/api/storage/init','INICIALIZAR');assert.equal(r.code,200);assert.equal(r.formats,1);
  assert.equal((await reader.post('/api/storage/init','INICIALIZAR')).code,409);
 });
 await check('corrupt files block writes without formatting',async()=>{
  await openClass(reader);await scan(reader,'ALU_01');await reader.command('CORRUPT');
  const r=await reader.command('REBOOT');assert(!r.status.attendance.storageReady);assert.equal(r.formats,0);
 });
}


async function unifiedTests(reader){
 await check('single firmware serves attendance and both admin paths',async()=>{
  await reader.reset();
  for(const route of ['/','/admin','/admin/'])assert.equal((await reader.get(route)).code,200);
  assert.equal((await reader.get()).status.mode,'rfid');
 });
 await check('one initialization and settling time on startup, reset and recovery',async()=>{
  let r=await reader.reset();assert.equal(r.initializations,1);assert.equal(r.earlyProbes,0);
  r=await reader.post('/api/reader/reset');assert.equal(r.initializations,2);assert.equal(r.earlyProbes,0);
  await reader.config('version',0);await reader.tick(2001);
  await reader.config('version',146);r=await reader.tick(2001);
  assert(r.status.connected);assert.equal(r.earlyProbes,0);
 });
 await check('writing during a class preserves attendance and suppresses held card',async()=>{
  await openClass(reader);await scan(reader,'ALU_01');
  await reader.card(false);await reader.tick(501);await reader.arm('ALU_02');
  await reader.card(true,cardBlock('OLD'));let r=await reader.tick();expectStage(r,'ESCRITURA_OK');
  assert.equal(r.status.attendance.session.presentStudents.length,1);
  r=await reader.tick(1000);assert.equal(r.status.attendance.session.presentStudents.length,1);
  r=await scan(reader,'ALU_02');assert.equal(r.status.attendance.session.presentStudents.length,2);
 });
 await check('pending administration cannot postpone the attendance deadline',async()=>{
  await openClass(reader);await reader.card(false);await reader.tick(599000);
  await reader.arm('ALU_01');const r=await reader.tick(1001);
  assert(r.status.pending);assert(!r.status.attendance.active);
  assert.equal(r.status.attendance.session.closeReason,'TIMEOUT');
 });
 await check('cancelled writing cannot turn an administrative card into attendance',async()=>{
  await openClass(reader);await reader.arm('ALU_01');
  await reader.card(true,cardBlock(CARDS.ALU_01));await reader.post('/api/cancel');
  const r=await reader.tick();assert.equal(r.writes,0);
  assert.equal(r.status.attendance.session.presentStudents.length,0);
 });
 await check('two admin clients cannot replace the pending identity',async()=>{
  await reader.reset();const results=await Promise.all([reader.arm('ALU_01'),reader.arm('ALU_02')]);
  assert.deepEqual(results.map(r=>r.code),[202,409]);
  assert.equal((await reader.get()).status.requestedId,CARDS.ALU_01);
 });
 for(const size of [0,3,11,255])for(const writing of [false,true])
  await check('reject invalid UID size '+size+' writing='+writing,async()=>{
   await reader.reset();await reader.config('uidSize',size);await reader.card(true);
   if(writing)await reader.arm();
   const r=await reader.tick();expectStage(r,'ERROR_SELECCION');assert.equal(r.reads,0);assert.equal(r.writes,0);
  });
 for(const writing of [false,true])await check('reject oversized block length writing='+writing,async()=>{
  await reader.reset();await reader.config('oversizedRead',true);await reader.card(true);
  if(writing)await reader.arm();
  const r=await reader.tick();expectStage(r,'ERROR_LECTURA');assert.equal(r.writes,0);
 });
}

async function optimizationTests(writer,reader){
 for(const f of [writer,reader]){
  const name=f===writer?'writer':'reader';
  await check('lightweight status and separated immutable people '+name,async()=>{
   await f.reset();let r=await f.get();
   assert(!Object.hasOwn(r.status,'people'));assert(!Object.hasOwn(r.status,'attempts'));
   assert.equal(typeof r.status.bootId,'string');assert(r.status.bootId.length);
   assert.equal(typeof r.status.stateRevision,'number');assert.equal(typeof r.status.historyRevision,'number');
   assert.equal(r.status.operation,'IDLE');
   const p=await f.get('/api/people');assert.equal(p.code,200);assert.equal(JSON.parse(p.response).people.length,8);
   assert(p.historyUnchanged);
  });
  await check('GET cannot copy/mutate an unfinished attempt '+name,async()=>{
   await f.reset();await f.post('/api/diagnose');
   const before=await f.history(),revision=(await f.get()).status.historyRevision;
   assert.equal(before.attempts[0].stage,'EN_CURSO');
   for(let n=0;n<10;++n)for(const route of ['/api/status','/api/people','/api/attempts','/api/diagnostic/report']){
    const r=await f.get(route);assert(r.historyUnchanged,'GET mutated snapshots: '+route);
    assert.equal(r.status.historyRevision,revision);
   }
   assert.deepEqual(await f.history(),before);
  });
  await check('two clients have independent full snapshot recovery '+name,async()=>{
   await f.reset();await f.command('ATTEMPTS 4');
   const [a,b]=await Promise.all([f.history(),f.history()]);assert.deepEqual(a,b);
   await f.command('ATTEMPTS 25');
   const [newA,newB]=await Promise.all([f.history(),f.history()]);
   assert.deepEqual(newA,newB);assert.equal(newA.attempts.length,20);
   assert.equal(newA.oldestSequence,10);assert.equal(newA.newestSequence,29);
   assert(newA.historyRevision>a.historyRevision);
   assert.equal(new Set(newA.attempts.map(x=>x.number)).size,20);
  });
  await check('reboot invalidates revisions and old client history '+name,async()=>{
   await f.reset();await f.command('ATTEMPTS 30');const old=await f.history();
   const r=await f.command('REBOOT'),fresh=await f.history();
   assert.notEqual(r.status.bootId,old.bootId);assert.equal(fresh.bootId,r.status.bootId);
   assert.equal(fresh.attempts.length,0);assert.equal(fresh.oldestSequence,0);assert.equal(fresh.newestSequence,0);
  });
  await check('thousands of attempts keep bounded history and frequent body '+name,async()=>{
   await f.reset();await f.command('ATTEMPTS 20');const before=await f.get();
   await f.command('ATTEMPTS 5000');const after=await f.get(),h=await f.history();
   assert.equal(h.attempts.length,20);assert.equal(h.newestSequence,5020);
   assert.equal(h.oldestSequence,5001);assert.equal(new Set(h.attempts.map(x=>x.number)).size,20);
   assert(Buffer.byteLength(after.response)<8000);
   assert(Math.abs(Buffer.byteLength(after.response)-Buffer.byteLength(before.response))<64);
   for(const a of h.attempts)assert(Buffer.byteLength(a.trace)<=1600);
   assert(Buffer.byteLength(JSON.stringify(h))<60000);
  });
 }
 await check('operation state transitions keep countdown outside revision',async()=>{
  await writer.reset();let r=await writer.arm();assert.equal(r.status.operation,'WAIT_WRITE');
  const revision=r.status.stateRevision;r=await writer.tick(1000);
  assert.equal(r.status.operation,'WAIT_WRITE');assert.equal(r.status.stateRevision,revision);
  assert.equal(r.status.remainingSeconds,59);
  r=await writer.post('/api/cancel');assert.equal(r.status.operation,'IDLE');
  r=await writer.post('/api/diagnose');assert.equal(r.status.operation,'WAIT_DIAGNOSTIC');
 });
 await check('streaming export evaluates class deadline without HTTP reentry',async()=>{
  await openClass(reader);await scan(reader,'ALU_01');await reader.tick(600000);
  await scan(reader,'PROF_02');await scan(reader,'ALU_04');
  await reader.config('streamAdvance',200001);
  const r=await reader.get('/api/attendance/export');
  assert.equal(r.code,200);assert(r.streamChunks>=3);
  assert.equal(r.httpReentered,false);
  assert.equal(r.status.attendance.active,false);assert.equal(r.status.attendance.session.closeReason,'TIMEOUT');
  assert.equal(r.status.attendance.session.presentStudents.length,1);
  await reader.config('streamAdvance',0);
 });
 await check('legacy 84-byte record is read without migration',async()=>{
  await reader.reset();
  const fixture=fs.readFileSync(path.join(__dirname,'fixtures','attendance-v1.hex'),'utf8').trim();
  assert.equal(fixture.length,168);await reader.command('LOADRECORD '+fixture);
  const r=await reader.command('REBOOT'),session=r.status.attendance.session;
  assert.equal(r.status.attendance.storageReady,true);
  assert.equal(session.teacherId,'PROF_01');assert.equal(session.class,'Ingeniería');
  assert.equal(session.closeReason,'TIMEOUT');assert.equal(session.openedAt,'2027-01-15T05:00:00-03:00');
  assert.equal(session.closedAt,'2027-01-15T05:10:00-03:00');
  assert.deepEqual(session.presentStudents,[{id:'ALU_01',name:'Lucía Fernández',scannedAt:'2027-01-15T05:00:02-03:00'}]);
  assert.equal(r.formats,0);
 });
 await check('25-record export cannot defer timeout or recursively serve HTTP',async()=>{
  await reader.reset();await reader.post('/api/time','1800000000');
  for(let i=0;i<24;++i){await scan(reader,i%2?'PROF_02':'PROF_01');await scan(reader,i%2?'ALU_04':'ALU_01');await reader.tick(600000);}
  await scan(reader,'PROF_01');await scan(reader,'ALU_01');
  await reader.config('streamAdvance',25000);
  const r=await reader.get('/api/attendance/export');
  assert.equal(r.response.trim().split('\n').length,25);
  assert(r.streamChunks>=26);assert.equal(r.httpReentered,false);
  assert.equal(r.status.attendance.active,false);assert.equal(r.status.attendance.session.closeReason,'TIMEOUT');
  await reader.config('streamAdvance',0);
 });
 await check('cooperative stream handles partial writes and bounded batches',async()=>{
  await reader.reset();await reader.command('ATTEMPTS 20');
  const expected=await reader.history();await reader.config('networkPartial',3);
  const r=await reader.get('/api/attempts');assert.equal(r.transportComplete,true);assert.equal(r.transportClosed,false);
  assert.deepEqual(JSON.parse(r.response),expected);assert(r.maxWriteBatch<=512);assert(r.socketCalls>20);
  await reader.config('networkPartial',0);
 });
 await check('EAGAIN serves attendance deadline and aborts stalled receiver',async()=>{
  await openClass(reader);await scan(reader,'ALU_01');await reader.tick(599200);
  await reader.config('networkStall',true);const before=(await reader.command('STATUS')).nowMs;
  const r=await reader.get('/api/attendance/export');
  assert(r.transportClosed);assert(!r.transportComplete);assert.equal(r.httpReentered,false);
  assert.equal(r.status.attendance.active,false);assert.equal(r.status.attendance.session.closeReason,'TIMEOUT');
  assert(r.nowMs-before<=400,'Stalled send was not bounded by the no-progress budget');
  await reader.config('networkStall',false);
 });
 await check('slow progressing receiver is bounded by total-write budget',async()=>{
  await openClass(reader);await scan(reader,'ALU_01');
  await reader.config('networkPartial',1);await reader.config('networkAdvance',100);
  const before=(await reader.command('STATUS')).nowMs;
  const r=await reader.get('/api/attendance/export');
  assert(r.transportClosed);assert(!r.transportComplete);assert.equal(r.httpReentered,false);
  assert(r.nowMs-before<=2700,'Progressing send exceeded its bounded frame/body budget');
  await reader.config('networkPartial',0);await reader.config('networkAdvance',0);
 });
 for(const fault of ['networkFail','networkZero'])await check('cooperative stream closes on '+fault,async()=>{
  await reader.reset();await reader.command('ATTEMPTS 1');await reader.config(fault,true);
  const r=await reader.get('/api/attempts');assert(r.transportClosed);assert(!r.transportComplete);assert.equal(r.httpReentered,false);
  await reader.config(fault,false);
 });
 await check('cooperative stream retries a transient EINTR',async()=>{
  await reader.reset();await reader.command('ATTEMPTS 1');await reader.config('networkInterrupts',1);
  const r=await reader.get('/api/attempts');assert(r.transportComplete);assert(!r.transportClosed);
  assert.equal(JSON.parse(r.response).attempts.length,1);assert(r.socketCalls>1);
 });
}

async function serve(firmware) {
  const stats={counts:{},statusActive:0,maxStatusActive:0,statusDelay:0};
  const server=http.createServer(async(req,res)=>{
    stats.counts[req.url]=(stats.counts[req.url]||0)+1;
    const isStatus=req.url==='/api/status';
    if(isStatus){++stats.statusActive;stats.maxStatusActive=Math.max(stats.maxStatusActive,stats.statusActive);}
    let finished=false;
    const done=()=>{if(!finished&&isStatus)--stats.statusActive;finished=true;};
    res.once('finish',done);res.once('close',done);
    try {
      if(isStatus&&stats.statusDelay)await new Promise(resolve=>setTimeout(resolve,stats.statusDelay));
      let body='';for await(const chunk of req) body+=chunk;
      const fields=new URLSearchParams(body);
      const result=req.method==='POST'?
        await firmware.post(req.url,fields.get('personId')||fields.get('epoch')||fields.get('confirmation')||'',fields.get('confirm')||''):
        await firmware.get(req.url);
      res.writeHead(result.code,{'Content-Type':['/','/admin','/admin/'].includes(req.url)?'text/html; charset=utf-8':'application/json; charset=utf-8','Cache-Control':result.cache});
      res.end(result.response);
    } catch(error) {res.writeHead(500);res.end(String(error));}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  return {url:'http://127.0.0.1:'+server.address().port,stats,close:()=>new Promise(resolve=>server.close(resolve))};
}


async function browserTests(writer,reader) {
 const {chromium}=require('playwright');
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||(fs.existsSync('/usr/bin/chromium')?'/usr/bin/chromium':undefined),headless:true,args:['--no-sandbox']});
 const w=await serve(writer),r=await serve(reader);
 const page=await browser.newPage({viewport:{width:390,height:844}});
 await page.addInitScript(()=>Object.defineProperty(navigator,'clipboard',{configurable:true,value:{writeText:async()=>{throw Error('HTTP clipboard unavailable');}}}));
 const errors=[];page.on('pageerror',e=>errors.push(String(e)));
 const stage=expected=>page.waitForFunction(text=>document.getElementById('message').textContent.startsWith(text+':'),expected);
 const ready=()=>page.waitForFunction(()=>!document.getElementById('prepare').disabled);
 try{
  await writer.reset();await page.goto(w.url+'/admin');await ready();
  assert(await page.locator('#writer').isVisible());assert(await page.locator('#attendance').isHidden());
  assert.equal(await page.locator('#person option').count(),8);
  assert.equal(await page.locator('input[name=cardId]').count(),0);
  await page.locator('#person').selectOption('ALU_01');await page.getByRole('checkbox').check();
  await page.getByRole('button',{name:'Preparar escritura'}).click();await stage('ESPERANDO_TARJETA');
  assert(await page.locator('#resetReader').isDisabled());
  await writer.card(true,cardBlock('OLD_LONG_CARD_ID'));await writer.tick();await stage('ESCRITURA_OK');
  assert.equal(await page.locator('#cardId').textContent(),CARDS.ALU_01);
  await ready();await page.locator('#person').selectOption('ALU_02');
  await page.getByRole('checkbox').check();
  await page.getByRole('button',{name:'Preparar escritura'}).click();await stage('ESPERANDO_TARJETA');
  await writer.tick();await stage('ESCRITURA_OK');
  const saved=await writer.get();assert.equal(saved.status.cardId,CARDS.ALU_02);
  await writer.card(false);await ready();
  await page.locator('#diagnose').click();await stage('ESPERANDO_TARJETA');
  await writer.card(true);await writer.tick();await stage('LECTURA_OK');assert.equal((await writer.get()).writes,2);
  await page.locator('#copy').click();
  await page.waitForFunction(()=>document.getElementById('report').value.includes('DIAGNOSTICO'));
  assert(await page.locator('#report').evaluate(el=>el.selectionEnd===el.value.length));
  assert.equal(await page.locator('#report').evaluate(el=>getComputedStyle(el,'::selection').backgroundColor),'rgb(255, 255, 255)');
  assert.equal(await page.locator('#report').evaluate(el=>getComputedStyle(el,'::selection').color),'rgb(0, 0, 0)');
  await page.evaluate(()=>Object.defineProperty(navigator,'clipboard',{configurable:true,value:{writeText:async text=>{window.copied=text;}}}));
  await page.locator('#copy').click();await page.waitForFunction(()=>window.copied&&window.copied.includes('RFID'));
  const css=await page.locator('style').textContent();
  assert.deepEqual([...new Set(css.match(/#[A-Fa-f0-9]{6}/g))].sort(),['#000000','#FFFFFF']);
  assert.equal(await page.locator('body').evaluate(el=>getComputedStyle(el).backgroundColor),'rgb(0, 0, 0)');
  assert.equal(await page.locator('button:disabled').first().evaluate(el=>getComputedStyle(el).opacity),'1');
  await page.screenshot({path:path.join(output,'escritor-mobile.png'),fullPage:true});
  const boot=(await writer.get()).status.bootId;
  await page.getByRole('link',{name:'Asistencia',exact:true}).click();
  await page.waitForFunction(()=>!document.getElementById('attendance').hidden);
  assert(await page.locator('#writer').isHidden());assert.equal((await writer.get()).status.bootId,boot);
  await page.getByRole('link',{name:'Administrar tarjetas',exact:true}).click();await ready();
  await writer.card(false);await ready();await page.getByRole('checkbox').check();await page.locator('#prepare').click();await stage('ESPERANDO_TARJETA');
  await page.locator('#cancel').click();await stage('CANCELADO');
  await page.route('**/api/status',route=>route.abort());
  await page.waitForFunction(()=>document.getElementById('esp').textContent.includes('Sin conexión'));
  assert(await page.locator('#prepare').isDisabled());await page.unroute('**/api/status');await ready();
  await openClass(reader);await scan(reader,'ALU_01');await page.goto(r.url);
  await page.waitForFunction(()=>document.getElementById('present').children.length===1);
  assert((await page.locator('#lesson').textContent()).includes('Ingeniería'));
  await page.locator('#resetReader').click();
  await page.waitForFunction(()=>document.getElementById('requestError').textContent.includes('reiniciado'));
  assert((await reader.get()).status.attendance.active);
  await page.locator('#diagnose').click();await stage('ESPERANDO_TARJETA');
  await reader.card(false);await reader.tick(501);await reader.card(true,cardBlock(CARDS.ALU_02));await reader.tick();await stage('LECTURA_OK');
  assert.equal((await reader.get()).status.attendance.session.presentStudents.length,1);
  await page.locator('#copy').click();await page.waitForFunction(()=>document.getElementById('report').value.includes('Intento'));
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
  await page.screenshot({path:path.join(output,'lector-mobile.png'),fullPage:true});
  assert.deepEqual(errors,[]);
  console.log('Chromium: fixed selector, monochrome UI, diagnosis, report fallback, attendance and recovery passed');
 }finally{await page.close();await browser.close();await w.close();await r.close();}
}


async function browserOptimizationTests(writer) {
 const {chromium}=require('playwright');
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||(fs.existsSync('/usr/bin/chromium')?'/usr/bin/chromium':undefined),headless:true,args:['--no-sandbox']});
 const service=await serve(writer),page=await browser.newPage({viewport:{width:390,height:844}});
 const errors=[];page.on('pageerror',e=>errors.push(String(e)));
 const pending=new Set();let peak=0;
 page.on('request',req=>{if(new URL(req.url()).pathname==='/api/status'){pending.add(req);peak=Math.max(peak,pending.size);}});
 const done=req=>pending.delete(req);page.on('requestfinished',done);page.on('requestfailed',done);
 const online=()=>page.evaluate(()=>window.dispatchEvent(new Event('online')));
 const ready=()=>page.waitForFunction(()=>!document.getElementById('prepare').disabled);
 try{
  await writer.reset();await page.goto(service.url+'/admin');await ready();
  assert.equal(service.stats.counts['/api/people'],1);
  const historyRequests=service.stats.counts['/api/attempts'];
  await page.evaluate(()=>{
   window.unnecessaryDomChanges=0;
   const observer=new MutationObserver(records=>{window.unnecessaryDomChanges+=records.length;});
   for(const id of ['message','trace','history'])observer.observe(document.getElementById(id),{childList:true,subtree:true,characterData:true});
  });
  await page.waitForTimeout(2300);
  assert.equal(service.stats.counts['/api/people'],1);
  assert.equal(service.stats.counts['/api/attempts'],historyRequests);
  assert.equal(await page.evaluate(()=>window.unnecessaryDomChanges),0);
  service.stats.statusDelay=700;
  await page.evaluate(()=>{for(let i=0;i<12;++i)window.dispatchEvent(new Event('online'));});
  await page.waitForTimeout(1800);
  assert.equal(peak,1,'A page overlapped its status requests');
  service.stats.statusDelay=0;
  await page.route('**/api/status',route=>route.abort());await online();
  await page.waitForFunction(()=>document.getElementById('esp').textContent.includes('Sin conexión'));
  await writer.command('ATTEMPTS 40');
  const snapshot=await writer.history();
  await page.unroute('**/api/status');await online();await ready();
  await page.waitForFunction(()=>document.querySelectorAll('#history details').length===20);
  const numbers=await page.locator('#history details').evaluateAll(nodes=>nodes.map(n=>Number(n.dataset.number)));
  assert.deepEqual(numbers,snapshot.attempts.map(x=>x.number));
  assert.equal(new Set(numbers).size,20);
  await page.route('**/api/status',route=>route.abort());await online();
  await page.waitForFunction(()=>document.getElementById('esp').textContent.includes('Sin conexión'));
  await writer.command('REBOOT');
  await page.unroute('**/api/status');await online();await ready();
  await page.waitForFunction(()=>document.querySelectorAll('#history details').length===0);
  assert.equal(await page.locator('#confirm').isChecked(),false);
  assert.equal(service.stats.counts['/api/people'],1,'Static roster was refetched on reconnect');
  await page.evaluate(()=>{Object.defineProperty(document,'hidden',{configurable:true,value:true});document.dispatchEvent(new Event('visibilitychange'));});
  await page.waitForTimeout(100);
  const beforeHidden=service.stats.counts['/api/status'];
  await page.waitForTimeout(2200);assert.equal(service.stats.counts['/api/status'],beforeHidden);
  await page.evaluate(()=>{Object.defineProperty(document,'hidden',{configurable:true,value:false});document.dispatchEvent(new Event('visibilitychange'));});
  await page.waitForFunction(()=>document.getElementById('esp').textContent.includes('[OK]'));
  await page.waitForTimeout(150);
  assert(service.stats.counts['/api/status']>beforeHidden,'Visible tab did not synchronize immediately');
  const second=await browser.newPage();
  try{
   await second.goto(service.url+'/admin');await second.waitForFunction(()=>!document.getElementById('prepare').disabled);
   await writer.command('ATTEMPTS 1');
   await Promise.all([online(),second.evaluate(()=>window.dispatchEvent(new Event('online')))]);
   for(const client of [page,second])await client.waitForFunction(()=>document.querySelectorAll('#history details').length===1);
   assert.equal(await page.locator('#history details').getAttribute('data-number'),await second.locator('#history details').getAttribute('data-number'));
  }finally{await second.close();}
  assert.deepEqual(errors,[]);
  console.log('Chromium optimization: roster once, history revisions, stable DOM, single-flight, reconnect, reboot, visibility and two clients passed');
 }finally{await page.close();await browser.close();await service.close();}
}

(async()=>{
 let writer,reader;
 try {
  writer=new Firmware('escritor');reader=new Firmware('lector');
  await nativeTests(writer,reader);
  await newFeatureTests(writer,reader);
  await unifiedTests(reader);
  await optimizationTests(writer,reader);
  if(!process.argv.includes('--native-only')&&!failures.length)
    await check('real browser end-to-end',()=>browserTests(writer,reader));
  if(!process.argv.includes('--native-only')&&!failures.length)
    await check('browser optimized synchronization',()=>browserOptimizationTests(writer));
  console.log(passed+' checks passed; '+failures.length+' failed. Artifacts: '+output);
 } finally {if(writer)await writer.stop();if(reader)await reader.stop();}
 if(failures.length)process.exitCode=1;
})().catch(error=>{console.error(error);process.exitCode=1;});
