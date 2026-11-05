#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "CooperativeServer.h"
#include <SPI.h>
#include <MFRC522.h>
#include <esp_system.h>
#include <string.h>

// ESP32 Dev Module + RC522, powered at 3.3 V.
constexpr byte PIN_SS = 5;
constexpr byte PIN_RST = 22;
constexpr byte PIN_SCK = 18;
constexpr byte PIN_MISO = 19;
constexpr byte PIN_MOSI = 23;
constexpr byte DATA_BLOCK = 4;
constexpr char WIFI_PASSWORD[] = "cambiar-esta-clave";
constexpr char FIRMWARE_VERSION[] = "3.2.1";
constexpr uint32_t POLL_MS = 100;

MFRC522 rfid(PIN_SS, PIN_RST);
void tickAttendance();
CooperativeServer server(80,tickAttendance);
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


struct PersonDef { const char* id; const char* name; const char* card; byte group; bool teacher; };
const char* const CLASSES[] = {"Ingeniería", "Economía"};
const PersonDef PEOPLE[] = {
 {"ALU_01","Lucía Fernández","A7F3C901",0,false},
 {"ALU_02","Martina Silva","A7F3C902",0,false},
 {"ALU_03","Mateo Rodríguez","A7F3C903",0,false},
 {"ALU_04","Valentina Pereira","A7F3C904",1,false},
 {"ALU_05","Santiago López","A7F3C905",1,false},
 {"ALU_06","Nicolás González","A7F3C906",1,false},
 {"PROF_01","Ana Martínez","B8D4E101",0,true},
 {"PROF_02","Martín Cabrera","B8D4E102",1,true}
};
struct Attempt {
 uint32_t number=0, at=0;
 String kind, stage, message, trace, uid, type, card, block, expected;
};
Attempt attempts[20];
byte attemptHead=0, attemptCount=0, activeSlot=0;
uint32_t attemptSerial=0;
bool attemptOpen=false, suppressAttendance=false;

enum class OperationState { IDLE, WAIT_DIAGNOSTIC, WAIT_WRITE, WRITING };
OperationState operationState=OperationState::IDLE;
String bootId;
uint32_t stateRevision=0, historyRevision=0;
bool isWritePending(){return operationState==OperationState::WAIT_WRITE;}
bool isDiagnosticPending(){return operationState==OperationState::WAIT_DIAGNOSTIC;}
bool isWriting(){return operationState==OperationState::WRITING;}
bool operationBusy(){return operationState!=OperationState::IDLE;}
void setOperation(OperationState state){
 if(operationState!=state){operationState=state;++stateRevision;}
}
const char* operationName(){
 switch(operationState){
  case OperationState::WAIT_DIAGNOSTIC:return "WAIT_DIAGNOSTIC";
  case OperationState::WAIT_WRITE:return "WAIT_WRITE";
  case OperationState::WRITING:return "WRITING";
  default:return "IDLE";
 }
}

bool waitingNotice=false;
String requestedId;
uint32_t armedAtMs=0;
constexpr uint32_t WRITE_TIMEOUT_MS=60000, DIAGNOSTIC_TIMEOUT_MS=30000;
String lastCardUid;
uint32_t lastCardSeenMs=0;
uint32_t lastRadioRecoveryMs=0;
void writeSelectedCard();

// JSON strings are escaped without requiring an extra JSON library.
String jsonString(const String& value) {
  String out;
  out.reserve(value.length()+16);
  out = "\"";
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
  ++stateRevision;
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
  out.reserve(count*3);
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
  ++stateRevision;
  cardUid = ""; cardType = ""; cardId = ""; blockHex = ""; expectedHex = "";
}

