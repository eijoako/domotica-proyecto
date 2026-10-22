// Runs the actual .ino handlers with a stateful RC522 test double.
// Optional Chromium tests exercise the real pages and HTTP requests.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const assert = require('node:assert/strict');
const http = require('node:http');
const readline = require('node:readline');
const {spawn, spawnSync} = require('node:child_process');
const root = path.resolve(__dirname, '..');
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
  arm(id='PRUEBA_01',confirm='yes') {return this.post('/api/write',id,confirm);}
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
    ['missing consent','OK',''],['wrong consent','OK','no']
  ]) await check('reject '+name,async()=>{
    await writer.reset();const r=await writer.arm(id,confirm);
    assert.equal(r.code,400);assert.equal(r.status.pending,false);assert.equal(r.attempts,0);
  });
  for(const id of ['X','1234567890123456','Student_01-A']) await check('write valid ID '+id,async()=>{
    await writer.reset();await writer.card(true);
    assert.equal((await writer.arm(id)).code,202);
    const r=await writer.tick();expectStage(r,'ESCRITURA_OK');
    assert.equal(r.status.cardId,id);assert.equal(r.writes,1);
    assert.equal(r.block.replaceAll(' ',''),cardBlock(id).toUpperCase());
    assert.equal((await writer.tick()).writes,1,'A request wrote twice');
  });
  await check('overwrite a held card and zero old tail',async()=>{
    await writer.reset();await writer.card(true);await writer.arm('1234567890123456');
    expectStage(await writer.tick(),'ESCRITURA_OK');
    await writer.arm('OTRO_02');const r=await writer.tick();
    expectStage(r,'ESCRITURA_OK');assert.equal(r.writes,2);
    assert.equal(r.block.replaceAll(' ',''),cardBlock('OTRO_02').toUpperCase());
    await reader.reset();await reader.card(true,r.block.replaceAll(' ',''));
    expectStage(await reader.tick(),'LECTURA_OK');
    const independent=await reader.get();
    assert.equal(independent.status.cardId,'OTRO_02');assert.equal(independent.attempts,0);
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
    await writer.reset();await writer.arm('FIRST');const r=await writer.arm('SECOND');
    assert.equal(r.code,409);assert.equal(r.status.requestedId,'FIRST');
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

async function serve(firmware) {
  const server=http.createServer(async(req,res)=>{
    try {
      let body='';for await(const chunk of req) body+=chunk;
      const fields=new URLSearchParams(body);
      const result=req.method==='POST'?
        await firmware.post(req.url,fields.get('cardId')||'',fields.get('confirm')||''):
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
  const browser=await chromium.launch({
    executablePath:process.env.CHROMIUM_PATH||(fs.existsSync('/usr/bin/chromium')?'/usr/bin/chromium':undefined),
    headless:true,args:['--no-sandbox']
  });
  const w=await serve(writer),r=await serve(reader);
  const page=await browser.newPage({viewport:{width:390,height:844}});
  const errors=[];page.on('pageerror',error=>errors.push(String(error)));
  const stage=expected=>page.waitForFunction(text=>document.getElementById('message').textContent.startsWith(text+':'),expected);
  const ready=()=>page.waitForFunction(()=>!document.getElementById('prepare').disabled);
  try {
    await writer.reset();
    await page.goto(w.url);await ready();
    assert((await page.locator('#version').textContent()).includes('escritor'));
    await page.getByLabel('Card ID').fill('PRUEBA_01');
    await page.getByRole('checkbox').check();
    await page.getByRole('button',{name:'Preparar escritura'}).click();await stage('ESPERANDO_TARJETA');
    await writer.card(true);await writer.tick();await stage('ESCRITURA_OK');
    assert.equal((await writer.get()).writes,1);
    await ready();await page.getByLabel('Card ID').fill('OTRO_02');
    await page.getByRole('button',{name:'Preparar escritura'}).click();await stage('ESPERANDO_TARJETA');
    await writer.tick();await stage('ESCRITURA_OK');
    const saved=await writer.get();assert.equal(saved.status.cardId,'OTRO_02');
    assert.equal(saved.block.replaceAll(' ',''),cardBlock('OTRO_02').toUpperCase());
    await page.screenshot({path:path.join(output,'escritor-mobile.png'),fullPage:true});
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth),'Writer has horizontal overflow');
    await writer.card(false);await ready();
    await page.getByRole('button',{name:'Preparar escritura'}).click();await stage('ESPERANDO_TARJETA');
    await page.getByRole('button',{name:'Cancelar',exact:true}).click();await stage('CANCELADO');
    await writer.card(true);assert.equal((await writer.tick()).writes,2);
    await ready();await writer.config('authFail',true);
    await page.getByRole('button',{name:'Preparar escritura'}).click();await stage('ESPERANDO_TARJETA');
    await writer.tick();await stage('ERROR_AUTENTICACION');
    await writer.reset();await ready();
    await page.getByRole('button',{name:'Preparar escritura'}).click();await stage('ESPERANDO_TARJETA');
    await writer.tick(60000);await stage('TIEMPO_AGOTADO');await ready();
    await page.route('**/api/status',route=>route.abort());
    await page.waitForFunction(()=>document.getElementById('connection').textContent.includes('Sin conexión'));
    assert(await page.locator('#prepare').isDisabled());
    await page.unroute('**/api/status');await ready();
    await reader.reset();await reader.card(true,saved.block.replaceAll(' ',''));await reader.tick();
    await page.goto(r.url);await stage('LECTURA_OK');
    assert.equal(await page.locator('#cardId').textContent(),'OTRO_02');
    assert.equal(await page.locator('#writeForm').count(),0);
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth),'Reader has horizontal overflow');
    await page.screenshot({path:path.join(output,'lector-mobile.png'),fullPage:true});
    assert.deepEqual(errors,[]);
    console.log('Chromium: write, overwrite, cancel, error, timeout, reconnect and independent reader passed');
  } finally {await page.close();await browser.close();await w.close();await r.close();}
}

(async()=>{
 let writer,reader;
 try {
  writer=new Firmware('escritor');reader=new Firmware('lector');
  await nativeTests(writer,reader);
  if(!process.argv.includes('--native-only')&&!failures.length)
    await check('real browser end-to-end',()=>browserTests(writer,reader));
  console.log(passed+' checks passed; '+failures.length+' failed. Artifacts: '+output);
 } finally {if(writer)await writer.stop();if(reader)await reader.stop();}
 if(failures.length)process.exitCode=1;
})().catch(error=>{console.error(error);process.exitCode=1;});
