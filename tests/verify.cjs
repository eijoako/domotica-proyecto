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
    if(mode === 'escritor') args.push('-DTEST_WRITER');
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
  await check('reader rejects writing endpoints',async()=>{
    await reader.reset();await reader.card(true);
    const r=await reader.post('/api/write','HACK','yes');
    assert.equal(r.code,404);assert.equal(r.attempts,0);
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
  expectStage(r,'ERROR_DETECCION');assert.equal(r.status.attempts[0].stage,'ERROR_DETECCION');
 });
 await check('fixed roster is identical and immutable',async()=>{
  await writer.reset();await reader.reset();
  const a=(await writer.get()).status.people,b=(await reader.get()).status.people;
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
  assert.equal(r.status.attempts[0].kind,'DIAGNOSTICO');
 });
 for(const f of [writer,reader])await check('20 attempts and copy report '+(f===writer?'writer':'reader'),async()=>{
  await f.reset();const r=await f.command('ATTEMPTS 35');
  assert.equal(r.status.attempts.length,20);assert.equal(r.status.attempts[0].number,35);assert.equal(r.status.attempts[19].number,16);
  const report=(await f.get('/api/diagnostic/report')).response;
  assert(report.includes('Intento 35'));assert(!report.includes('Intento 15 /'));
  assert.equal((await f.post('/api/reader/reset')).code,200);
  assert.equal((await f.get()).status.attempts.length,20);
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

async function serve(firmware) {
  const server=http.createServer(async(req,res)=>{
    try {
      let body='';for await(const chunk of req) body+=chunk;
      const fields=new URLSearchParams(body);
      const result=req.method==='POST'?
        await firmware.post(req.url,fields.get('personId')||fields.get('epoch')||fields.get('confirmation')||'',fields.get('confirm')||''):
        await firmware.get(req.url);
      res.writeHead(result.code,{'Content-Type':req.url==='/'?'text/html; charset=utf-8':'application/json; charset=utf-8','Cache-Control':result.cache});
      res.end(result.response);
    } catch(error) {res.writeHead(500);res.end(String(error));}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  return {url:'http://127.0.0.1:'+server.address().port,close:()=>new Promise(resolve=>server.close(resolve))};
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
  await writer.reset();await page.goto(w.url);await ready();
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
  await page.locator('#copy').click();await page.waitForFunction(()=>window.copied&&window.copied.includes('3.0.0'));
  const css=await page.locator('style').textContent();
  assert.deepEqual([...new Set(css.match(/#[A-Fa-f0-9]{6}/g))].sort(),['#000000','#FFFFFF']);
  assert.equal(await page.locator('body').evaluate(el=>getComputedStyle(el).backgroundColor),'rgb(0, 0, 0)');
  assert.equal(await page.locator('button:disabled').first().evaluate(el=>getComputedStyle(el).opacity),'1');
  await page.screenshot({path:path.join(output,'escritor-mobile.png'),fullPage:true});
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

(async()=>{
 let writer,reader;
 try {
  writer=new Firmware('escritor');reader=new Firmware('lector');
  await nativeTests(writer,reader);
  await newFeatureTests(writer,reader);
  if(!process.argv.includes('--native-only')&&!failures.length)
    await check('real browser end-to-end',()=>browserTests(writer,reader));
  console.log(passed+' checks passed; '+failures.length+' failed. Artifacts: '+output);
 } finally {if(writer)await writer.stop();if(reader)await reader.stop();}
 if(failures.length)process.exitCode=1;
})().catch(error=>{console.error(error);process.exitCode=1;});