void releaseCard() {
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// A failed selection may leave the card in READY instead of IDLE/HALT.
// Briefly removing the RF field lets it be selected again without a power cycle.
void recoverCardSession() {
  rfid.PCD_StopCrypto1();
  rfid.PCD_AntennaOff();
  delay(10);
  rfid.PCD_AntennaOn();
  delay(10);
  lastRadioRecoveryMs=millis();
}

bool checkReader(bool first) {
  if (!first && millis() - lastProbeMs < 2000) return readerConnected;
  lastProbeMs = millis();
  if (!readerConnected) { rfid.PCD_Init(); delay(50); }
  const byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  const bool connected = version != 0x00 && version != 0xFF;
  if (first || connected != readerConnected) {
    readerVersion = version;
    if (connected)
      report("LECTOR_LISTO", "RC522 conectado. Version de chip: " + hexBytes(&version, 1));
    else
      report("ERROR_LECTOR", "El RC522 no responde (registro 00/FF). Revisar 3.3 V y cables SPI.");
  }
  if(readerVersion!=version)++stateRevision;
  readerVersion = version;
  readerConnected = connected;
  return connected;
}

Selection selectCard() {
  // Match PICC_IsNewCardPresent's radio setup, while keeping exact statuses.
  // VersionReg can respond correctly even when the RF path is misconfigured.
  rfid.PCD_StopCrypto1();
  rfid.PCD_WriteRegister(MFRC522::TxModeReg,0x00);
  rfid.PCD_WriteRegister(MFRC522::RxModeReg,0x00);
  rfid.PCD_WriteRegister(MFRC522::ModWidthReg,0x26);
  rfid.PCD_AntennaOn();
  byte atqa[2] = {};
  byte atqaSize = sizeof(atqa);
  // WUPA handles both newly arrived and already halted cards.
  auto status = rfid.PICC_WakeupA(atqa, &atqaSize);
  if (status == MFRC522::STATUS_TIMEOUT) {
    atqaSize = sizeof(atqa);
    status = rfid.PICC_RequestA(atqa, &atqaSize);
  }
  if (status == MFRC522::STATUS_TIMEOUT) {
    // READY/ACTIVE cards may ignore both short commands after an interrupted
    // exchange. A bounded RF reset allows the NEXT poll to select them again.
    // Never reinitialize SPI or recurse into HTTP; no card writes occur here.
    if(millis()-lastRadioRecoveryMs>=2000)recoverCardSession();
    return Selection::ABSENT;
  }
  if (status != MFRC522::STATUS_OK) {
    const String detail="Respuesta de radio inválida; presentar una sola tarjeta. "+String(rfid.GetStatusCodeName(status));
    if (lastDetectionErrorMs == 0 || millis() - lastDetectionErrorMs >= 2000) {
      report("ERROR_DETECCION",detail);
      lastDetectionErrorMs = millis();
    } else {stage="ERROR_DETECCION";message=detail;}
    recoverCardSession();
    return Selection::ERROR;
  }
  status = rfid.PICC_Select(&rfid.uid);
  if (status != MFRC522::STATUS_OK) {
    const String detail="Tarjeta detectada, pero no seleccionada: "+String(rfid.GetStatusCodeName(status));
    if (lastDetectionErrorMs == 0 || millis() - lastDetectionErrorMs >= 2000) {
      report("ERROR_SELECCION",detail);
      lastDetectionErrorMs = millis();
    } else {stage="ERROR_SELECCION";message=detail;}
    recoverCardSession();
    return Selection::ERROR;
  }
  if(rfid.uid.size!=4&&rfid.uid.size!=7&&rfid.uid.size!=10){
    report("ERROR_SELECCION","Longitud de UID inválida; se descartó la respuesta.");
    recoverCardSession();return Selection::ERROR;
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
  byte buffer[18] = {};
  byte size = sizeof(buffer);
  const auto status = rfid.MIFARE_Read(DATA_BLOCK, buffer, &size);
  if (status != MFRC522::STATUS_OK) {
    report("ERROR_LECTURA", "Fallo al leer el bloque 4: " + String(rfid.GetStatusCodeName(status)));
    return false;
  }
  if(size>sizeof(buffer)){
    report("ERROR_LECTURA","Longitud de respuesta inválida; se descartó el bloque.");return false;
  }
  if (size < 16) {
    report("ERROR_LECTURA", "Lectura incompleta del bloque 4: " + String(size) + " bytes; se necesitan 16.");
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
  bootId=String(esp_random(),HEX)+"-"+String(esp_random(),HEX);
  Serial.begin(115200);
  Serial.println();
  Serial.printf("%s | firmware %s\n", ssid, FIRMWARE_VERSION);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS);
  checkReader(true);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid, WIFI_PASSWORD)) Serial.println("ERROR: no se pudo crear la red Wi-Fi.");
  Serial.printf("Wi-Fi: %s | IP: %s | Monitor serie: 115200\n",
                ssid, WiFi.softAPIP().toString().c_str());
}


int personById(const String& id) {
 for(byte i=0;i<8;++i) if(id==PEOPLE[i].id)return i;
 return -1;
}
int personByCard(const String& card) {
 for(byte i=0;i<8;++i) if(card==PEOPLE[i].card)return i;
 return -1;
}
void snapshotAttempt() {
 if(!attemptOpen)return;
 Attempt& a=attempts[activeSlot];
 a.stage=stage; a.message=message; a.trace=trace.length()>1600?trace.substring(trace.length()-1600):trace;
 a.uid=cardUid; a.type=cardType; a.card=cardId; a.block=blockHex; a.expected=expectedHex;
}
void beginAttempt(const String& kind) {
 clearCard(); trace="";
 activeSlot=attemptHead; attemptHead=(attemptHead+1)%20;
 if(attemptCount<20)++attemptCount;
 attempts[activeSlot]=Attempt();
 attempts[activeSlot].number=++attemptSerial;
 attempts[activeSlot].at=millis();
 attempts[activeSlot].kind=kind;
 attempts[activeSlot].stage="EN_CURSO";
 attempts[activeSlot].message="Consultar el estado actual.";
 ++historyRevision;++stateRevision;
 attemptOpen=true;
 lastDetectionErrorMs=0;
}
void endAttempt() {
 if(!attemptOpen)return;
 snapshotAttempt();attemptOpen=false;++historyRevision;++stateRevision;
}
String peopleJson() {
 String s;s.reserve(1400);s="[";
 for(byte i=0;i<8;++i){
  if(i)s+=",";
  s+="{\"id\":"+jsonString(PEOPLE[i].id)+",\"name\":"+jsonString(PEOPLE[i].name);
  s+=",\"cardId\":"+jsonString(PEOPLE[i].card)+",\"class\":"+jsonString(CLASSES[PEOPLE[i].group]);
  s+=",\"teacher\":"+String(PEOPLE[i].teacher?"true":"false")+"}";
 }
 return s+"]";
}

