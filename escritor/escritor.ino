#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <string.h>

// ESP32 Dev Module + RC522, powered at 3.3 V.
constexpr byte PIN_SS = 5;
constexpr byte PIN_RST = 22;
constexpr byte PIN_SCK = 18;
constexpr byte PIN_MISO = 19;
constexpr byte PIN_MOSI = 23;
constexpr byte DATA_BLOCK = 4;
constexpr char WIFI_PASSWORD[] = "cambiar-esta-clave";
constexpr char FIRMWARE_VERSION[] = "2.0.0";
constexpr uint32_t POLL_MS = 100;

MFRC522 rfid(PIN_SS, PIN_RST);
WebServer server(80);
String stage = "INICIO";
String message = "Iniciando RC522";
String trace;
String cardUid, cardType, cardId, blockHex, expectedHex;
bool readerConnected = false;
byte readerVersion = 0;
uint32_t lastProbeMs = 0;
uint32_t lastPollMs = 0;
uint32_t lastDetectionErrorMs = 0;
enum class Selection { ABSENT, SELECTED, ERROR };
Selection selectCard();
bool checkReader(bool first = false);

// JSON strings are escaped without requiring an extra JSON library.
String jsonString(const String& value) {
  String out = "\"";
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
    else if (c < 0x20) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04x", c);
      out += escaped;
    } else out += static_cast<char>(c);
  }
  return out + "\"";
}

void report(const char* nextStage, const String& text) {
  stage = nextStage;
  message = text;
  const String line = String(millis()) + " ms [" + stage + "] " + text + "\n";
  trace += line;
  // Keep a bounded log; polling must not exhaust ESP32 RAM.
  if (trace.length() > 3500) {
    trace.remove(0, trace.length() - 3000);
    const int newline = trace.indexOf('\n');
    if (newline >= 0) trace.remove(0, newline + 1);
  }
  Serial.print(line);
}

String hexBytes(const byte* bytes, byte count) {
  String out;
  for (byte i = 0; i < count; ++i) {
    if (i) out += ' ';
    if (bytes[i] < 16) out += '0';
    out += String(bytes[i], HEX);
  }
  out.toUpperCase();
  return out;
}

bool validCardId(const String& value) {
  if (value.isEmpty() || value.length() > 16) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  }
  return true;
}

String decodeCardId(const byte* data) {
  String value;
  for (byte i = 0; i < 16 && data[i] != 0 && data[i] != 0xFF; ++i)
    value += static_cast<char>(data[i]);
  return validCardId(value) ? value : "";
}

void clearCard() {
  cardUid = ""; cardType = ""; cardId = ""; blockHex = ""; expectedHex = "";
}

void releaseCard() {
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

bool checkReader(bool first) {
  if (!first && millis() - lastProbeMs < 2000) return readerConnected;
  lastProbeMs = millis();
  if (!readerConnected) rfid.PCD_Init();
  const byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  const bool connected = version != 0x00 && version != 0xFF;
  if (first || connected != readerConnected) {
    readerVersion = version;
    if (connected)
      report("LECTOR_LISTO", "RC522 conectado. Version de chip: " + hexBytes(&version, 1));
    else
      report("ERROR_LECTOR", "El RC522 no responde (registro 00/FF). Revisar 3.3 V y cables SPI.");
  }
  readerVersion = version;
  readerConnected = connected;
  return connected;
}

Selection selectCard() {
  byte atqa[2];
  byte atqaSize = sizeof(atqa);
  auto status = rfid.PICC_RequestA(atqa, &atqaSize);
  if (status == MFRC522::STATUS_TIMEOUT) {
    atqaSize = sizeof(atqa);
    status = rfid.PICC_WakeupA(atqa, &atqaSize);
  }
  if (status == MFRC522::STATUS_TIMEOUT) return Selection::ABSENT;
  if (status != MFRC522::STATUS_OK) {
    if (lastDetectionErrorMs == 0 || millis() - lastDetectionErrorMs >= 2000) {
      report("ERROR_DETECCION", "Respuesta de radio invalida; presentar una sola tarjeta. " +
             String(rfid.GetStatusCodeName(status)));
      lastDetectionErrorMs = millis();
    }
    rfid.PCD_StopCrypto1();
    return Selection::ERROR;
  }
  status = rfid.PICC_Select(&rfid.uid);
  if (status != MFRC522::STATUS_OK) {
    if (lastDetectionErrorMs == 0 || millis() - lastDetectionErrorMs >= 2000) {
      report("ERROR_SELECCION", "Tarjeta detectada, pero no seleccionada: " +
             String(rfid.GetStatusCodeName(status)));
      lastDetectionErrorMs = millis();
    }
    rfid.PCD_StopCrypto1();
    return Selection::ERROR;
  }
  return Selection::SELECTED;
}

void describeCard() {
  cardUid = hexBytes(rfid.uid.uidByte, rfid.uid.size);
  cardType = String(rfid.PICC_GetTypeName(rfid.PICC_GetType(rfid.uid.sak)));
  report("TARJETA_DETECTADA", "UID tecnico: " + cardUid + " | Tipo: " + cardType);
}

bool authenticate() {
  if (rfid.PICC_GetType(rfid.uid.sak) != MFRC522::PICC_TYPE_MIFARE_1K) {
    report("TIPO_NO_COMPATIBLE", "El bloque 4 requiere MIFARE Classic 1K. Tipo detectado: " + cardType);
    return false;
  }
  report("AUTENTICANDO", "Bloque 4, Key A predeterminada FF FF FF FF FF FF.");
  MFRC522::MIFARE_Key key;
  memset(key.keyByte, 0xFF, sizeof(key.keyByte));
  const auto status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A,
                                           DATA_BLOCK, &key, &rfid.uid);
  if (status != MFRC522::STATUS_OK) {
    report("ERROR_AUTENTICACION", "No se pudo autenticar. Puede haber otra clave o un problema de radio. " +
           String(rfid.GetStatusCodeName(status)));
    return false;
  }
  return true;
}

