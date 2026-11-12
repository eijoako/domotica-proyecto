#pragma once
const char PAGE[] PROGMEM=R"HTML(
<!doctype html><html lang="es"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>RFID terminal</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#000000;color:#FFFFFF;font:16px ui-monospace,Consolas,monospace}
::selection{background:#FFFFFF;color:#000000}
option:checked{background:#FFFFFF;color:#000000}
main{max-width:920px;margin:auto;padding:16px}h1,h2{font-size:1.2rem}section,details{border:1px solid #FFFFFF;padding:16px;margin:16px 0}
button,input,select,textarea,a{font:inherit;color:#FFFFFF;background:#000000;border:1px solid #FFFFFF;border-radius:0;box-shadow:none}
button,a{display:inline-block;min-height:44px;padding:10px;margin:6px 6px 6px 0;cursor:pointer}
button:hover,button:focus-visible,a:focus-visible{color:#000000;background:#FFFFFF}
:focus-visible{outline:2px solid #FFFFFF;outline-offset:3px}
button:disabled{border-style:dashed;cursor:not-allowed;opacity:1}
input,select,textarea{width:100%;padding:10px;min-height:44px;appearance:none}
option{background:#000000;color:#FFFFFF}input::placeholder{color:#FFFFFF;opacity:1}
input[type=checkbox]{width:24px;height:24px;min-height:24px;vertical-align:middle;display:inline-grid;place-content:center}
input[type=checkbox]:checked::after{content:"X";color:#FFFFFF}
label{display:block;margin:12px 0}pre,dd,output{white-space:pre-wrap;overflow-wrap:anywhere}pre{font-size:.85rem}dd{margin:4px 0 14px}
[hidden]{display:none!important}summary{cursor:pointer;min-height:44px}textarea{min-height:160px}
</style><main>
<h1 id="title">RFID // TERMINAL</h1><p id="version"></p>
<nav aria-label="Navegación"><a href="/">Asistencia</a><a href="/admin">Administrar tarjetas</a></nav>
<section aria-label="Estado"><p id="esp">[ESPERANDO] ESP32</p><p id="connection">[ESPERANDO] RC522</p>
<p id="detection">[ESPERANDO] Tarjeta</p><p id="message" role="status" aria-live="polite"></p><p id="remaining"></p></section>
<section id="attendance" hidden><h2>ASISTENCIA</h2>
<p id="attendanceMessage" role="status" aria-live="polite"></p><p id="lesson"></p><p id="countdown"></p>
<ul id="present"></ul></section>
<section id="writer" hidden><h2>CONFIGURAR TARJETA</h2>
<form id="writeForm"><label>Persona<select id="person" name="personId"></select></label>
<p>Card ID fijo: <output id="requestedId"></output></p>
<label><input id="confirm" type="checkbox" required> Confirmo sobrescribir solo el bloque 4 de la próxima tarjeta.</label>
<button id="prepare" disabled>Preparar escritura</button></form></section>
<section><h2>DIAGNÓSTICO SIN ESCRITURA</h2>
<button id="diagnose" disabled>Probar tarjeta sin escribir</button><button id="resetReader" disabled>Reiniciar RC522</button>
<button id="cancel" disabled>Cancelar espera</button><button id="copy">Copiar diagnóstico</button>
<p id="requestError" role="status" aria-live="polite"></p>
<label id="copyFallback" hidden>Informe seleccionable<textarea id="report" readonly></textarea></label>
<dl><dt>UID (solo técnico)</dt><dd id="uid">—</dd><dt>Tipo</dt><dd id="type">—</dd><dt>Card ID leído</dt><dd id="cardId">—</dd>
<dt>Bloque 4 leído</dt><dd id="blockHex">—</dd><dt>16 bytes solicitados</dt><dd id="expectedHex">—</dd></dl>
<pre id="trace"></pre></section><section><h2>ÚLTIMOS 20 INTENTOS // RAM</h2><div id="history"></div></section>
</main><script>
const $=id=>document.getElementById(id);
const admin=location.pathname==='/admin'||location.pathname==='/admin/';
let people=[],peopleLoaded=false,historyText='',presentText='',current=null;
let bootId=null,historyRevision=null,renderedKey='',disconnected=true;
let timer=null,syncing=false,syncAgain=false,statusController=null,actionSending=false;
const busy=s=>!!s&&(s.pending||s.diagnosticPending||s.operation==='WRITING');
function selectedPerson(){const p=people.find(p=>p.id===$('person').value);$('requestedId').textContent=p?p.cardId:'';$('confirm').checked=false;}
$('person').addEventListener('change',selectedPerson);
function showDisconnected(){
 disconnected=true;
 $('esp').textContent='[ERROR] Sin conexión con la ESP32';
 for(const id of ['prepare','diagnose','resetReader','cancel'])$(id).disabled=true;
}
async function timedFetch(path,options={}){
 const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),8000);
 try{
  const response=await fetch(path,{...options,signal:controller.signal});
  const body=await response.text();
  return {ok:response.ok,json:async()=>JSON.parse(body),text:async()=>body};
 }finally{clearTimeout(timeout);}
}
async function action(path,fields={}){
 if(actionSending)return;
 actionSending=true;$('requestError').textContent='';
 try{
  const r=await timedFetch(path,{method:'POST',cache:'no-store',body:new URLSearchParams(fields)});
  const d=await r.json();$('requestError').textContent=(r.ok?'[OK] ':'[ERROR] ')+d.message;
  if(r.ok&&path==='/api/write')$('confirm').checked=false;
 }catch(e){$('requestError').textContent='[ERROR] No se pudo confirmar la solicitud; consultar el resultado antes de reintentar.';}
 finally{actionSending=false;requestSync();}
}
$('writeForm').addEventListener('submit',e=>{e.preventDefault();action('/api/write',{personId:$('person').value,confirm:$('confirm').checked?'yes':'no'});});
$('diagnose').onclick=()=>action('/api/diagnose');
$('resetReader').onclick=()=>action('/api/reader/reset');
$('cancel').onclick=()=>action('/api/cancel');
$('copy').onclick=async()=>{
 try{
  const r=await timedFetch('/api/diagnostic/report',{cache:'no-store'});if(!r.ok)throw Error();
  const text=await r.text();$('report').value=text;$('copyFallback').hidden=false;
  try{if(!navigator.clipboard)throw Error();await navigator.clipboard.writeText(text);$('requestError').textContent='[OK] Informe copiado.';}
  catch(e){$('report').focus();$('report').select();$('requestError').textContent='[OK] Informe seleccionado; copiar manualmente.';}
 }catch(e){$('requestError').textContent='[ERROR] No se pudo obtener el informe.';}
};
function renderHistory(attempts){
 const serialized=JSON.stringify(attempts);if(serialized===historyText)return;historyText=serialized;
 const open=new Set([...$('history').querySelectorAll('details[open]')].map(d=>d.dataset.number));
 $('history').replaceChildren();
 for(const a of attempts){
  const d=document.createElement('details');d.dataset.number=String(a.number);d.open=open.has(String(a.number));
  const s=document.createElement('summary');s.textContent='#'+a.number+' '+a.kind+' // '+a.stage;
  const p=document.createElement('pre');p.textContent=a.trace+'\n'+a.message;d.append(s,p);$('history').append(d);
 }
}
function renderPeople(list){
 people=list;$('person').replaceChildren();
 for(const p of people){const o=document.createElement('option');o.value=p.id;o.textContent=p.name+(p.teacher?'docente':'estudiante')+' // '+p.class;$('person').append(o);}
 peopleLoaded=true;selectedPerson();
}
function updateCountdown(s){
 const working=busy(s);
 $('remaining').textContent=working?'[ESPERANDO] '+s.remainingSeconds+' segundos':'';
 if(s.attendance){
  const a=s.attendance,n=a.remainingSeconds,q=a.session;
  $('countdown').textContent=a.active?'[OK] ABIERTA // '+String(Math.floor(n/60)).padStart(2,'0')+':'+String(n%60).padStart(2,'0'):'[CERRADO] '+'';
 }
}
function render(s){
 document.title=admin?'Administración RFID':'Asistencia RFID';
 $('title').textContent=document.title+' // TERMINAL';$('version').textContent='Firmware '+s.version+' // '+s.mode;
 $('esp').textContent='[OK] ESP32 responde';
 $('connection').textContent=s.connected?'[OK] RC522 // versión '+s.readerVersion:'[ERROR] RC522 sin respuesta';
 $('detection').textContent=s.uid?'[OK] Tarjeta detectada // '+s.uid:'[ESPERANDO] Sin tarjeta identificada en este intento';
 $('message').textContent=s.stage+': '+(s.stage.includes('ERROR')||s.stage==='TIPO_NO_COMPATIBLE'?'[ERROR] ':s.stage==='ESPERANDO_TARJETA'?'[ESPERANDO] ':'[OK] ')+s.message;
 const working=busy(s);
 $('prepare').disabled=!s.connected||working;$('diagnose').disabled=!s.connected||working;
 $('resetReader').disabled=working;$('cancel').disabled=!working;
 $('writer').hidden=!admin;$('attendance').hidden=admin;
 for(const key of ['uid','type','cardId','blockHex','expectedHex'])$(key).textContent=s[key]||'—';
 $('trace').textContent=s.trace;
 if(s.attendance){
  const a=s.attendance,q=a.session;
  $('attendanceMessage').textContent=a.message;
  $('lesson').textContent=q?q.class+' // '+q.teacher+' // Presentes: '+q.presentStudents.length+'/'+q.capacity:'[CERRADO] Sin sesión';
  const entries=q?q.presentStudents:[];
  const serialized=JSON.stringify(entries);
  if(serialized!==presentText){
   presentText=serialized;$('present').replaceChildren();
   for(const p of entries){const li=document.createElement('li');li.textContent='[OK] '+p.name+' // ';$('present').append(li);}
  }
 }
}
function invalidateBoot(nextBoot){
 bootId=nextBoot;historyRevision=null;renderedKey='';historyText='';presentText='';current=null;
 $('history').replaceChildren();$('present').replaceChildren();
 $('trace').textContent='';$('report').value='';$('copyFallback').hidden=true;
 $('confirm').checked=false;
 $('esp').textContent='[ESPERANDO] Sincronizando el inicio de la ESP32';
 for(const id of ['prepare','diagnose','resetReader','cancel'])$(id).disabled=true;
}
async function fetchJson(path,signal){
 const r=await fetch(path,{cache:'no-store',signal});if(!r.ok)throw Error('HTTP '+r.status);return r.json();
}
function pollDelay(){
 return busy(current)||(current&&current.attendance&&(current.attendance.active))?500:2000;
}
function requestSync(){
 clearTimeout(timer);timer=null;
 if(document.hidden)return;
 if(syncing){syncAgain=true;return;}
 synchronize();
}
async function synchronize(){
 if(syncing||document.hidden)return;
 syncing=true;syncAgain=false;
 const controller=new AbortController();statusController=controller;
 const timeout=setTimeout(()=>controller.abort(),8000);
 try{
  const s=await fetchJson('/api/status',controller.signal);
  if(document.hidden)return;
  const nextBoot=String(s.bootId);
  if(nextBoot!==bootId)invalidateBoot(nextBoot);
  if(!peopleLoaded){
   const data=await fetchJson('/api/people',controller.signal);
   if(document.hidden)return;
   renderPeople(data.people);
  }
  if(historyRevision!==s.historyRevision){
   const data=await fetchJson('/api/attempts',controller.signal);
   if(document.hidden)return;
   // A reboot between requests invalidates this entire synchronization.
   // A full bounded snapshot also replaces entries missed while disconnected.
   if(String(data.bootId)!==nextBoot){invalidateBoot(String(data.bootId));syncAgain=true;return;}
   renderHistory(data.attempts);historyRevision=data.historyRevision;
  }
  const key=nextBoot+':'+s.stateRevision;
  if(disconnected||renderedKey!==key){render(s);renderedKey=key;}
  current=s;updateCountdown(s);disconnected=false;
 }catch(e){if(!document.hidden)showDisconnected();}
 finally{
  clearTimeout(timeout);statusController=null;syncing=false;
  if(!document.hidden){const delay=syncAgain?0:pollDelay();syncAgain=false;timer=setTimeout(requestSync,delay);}
 }
}
document.addEventListener('visibilitychange',()=>{
 if(document.hidden){clearTimeout(timer);timer=null;if(statusController)statusController.abort();}
 else requestSync();
});
window.addEventListener('online',requestSync);
window.addEventListener('offline',()=>{
 clearTimeout(timer);timer=null;
 if(statusController)statusController.abort();
 showDisconnected();
});
requestSync();
</script></html>
)HTML";