String attemptJson(const Attempt& a){
 String s;
 s.reserve(768+a.trace.length()*2);
 s="{\"number\":";s+=String(a.number);s+=",\"atMs\":"+String(a.at)+",\"kind\":"+jsonString(a.kind);
 s+=",\"stage\":"+jsonString(a.stage)+",\"message\":"+jsonString(a.message);
 s+=",\"trace\":"+jsonString(a.trace)+",\"uid\":"+jsonString(a.uid);
 s+=",\"type\":"+jsonString(a.type)+",\"cardId\":"+jsonString(a.card);
 s+=",\"blockHex\":"+jsonString(a.block)+",\"expectedHex\":"+jsonString(a.expected)+"}";
 return s;
}
void sendPeople(){
 server.sendHeader("Cache-Control","no-store");
 server.send(200,"application/json; charset=utf-8","{\"people\":"+peopleJson()+"}");
}
void sendAttempts(){
 // Full bounded snapshot; each client replaces its copy, even after missed events.
 // Serialize one attempt per chunk, never an entire history-sized temporary.
 server.sendHeader("Cache-Control","no-store");
 server.setContentLength(CONTENT_LENGTH_UNKNOWN);
 server.send(200,"application/json; charset=utf-8","");
 const uint32_t newest=attemptCount?attempts[(attemptHead+19)%20].number:0;
 const uint32_t oldest=attemptCount?attempts[(attemptHead+20-attemptCount)%20].number:0;
 server.sendContent("{\"bootId\":"+jsonString(bootId)+",\"historyRevision\":"+String(historyRevision)+
                    ",\"oldestSequence\":"+String(oldest)+",\"newestSequence\":"+String(newest)+",\"attempts\":[");
 for(byte n=0;n<attemptCount;++n){
  if(n)server.sendContent(",");
  server.sendContent(attemptJson(attempts[(attemptHead+19-n)%20]));
  yield();
 }
 server.sendContent("]}");
 server.sendContent("");
}
String commonStatus(const char* mode) {
 const uint32_t duration=isWritePending()?WRITE_TIMEOUT_MS:DIAGNOSTIC_TIMEOUT_MS;
 const uint32_t elapsed=millis()-armedAtMs;
 String s;s.reserve(1024+trace.length()*2);
 s="{\"mode\":";s+=jsonString(mode);s+=",\"version\":"+jsonString(FIRMWARE_VERSION);
 s+=",\"bootId\":"+jsonString(bootId)+",\"stateRevision\":"+String(stateRevision);
 s+=",\"historyRevision\":"+String(historyRevision)+",\"operation\":"+jsonString(operationName());
 s+=",\"connected\":"+String(readerConnected?"true":"false");
 s+=",\"readerVersion\":"+jsonString(hexBytes(&readerVersion,1));
 s+=",\"stage\":"+jsonString(stage)+",\"message\":"+jsonString(message)+",\"trace\":"+jsonString(trace);
 s+=",\"uid\":"+jsonString(cardUid)+",\"type\":"+jsonString(cardType);
 s+=",\"cardId\":"+jsonString(cardId)+",\"blockHex\":"+jsonString(blockHex)+",\"expectedHex\":"+jsonString(expectedHex);
 s+=",\"pending\":"+String(isWritePending()?"true":"false")+",\"diagnosticPending\":"+String(isDiagnosticPending()?"true":"false");
 s+=",\"requestedId\":"+jsonString(requestedId);
 s+=",\"remainingSeconds\":"+String((isWritePending()||isDiagnosticPending())&&elapsed<duration?(duration-elapsed+999)/1000:0);

 return s;
}
bool readSelectedCard() {
 describeCard();
 if(!authenticate())return false;
 byte data[16];
 report("LEYENDO","Leyendo los 16 bytes del bloque 4.");
 if(!readBlock(data))return false;
 report("LECTURA_OK",cardId.isEmpty()?"Bloque leído sin un Card ID válido; consultar hexadecimal.":"Card ID leído: "+cardId);
 return true;
}
void diagnoseCard() {
 if(operationBusy()){reply(409,"Hay una operación pendiente.");return;}
 if(!checkReader(true)){reply(503,"El RC522 no responde.");return;}
 recoverCardSession();
 beginAttempt("DIAGNOSTICO");
 suppressAttendance=true;
 setOperation(OperationState::WAIT_DIAGNOSTIC); waitingNotice=false; armedAtMs=millis();
 report("ESPERANDO_TARJETA","Prueba sin escritura ni registro de asistencia. Acercar una tarjeta.");
 reply(202,"Diagnóstico preparado.");
}
void cancelOperation() {
 if(!isWritePending()&&!isDiagnosticPending()){reply(409,"No hay una operación esperando tarjeta; revisar el resultado.");return;}
 setOperation(OperationState::IDLE);
 report("CANCELADO","Operación cancelada."); endAttempt(); reply(200,"Cancelado.");
}
void resetReader() {
 if(operationBusy()){reply(409,"Cancelar o terminar la operación antes de reiniciar el RC522.");return;}
 beginAttempt("REINICIO_LECTOR");
 readerConnected=false; lastCardUid="";
 checkReader(true);
 endAttempt();reply(readerConnected?200:503,readerConnected?"RC522 reiniciado. La asistencia no fue modificada.":"El RC522 no responde.");
}
void diagnosticReport() {
 server.sendHeader("Cache-Control","no-store");
 server.setContentLength(CONTENT_LENGTH_UNKNOWN);
 server.send(200,"text/plain; charset=utf-8","");
 String header="RFID "+String(FIRMWARE_VERSION)+"\nESP32: responde\nRC522: "+String(readerConnected?"conectado":"sin respuesta");
 header+="\nPrograma: asistencia y administración";
 header+="\nRegistro de versión: "+hexBytes(&readerVersion,1)+"\n";
 server.sendContent(header);
 for(byte n=0;n<attemptCount;++n){
  const byte slot=(attemptHead+19-n)%20;
  const Attempt& a=attempts[slot];
  server.sendContent("\n--- Intento "+String(a.number)+" / "+a.kind+" / "+String(a.at)+" ms ---\n");
  if(attemptOpen&&slot==activeSlot)
   server.sendContent(trace+"\nResultado actual: "+stage+" / "+message+"\n");
  else
   server.sendContent(a.trace+"\nResultado: "+a.stage+" / "+a.message+"\n");
  yield();
 }
 server.sendContent("");
}
bool pollManual(bool connected) {
 if(!isWritePending()&&!isDiagnosticPending())return false;
 const uint32_t duration=isWritePending()?WRITE_TIMEOUT_MS:DIAGNOSTIC_TIMEOUT_MS;
 if(!connected){
  setOperation(OperationState::IDLE);
  report("ERROR_LECTOR","Operación interrumpida: el RC522 no responde.");endAttempt();return true;
 }
 if(millis()-armedAtMs>=duration){
  setOperation(OperationState::IDLE);
  report("TIEMPO_AGOTADO","Terminó la espera sin completar una operación.");endAttempt();return true;
 }
 if(millis()-lastPollMs<POLL_MS)return true;
 lastPollMs=millis();
 const Selection selection=selectCard();
 if(selection==Selection::ABSENT){
  if(!waitingNotice&&millis()-armedAtMs>=5000){
   waitingNotice=true;report("ESPERANDO_TARJETA","RC522 conectado; aún no detecta una tarjeta de 13,56 MHz.");
  }
  return true;
 }
 if(selection==Selection::SELECTED){
  lastCardUid=hexBytes(rfid.uid.uidByte,rfid.uid.size);lastCardSeenMs=millis();
  if(isWritePending()){setOperation(OperationState::WRITING);writeSelectedCard();setOperation(OperationState::IDLE);}
  else
  {setOperation(OperationState::IDLE);readSelectedCard();releaseCard();}
 }
 setOperation(OperationState::IDLE);endAttempt();
 return true;
}
void registerCommonRoutes() {
 server.on("/api/people",HTTP_GET,sendPeople);
 server.on("/api/attempts",HTTP_GET,sendAttempts);
 server.on("/api/diagnose",HTTP_POST,diagnoseCard);
 server.on("/api/cancel",HTTP_POST,cancelOperation);
 server.on("/api/reader/reset",HTTP_POST,resetReader);
 server.on("/api/diagnostic/report",HTTP_GET,diagnosticReport);
}