bool readBlock(byte* data) {
  byte buffer[18];
  byte size = sizeof(buffer);
  const auto status = rfid.MIFARE_Read(DATA_BLOCK, buffer, &size);
  if (status != MFRC522::STATUS_OK || size < 16) {
    report("ERROR_LECTURA", "Fallo al leer el bloque 4: " + String(rfid.GetStatusCodeName(status)));
    return false;
  }
  memcpy(data, buffer, 16);
  blockHex = hexBytes(data, 16);
  cardId = decodeCardId(data);
  return true;
}

void reply(int code, const String& text) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", "{\"message\":" + jsonString(text) + "}");
}

void beginHardware(const char* ssid) {
  Serial.begin(115200);
  Serial.println();
  Serial.printf("%s | firmware %s\n", ssid, FIRMWARE_VERSION);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS);
  rfid.PCD_Init();
  delay(50);
  checkReader(true);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid, WIFI_PASSWORD)) Serial.println("ERROR: no se pudo crear la red Wi-Fi.");
  Serial.printf("Wi-Fi: %s | IP: %s | Monitor serie: 115200\n",
                ssid, WiFi.softAPIP().toString().c_str());
}

const char PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html lang="es">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Diagnostico RFID</title>
<style>
:root{color-scheme:dark}*{box-sizing:border-box}
body{margin:0;background:#10151e;color:#e6edf7;font:16px system-ui}
main{max-width:850px;margin:auto;padding:20px}h1{font-size:1.5rem}
section{background:#1b2636;padding:20px;border:1px solid #33445b;border-radius:14px;margin:16px 0}
label{display:block;margin:12px 0}input[type=text]{width:100%;padding:12px;font:inherit;background:#111925;color:inherit;border:1px solid #72849a;border-radius:6px}
button{padding:12px;margin:6px 8px 6px 0;border:0;border-radius:6px;font:inherit;background:#72cdf8;color:#071725;cursor:pointer}
button:disabled{opacity:.5;cursor:default}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:.85rem}
dd{margin:3px 0 14px;overflow-wrap:anywhere}dt,.muted{color:#aabbd0}.error{color:#ffb4b4}
#message{font-weight:600;min-height:3em}#writer{display:none}
</style>
<main>
<h1 id="title">Diagnóstico RFID</h1>
<p id="version" class="muted"></p>
<section>
<div id="connection">Conectando con la ESP32…</div>
<p id="message"></p><p id="remaining" class="muted"></p>
</section>
<section id="writer">
<h2>Escribir un ID</h2>
<p>Se reemplazarán los 16 bytes del bloque 4 de la próxima tarjeta detectada.</p>
<form id="writeForm">
<label>Card ID<input id="cardInput" name="cardId" type="text" required minlength="1" maxlength="16" pattern="[A-Za-z0-9_-]{1,16}" autocomplete="off" placeholder="PRUEBA_01"></label>
<p class="muted">Entre 1 y 16 letras sin tildes, números, guiones o guiones bajos.</p>
<label><input id="confirm" type="checkbox" required> Confirmo la sobrescritura del bloque 4.</label>
<button id="prepare" disabled>Preparar escritura</button>
<button id="cancel" type="button" disabled>Cancelar</button>
</form>
<p id="requestError" class="error"></p>
</section>
<section>
<h2>Última tarjeta</h2>
<dl>
<dt>UID (solo información técnica)</dt><dd id="uid">—</dd>
<dt>Tipo</dt><dd id="type">—</dd>
<dt>Card ID leído</dt><dd id="cardId">—</dd>
<dt>Bloque 4 leído (hexadecimal)</dt><dd id="blockHex">—</dd>
<dt>Contenido solicitado (escritor)</dt><dd id="expectedHex">—</dd>
</dl>
</section>
<section><h2>Diagnóstico</h2><pre id="trace">Esperando estado…</pre></section>
</main>
<script>
const $=id=>document.getElementById(id);
async function action(path,body) {
  $('requestError').textContent='';
  try {
    const response=await fetch(path,{method:'POST',cache:'no-store',body:new URLSearchParams(body)});
    const data=await response.json();
    if(!response.ok) throw new Error(data.message||'La operación fue rechazada');
  } catch(error) { $('requestError').textContent=error.message; }
}
$('writeForm').addEventListener('submit',event=>{
  event.preventDefault();
  action('/api/write',{cardId:$('cardInput').value,confirm:$('confirm').checked?'yes':'no'});
});
$('cancel').addEventListener('click',()=>action('/api/cancel',{}));
async function poll() {
  try {
    const response=await fetch('/api/status',{cache:'no-store'});
    if(!response.ok) throw new Error('No se pudo consultar el estado');
    const s=await response.json();
    document.title=s.mode==='escritor'?'Escritor RFID':'Lector RFID';
    $('title').textContent=document.title;
    $('version').textContent='Firmware '+s.version+' · '+s.mode;
    $('connection').textContent=s.connected?'RC522 conectado · versión '+s.readerVersion:'RC522 sin conexión: revisar alimentación y cables';
    $('message').textContent=s.stage+': '+s.message;
    $('remaining').textContent=s.pending?'Esperando tarjeta para '+s.requestedId+' · '+s.remainingSeconds+' segundos':'';
    for(const key of ['uid','type','cardId','blockHex','expectedHex'])
      $(key).textContent=s[key]||'—';
    $('trace').textContent=s.trace;
    $('writer').style.display=s.mode==='escritor'?'block':'none';
    $('prepare').disabled=!s.connected||s.pending;
    $('cancel').disabled=!s.pending;
  } catch(error) {
    $('connection').textContent='Sin conexión con la ESP32. Revisar la red Wi-Fi.';
    $('prepare').disabled=true;
    $('cancel').disabled=true;
  } finally { setTimeout(poll,500); }
}
poll();
</script>
</html>
)HTML";

constexpr uint32_t WRITE_TIMEOUT_MS = 60000;
bool pending = false;
bool waitingNotice = false;
String requestedId;
uint32_t armedAtMs = 0;

void sendStatus() {
  const uint32_t elapsed = millis() - armedAtMs;
  const uint32_t remaining = pending && elapsed < WRITE_TIMEOUT_MS ?
      (WRITE_TIMEOUT_MS - elapsed + 999) / 1000 : 0;
  String body = "{\"mode\":\"escritor\",\"version\":" + jsonString(FIRMWARE_VERSION);
  body += ",\"connected\":" + String(readerConnected ? "true" : "false");
  body += ",\"readerVersion\":" + jsonString(hexBytes(&readerVersion, 1));
  body += ",\"stage\":" + jsonString(stage) + ",\"message\":" + jsonString(message);
  body += ",\"trace\":" + jsonString(trace);
  body += ",\"uid\":" + jsonString(cardUid) + ",\"type\":" + jsonString(cardType);
  body += ",\"cardId\":" + jsonString(cardId) + ",\"blockHex\":" + jsonString(blockHex);
  body += ",\"expectedHex\":" + jsonString(expectedHex);
  body += ",\"pending\":" + String(pending ? "true" : "false");
  body += ",\"requestedId\":" + jsonString(requestedId);
  body += ",\"remainingSeconds\":" + String(remaining) + "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", body);
}

void prepareWrite() {
  if (pending) { reply(409, "Hay una escritura pendiente; cancelarla o esperar su resultado"); return; }
  if (!checkReader(true)) { reply(503, "El RC522 no responde; revisar 3.3 V y cables SPI"); return; }
  const String id = server.arg("cardId");
  if (!validCardId(id)) { reply(400, "Card ID invalido: usar entre 1 y 16 letras, numeros, guiones o guiones bajos"); return; }
  if (server.arg("confirm") != "yes") { reply(400, "Falta confirmar la sobrescritura"); return; }
  clearCard();
  trace = "";
  requestedId = id;
  armedAtMs = millis();
  waitingNotice = false;
  lastDetectionErrorMs = 0;
  pending = true;
  report("ESPERANDO_TARJETA", "Acercar una sola tarjeta para escribir: " + requestedId);
  reply(202, "Escritura preparada");
}

void cancelWrite() {
  if (!pending) { reply(409, "No hay una escritura esperando tarjeta; revisar el ultimo resultado"); return; }
  pending = false;
  report("CANCELADO", "Escritura cancelada por el usuario.");
  reply(200, "Cancelado");
}

void writeSelectedCard() {
  describeCard();
  if (!authenticate()) { releaseCard(); return; }

  byte previous[16];
  report("LECTURA_PREVIA", "Leyendo el bloque 4 antes de modificarlo.");
  if (!readBlock(previous)) { releaseCard(); return; }
  report("CONTENIDO_PREVIO", blockHex);

  byte desired[16] = {0};
  for (size_t i = 0; i < requestedId.length(); ++i)
    desired[i] = static_cast<byte>(requestedId[i]);
  expectedHex = hexBytes(desired, sizeof(desired));
  report("ESCRIBIENDO", "Sobrescribiendo solamente el bloque de datos 4.");
  const auto status = rfid.MIFARE_Write(DATA_BLOCK, desired, sizeof(desired));
  if (status != MFRC522::STATUS_OK) {
    report("ERROR_ESCRITURA", "No se confirmo la escritura. Comprobar la tarjeta con el lector. " +
           String(rfid.GetStatusCodeName(status)));
    releaseCard();
    return;
  }

  byte actual[16];
  report("VERIFICANDO", "Volviendo a leer el bloque 4 y comparando los 16 bytes.");
  // Do not display the previous content as if it were a successful verification.
  blockHex = ""; cardId = "";
  if (!readBlock(actual)) {
    report("ERROR_VERIFICACION", "La escritura fue enviada, pero no se pudo releer. Comprobar con el lector.");
  } else if (memcmp(actual, desired, sizeof(desired)) != 0) {
    report("ERROR_VERIFICACION", "El contenido leido no coincide con los 16 bytes solicitados.");
  } else {
    report("ESCRITURA_OK", "Card ID escrito y verificado: " + cardId +
           ". Retirar la tarjeta. Usar el programa lector para una comprobacion independiente.");
  }
  releaseCard();
}

void setup() {
  beginHardware("RFID-Escritor");
  server.on("/", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", PAGE);
  });
  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/api/write", HTTP_POST, prepareWrite);
  server.on("/api/cancel", HTTP_POST, cancelWrite);
  server.onNotFound([] { reply(404, "Ruta inexistente en el programa escritor"); });
  server.begin();
}

void loop() {
  server.handleClient();
  const bool connected = checkReader();
  if (!pending) return;
  if (millis() - armedAtMs >= WRITE_TIMEOUT_MS) {
    pending = false;
    report("TIEMPO_AGOTADO", "No se escribio: termino la espera de 60 segundos. Revisar tarjeta y conexion.");
    return;
  }
  if (!connected) { pending = false; return; }
  if (millis() - lastPollMs < POLL_MS) return;
  lastPollMs = millis();
  const Selection selection = selectCard();
  if (selection == Selection::ABSENT) {
    if (!waitingNotice && millis() - armedAtMs >= 5000) {
      waitingNotice = true;
      report("ESPERANDO_TARJETA", "El RC522 responde, pero no detecta tarjeta. Usar una sola MIFARE Classic 1K a 13,56 MHz.");
    }
    return;
  }
  pending = false; // Consume each request once, including errors.
  if (selection == Selection::SELECTED) writeSelectedCard();
}