#include <LittleFS.h>
#include <esp_system.h>
#include <time.h>
#include <stdlib.h>

constexpr uint32_t SESSION_MS=600000;
struct AttendanceRecord {
 uint32_t magic, version, nonce;
 byte teacher, present, reason, reserved;
 int64_t opened, closed, scanned[6];
 uint32_t checksum;
};
constexpr size_t RECORD_SIZE=offsetof(AttendanceRecord,checksum)+sizeof(uint32_t);
static_assert(RECORD_SIZE==84,"Formato de registro incompatible");
uint32_t recordChecksum(const AttendanceRecord& r);
bool validRecord(const AttendanceRecord& r);
bool saveRecord(const String& path,AttendanceRecord r);
bool readRecord(File& f,AttendanceRecord& r);
String recordJson(const AttendanceRecord& r);
AttendanceRecord currentRecord={};
enum class AttendanceState { CLOSED, OPEN, CLOSE_PENDING };
AttendanceState attendanceState=AttendanceState::CLOSED;
void setAttendanceState(AttendanceState state);
bool isSessionActive(){return attendanceState!=AttendanceState::CLOSED;}
bool isClosePending(){return attendanceState==AttendanceState::CLOSE_PENDING;}
void setAttendanceState(AttendanceState state){
 if(attendanceState!=state){attendanceState=state;++stateRevision;}
}
bool storageReady=false, sessionValid=false;
String sessionPath, attendanceMessage="[CERRADO] Pase tarjeta de docente.";
uint32_t openedMs=0, lastCloseAttempt=0, clockAtMs=0;
int64_t clockEpoch=0;

void setAttendanceMessage(const String& text){
 attendanceMessage=text;++stateRevision;
}
uint32_t recordChecksum(const AttendanceRecord& r) {
 const byte* data=reinterpret_cast<const byte*>(&r);
 uint32_t hash=2166136261U;
 for(size_t i=0;i<offsetof(AttendanceRecord,checksum);++i){hash^=data[i];hash*=16777619U;}
 return hash;
}
bool validRecord(const AttendanceRecord& r) {
 if(r.magic!=0x52464944||r.version!=1||r.teacher<6||r.teacher>7||r.reason>2||r.checksum!=recordChecksum(r))return false;
 const byte allowed=PEOPLE[r.teacher].group==0?0x07:0x38;
 return (r.present&~allowed)==0;
}
bool saveRecord(const String& path,AttendanceRecord r) {
 r.checksum=recordChecksum(r);
 const String temp=path+".tmp";
 File f=LittleFS.open(temp,"w");
 if(!f)return false;
 const bool complete=f.write(reinterpret_cast<const byte*>(&r),RECORD_SIZE)==RECORD_SIZE;
 f.flush();f.close();
 if(!complete){LittleFS.remove(temp);return false;}
 // LittleFS atomically replaces an existing destination; never delete it first.
 return LittleFS.rename(temp,path);
}
bool readRecord(File& f,AttendanceRecord& r) {
 memset(&r,0,sizeof(r));
 return f&&f.size()==RECORD_SIZE&&f.read(reinterpret_cast<byte*>(&r),RECORD_SIZE)==RECORD_SIZE&&validRecord(r);
}
bool beginAttendanceStorage() {
 storageReady=false;sessionValid=false;setAttendanceState(AttendanceState::CLOSED);++stateRevision;
 if(!LittleFS.begin(false)) {setAttendanceMessage("[ERROR] No se pudo montar LittleFS. No se formateó.");return false;}
 if(!LittleFS.exists("/sessions")&&!LittleFS.mkdir("/sessions"))return false;
 File dir=LittleFS.open("/sessions");if(!dir||!dir.isDirectory())return false;
 File f=dir.openNextFile();
 while(f){
  if(!f.isDirectory()&&String(f.name()).endsWith(".bin")){
   const String path=f.path();AttendanceRecord r;
   const bool ok=readRecord(f,r);f.close();
   if(!ok){setAttendanceMessage("[ERROR] Registro dañado; no se borró: "+path);return false;}
   if(r.reason==0){r.reason=2;r.closed=0;if(!saveRecord(path,r)){setAttendanceMessage("[ERROR] No se pudo conservar el cierre por reinicio.");return false;}}
   if(!sessionValid||r.opened>currentRecord.opened||(r.opened==currentRecord.opened&&r.nonce>currentRecord.nonce)){
    currentRecord=r;sessionPath=path;sessionValid=true;
   }
  }else f.close();
  f=dir.openNextFile();
 }
 storageReady=true;
 if(sessionValid)setAttendanceMessage(currentRecord.reason==2?"[CERRADO] Sesión interrumpida por reinicio; presentes conservados.":"[CERRADO] Última sesión conservada.");
 return true;
}
int64_t epochNow() {return clockEpoch?clockEpoch+(millis()-clockAtMs)/1000:0;}
String timestamp(int64_t epoch) {
 if(!epoch)return "";
 time_t t=static_cast<time_t>(epoch);struct tm value;localtime_r(&t,&value);
 char out[32];strftime(out,sizeof(out),"%Y-%m-%dT%H:%M:%S-03:00",&value);
 return String(out);
}
String recordJson(const AttendanceRecord& r) {
 String s; s.reserve(1024);s="{\"teacherId\":";s+=jsonString(PEOPLE[r.teacher].id);s+=",\"teacher\":"+jsonString(PEOPLE[r.teacher].name);
 s+=",\"class\":"+jsonString(CLASSES[PEOPLE[r.teacher].group]);
 s+=",\"openedAt\":"+jsonString(timestamp(r.opened))+",\"closedAt\":"+jsonString(timestamp(r.closed));
 s+=",\"closeReason\":"+jsonString(r.reason==0?"OPEN":r.reason==1?"TIMEOUT":"DEVICE_RESTART");
 s+=",\"presentStudents\":[";
 bool first=true;
 for(byte i=0;i<6;++i)if(r.present&(1<<i)){
  if(!first)s+=",";
  first=false;
  s+="{\"id\":"+jsonString(PEOPLE[i].id)+",\"name\":"+jsonString(PEOPLE[i].name)+",\"scannedAt\":"+jsonString(timestamp(r.scanned[i]))+"}";
 }
 return s+"]}";
}
void tickAttendance() {
 if(!isSessionActive()||millis()-openedMs<SESSION_MS)return;
 if(isClosePending()&&millis()-lastCloseAttempt<5000)return;
 lastCloseAttempt=millis();
 AttendanceRecord finished=currentRecord;finished.reason=1;finished.closed=finished.opened+600;
 if(!saveRecord(sessionPath,finished)){
  setAttendanceState(AttendanceState::CLOSE_PENDING);setAttendanceMessage("[ERROR] Ventana cerrada; se reintentará guardar el cierre.");return;
 }
 currentRecord=finished;setAttendanceState(AttendanceState::CLOSED);
 setAttendanceMessage("[CERRADO] Asistencia cerrada automáticamente; registros conservados.");
}
void attendanceScan(const String& card) {
 tickAttendance();
 const int person=personByCard(card);
 if(!storageReady){setAttendanceMessage("[ERROR] Almacenamiento no disponible.");return;}
 if(isClosePending()){setAttendanceMessage("[ERROR] Cierre pendiente de guardar; no se admiten registros.");return;}
 if(person<0){setAttendanceMessage("[ERROR] Tarjeta no registrada.");return;}
 if(!isSessionActive()){
  if(!PEOPLE[person].teacher){setAttendanceMessage("[CERRADO] Solo un docente puede abrir la clase.");return;}
  if(!clockEpoch){setAttendanceMessage("[ERROR] Sincronizar la hora antes de abrir asistencia.");return;}
  AttendanceRecord created;memset(&created,0,sizeof(created));
  created.magic=0x52464944;created.version=1;created.teacher=person;created.opened=epochNow();
  String path;
  do {created.nonce=esp_random();path="/sessions/"+String(static_cast<unsigned long>(created.opened))+"-"+String(created.nonce,HEX)+".bin";}while(LittleFS.exists(path));
  if(!saveRecord(path,created)){setAttendanceMessage("[ERROR] No se pudo crear la sesión.");return;}
  currentRecord=created;sessionPath=path;sessionValid=true;setAttendanceState(AttendanceState::OPEN);openedMs=millis();
  setAttendanceMessage("[OK] Clase abierta: "+String(CLASSES[PEOPLE[person].group]));return;
 }
 if(PEOPLE[person].teacher){setAttendanceMessage("[ERROR] Ya hay una sesión abierta; no se reemplazó.");return;}
 if(PEOPLE[person].group!=PEOPLE[currentRecord.teacher].group){setAttendanceMessage("[ERROR] Estudiante de otra clase.");return;}
 if(currentRecord.present&(1<<person)){setAttendanceMessage("[OK] Ya estaba registrado; no se duplicó.");return;}
 AttendanceRecord updated=currentRecord;updated.present|=1<<person;updated.scanned[person]=epochNow();
 if(!saveRecord(sessionPath,updated)){setAttendanceMessage("[ERROR] No se pudo guardar; volver a pasar la tarjeta.");return;}
 currentRecord=updated;setAttendanceMessage("[OK] Alumno registrado: "+String(PEOPLE[person].name));
}
String attendanceJson() {
 const bool open=isSessionActive()&&millis()-openedMs<SESSION_MS;
 String s; s.reserve(1400);s="{\"storageReady\":";s+=String(storageReady?"true":"false");s+=",\"clockReady\":"+String(clockEpoch?"true":"false");
 s+=",\"active\":"+String(open?"true":"false")+",\"closePending\":"+String(isClosePending()?"true":"false");
 s+=",\"message\":"+jsonString(attendanceMessage);
 s+=",\"remainingSeconds\":"+String(open?(SESSION_MS-(millis()-openedMs)+999)/1000:0);
 s+=",\"session\":"+String(sessionValid?recordJson(currentRecord):"null");
 return s+"}";
}
void synchronizeClock() {
 if(isSessionActive()){reply(409,"No se cambia la hora durante una sesión.");return;}
 const String text=server.arg("epoch");char* end;
 const long long epoch=strtoll(text.c_str(),&end,10);
 if(end==text.c_str()||*end||epoch<1704067200LL||epoch>4102444800LL){reply(400,"Fecha inválida.");return;}
 clockEpoch=epoch;clockAtMs=millis();++stateRevision;reply(200,"Hora sincronizada (Uruguay, UTC-3).");
}
void initializeStorage() {
 if(storageReady||isSessionActive()){reply(409,"No se permite formatear un almacenamiento montado o una sesión abierta.");return;}
 if(server.arg("confirmation")!="INICIALIZAR"){reply(400,"Se requiere confirmación explícita: INICIALIZAR. Puede borrar datos previos.");return;}
 if(!LittleFS.format()||!beginAttendanceStorage()){reply(500,"No se pudo inicializar LittleFS.");return;}
 reply(200,"Almacenamiento preparado.");
}
void exportAttendance() {
 if(!storageReady){reply(503,"Almacenamiento no disponible.");return;}
 server.sendHeader("Cache-Control","no-store");
 server.sendHeader("Content-Disposition","attachment; filename=asistencias.ndjson");
 server.setContentLength(CONTENT_LENGTH_UNKNOWN);
 server.send(200,"application/x-ndjson; charset=utf-8","");
 tickAttendance();
 File dir=LittleFS.open("/sessions");File f=dir.openNextFile();
 while(f){
  tickAttendance();
  if(!server.client().connected()){f.close();break;}
  if(!f.isDirectory()&&String(f.name()).endsWith(".bin")){
   AttendanceRecord r;
   if(readRecord(f,r)){
    // A deadline may atomically replace this file while its old handle is open.
    if(sessionValid&&sessionPath==String(f.path()))r=currentRecord;
    server.sendContent(recordJson(r)+"\n");
   }else server.sendContent("{\"error\":\"Registro dañado\"}\n");
  }
  f.close();tickAttendance();yield();f=dir.openNextFile();
 }
 tickAttendance();
 server.sendContent("");
 tickAttendance();
}
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
<p id="attendanceMessage" role="status" aria-live="polite"></p><p id="lesson"></p><p id="countdown"></p><p id="clockState"></p>
<ul id="present"></ul><button id="sync">Sincronizar hora del teléfono</button><a href="/api/attendance/export" download>Descargar registros</a>
<div id="initialize" hidden><p>[ERROR] LittleFS no disponible. Inicializar puede borrar datos previos. No se hace automáticamente.</p>
<label>Confirmación: escribir INICIALIZAR<input id="storageConfirmation" autocomplete="off"></label><button id="initStorage">Inicializar almacenamiento</button></div></section>
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
 for(const id of ['prepare','diagnose','resetReader','cancel','sync','initStorage'])$(id).disabled=true;
}
async function action(path,fields={}){
 if(actionSending)return;
 actionSending=true;$('requestError').textContent='';
 try{
  const r=await fetch(path,{method:'POST',cache:'no-store',body:new URLSearchParams(fields)});
  const d=await r.json();$('requestError').textContent=(r.ok?'[OK] ':'[ERROR] ')+d.message;
  if(r.ok&&path==='/api/write')$('confirm').checked=false;
 }catch(e){$('requestError').textContent='[ERROR] No se pudo confirmar la solicitud; consultar el resultado antes de reintentar.';}
 finally{actionSending=false;requestSync();}
}
$('writeForm').addEventListener('submit',e=>{e.preventDefault();action('/api/write',{personId:$('person').value,confirm:$('confirm').checked?'yes':'no'});});
$('diagnose').onclick=()=>action('/api/diagnose');
$('resetReader').onclick=()=>action('/api/reader/reset');
$('cancel').onclick=()=>action('/api/cancel');
$('sync').onclick=()=>action('/api/time',{epoch:Math.floor(Date.now()/1000)});
$('initStorage').onclick=()=>action('/api/storage/init',{confirmation:$('storageConfirmation').value});
$('copy').onclick=async()=>{
 try{
  const r=await fetch('/api/diagnostic/report',{cache:'no-store'});if(!r.ok)throw Error();
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
 for(const p of people){const o=document.createElement('option');o.value=p.id;o.textContent=p.name+' // '+(p.teacher?'docente':'estudiante')+' // '+p.class;$('person').append(o);}
 peopleLoaded=true;selectedPerson();
}
function updateCountdown(s){
 const working=busy(s);
 $('remaining').textContent=working?'[ESPERANDO] '+s.remainingSeconds+' segundos':'';
 if(s.attendance){
  const a=s.attendance,n=a.remainingSeconds,q=a.session;
  $('countdown').textContent=a.active?'[OK] ABIERTA // '+String(Math.floor(n/60)).padStart(2,'0')+':'+String(n%60).padStart(2,'0'):'[CERRADO] '+(q?q.closeReason:'');
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
  $('lesson').textContent=q?q.class+' // '+q.teacher+' // Presentes: '+q.presentStudents.length+'/3':'[CERRADO] Sin sesión';
  $('clockState').textContent=a.clockReady?'[OK] Hora sincronizada (UTC-3)':'[ESPERANDO] Sincronizar la hora del teléfono';
  const entries=q?q.presentStudents:[];
  const serialized=JSON.stringify(entries);
  if(serialized!==presentText){
   presentText=serialized;$('present').replaceChildren();
   for(const p of entries){const li=document.createElement('li');li.textContent='[OK] '+p.name+' // '+p.scannedAt;$('present').append(li);}
  }
  $('initialize').hidden=a.storageReady;$('sync').disabled=a.active||a.closePending;
  $('initStorage').disabled=working||a.storageReady;
 }
}
function invalidateBoot(nextBoot){
 bootId=nextBoot;historyRevision=null;renderedKey='';historyText='';presentText='';current=null;
 $('history').replaceChildren();$('present').replaceChildren();
 $('trace').textContent='';$('report').value='';$('copyFallback').hidden=true;
 $('confirm').checked=false;
 $('esp').textContent='[ESPERANDO] Sincronizando el inicio de la ESP32';
 for(const id of ['prepare','diagnose','resetReader','cancel','sync','initStorage'])$(id).disabled=true;
}
async function fetchJson(path,signal){
 const r=await fetch(path,{cache:'no-store',signal});if(!r.ok)throw Error('HTTP '+r.status);return r.json();
}
function pollDelay(){
 return busy(current)||(current&&current.attendance&&(current.attendance.active||current.attendance.closePending))?500:2000;
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

void sendStatus(){
 server.sendHeader("Cache-Control","no-store");
 server.send(200,"application/json; charset=utf-8",commonStatus("rfid")+",\"attendance\":"+attendanceJson()+"}");
}
void prepareWrite(){
 if(operationBusy()){reply(409,"Hay una operación pendiente.");return;}
 const int person=personById(server.arg("personId"));
 if(person<0){reply(400,"Solo se admiten las ocho personas fijas.");return;}
 if(server.arg("confirm")!="yes"){reply(400,"Falta confirmar la sobrescritura.");return;}
 if(!checkReader(true)){reply(503,"El RC522 no responde.");return;}
 recoverCardSession();
 beginAttempt("ESCRITURA");
 suppressAttendance=true;
 requestedId=PEOPLE[person].card;
 armedAtMs=millis();waitingNotice=false;setOperation(OperationState::WAIT_WRITE);
 report("ESPERANDO_TARJETA","Acercar tarjeta para "+String(PEOPLE[person].name)+" // "+requestedId);
 reply(202,"Escritura preparada.");
}
void writeSelectedCard() {
  if(!validCardId(requestedId)){
    report("ERROR_ESCRITURA","Card ID inválido; no se escribió la tarjeta.");releaseCard();return;
  }
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
           ". Retirar la tarjeta antes de usarla para asistencia.");
  }
  releaseCard();
}


void setup(){
 setenv("TZ","UYT3",1);tzset();
 beginHardware("RFID-ESP32");beginAttendanceStorage();
 server.on("/",HTTP_GET,[]{server.sendHeader("Cache-Control","no-store");server.send_P(200,"text/html; charset=utf-8",PAGE);});
 server.on("/admin",HTTP_GET,[]{server.sendHeader("Cache-Control","no-store");server.send_P(200,"text/html; charset=utf-8",PAGE);});
 server.on("/admin/",HTTP_GET,[]{server.sendHeader("Cache-Control","no-store");server.send_P(200,"text/html; charset=utf-8",PAGE);});
 server.on("/api/write",HTTP_POST,prepareWrite);
 server.on("/api/status",HTTP_GET,sendStatus);
 server.on("/api/time",HTTP_POST,synchronizeClock);
 server.on("/api/storage/init",HTTP_POST,initializeStorage);
 server.on("/api/attendance/export",HTTP_GET,exportAttendance);
 registerCommonRoutes();server.onNotFound([]{reply(404,"Ruta inexistente.");});server.begin();
}
void loop(){
 tickAttendance();server.handleClient();tickAttendance();
 const bool connected=checkReader();
 if(pollManual(connected))return;
 if(!connected){lastCardUid="";return;}
 if(millis()-lastPollMs<POLL_MS)return;
 lastPollMs=millis();
 const Selection selection=selectCard();
 if(selection==Selection::ABSENT){if(millis()-lastCardSeenMs>=400){lastCardUid="";suppressAttendance=false;}return;}
 if(selection==Selection::ERROR){
  const String errorStage=stage,errorMessage=message;
  beginAttempt("LECTURA");report(errorStage.c_str(),errorMessage);endAttempt();lastCardUid="";return;
 }
 const String uid=hexBytes(rfid.uid.uidByte,rfid.uid.size);lastCardSeenMs=millis();
 if(suppressAttendance){lastCardUid=uid;releaseCard();return;}
 if(uid==lastCardUid){releaseCard();return;}lastCardUid=uid;
 beginAttempt("ASISTENCIA");
 if(readSelectedCard()){
  attendanceScan(cardId);
  trace+="[ASISTENCIA] "+attendanceMessage+"\n";
 }
 releaseCard();endAttempt();
}
