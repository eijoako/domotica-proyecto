#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "CooperativeServer.h"
#include <SPI.h>
#include <MFRC522.h>
#include <esp_system.h>
#include <string.h>
#include "WebPage.h"

// ESP32 Dev Module + RC522, powered at 3.3 V.
constexpr byte PIN_SS = 5;
constexpr byte PIN_RST = 22;
constexpr byte PIN_SCK = 18;
constexpr byte PIN_MISO = 19;
constexpr byte PIN_MOSI = 23;
constexpr byte DATA_BLOCK = 4;
constexpr char WIFI_PASSWORD[] = "cambiar-esta-clave";
constexpr char FIRMWARE_VERSION[] = "4.0.0";
constexpr uint32_t POLL_MS = 250;

MFRC522 rfid(PIN_SS, PIN_RST);
void tickAttendance();
CooperativeServer server(80,tickAttendance);
constexpr char ADMIN_USER[]="admin";
constexpr char ADMIN_PASSWORD[]="cambiar-clave-admin";
bool requireAdmin(){
 if(server.authenticate(ADMIN_USER,ADMIN_PASSWORD))return true;
 server.requestAuthentication();return false;
}

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


#include "People.h"
struct Attempt {
 uint32_t number=0, at=0;
 String kind, stage, message, trace, uid, type, card, block, expected;
};
String attemptJson(const Attempt& a);
Attempt attempts[20];
byte attemptHead=0, attemptCount=0, activeSlot=0;
uint32_t attemptSerial=0;
bool attemptOpen=false, suppressAttendance=false;

enum class OperationState { IDLE, WAIT_DIAGNOSTIC, WAIT_WRITE, WRITING };
void setOperation(OperationState state);
OperationState operationState=OperationState::IDLE;
String bootId;
uint32_t stateRevision=0, historyRevision=0;
bool isWritePending(){return operationState==OperationState::WAIT_WRITE;}
bool isDiagnosticPending(){return operationState==OperationState::WAIT_DIAGNOSTIC;}
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
bool writeSelectedCard();
bool readBlock(byte (&data)[16]);

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
  // Standard discovery first; WUPA is only needed for an already halted card.
  auto status = rfid.PICC_RequestA(atqa, &atqaSize);
  if (status == MFRC522::STATUS_TIMEOUT) {
    atqaSize = sizeof(atqa);
    status = rfid.PICC_WakeupA(atqa, &atqaSize);
  }
  if (status == MFRC522::STATUS_TIMEOUT) return Selection::ABSENT;
  // PICC_IsNewCardPresent also accepts a collision: SELECT implements the
  // library's anticollision protocol and validates the resulting UID/SAK.
  if (status != MFRC522::STATUS_OK && status != MFRC522::STATUS_COLLISION) {
    const byte fifo=rfid.PCD_ReadRegister(MFRC522::FIFOLevelReg);
    const String detail="DETECCION ATQA (buffer 2 bytes), FIFO="+String(fifo)+": "+String(rfid.GetStatusCodeName(status));
    if (stage!="ERROR_DETECCION" || message!=detail || lastDetectionErrorMs == 0 || millis() - lastDetectionErrorMs >= 2000) {
      report("ERROR_DETECCION",detail);
      lastDetectionErrorMs = millis();
    }
    recoverCardSession();
    return Selection::ERROR;
  }
  status = rfid.PICC_Select(&rfid.uid);
  if (status != MFRC522::STATUS_OK) {
    const String detail="SELECT/anticolision: "+String(rfid.GetStatusCodeName(status));
    if (stage!="ERROR_SELECCION" || message!=detail || lastDetectionErrorMs == 0 || millis() - lastDetectionErrorMs >= 2000) {
      report("ERROR_SELECCION",detail);
      lastDetectionErrorMs = millis();
    }
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
  report("AUTENTICANDO", "Sector 1 (autenticacion en bloque 7), Key A predeterminada FF FF FF FF FF FF.");
  MFRC522::MIFARE_Key key;
  memset(key.keyByte, 0xFF, sizeof(key.keyByte));
  const auto status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A,
                                           DATA_BLOCK+3, &key, &rfid.uid);
  if (status != MFRC522::STATUS_OK) {
    report("ERROR_AUTENTICACION", "No se pudo autenticar. Puede haber otra clave o un problema de radio. " +
           String(rfid.GetStatusCodeName(status)));
    return false;
  }
  return true;
}

bool readBlock(byte (&data)[16]) {
  byte buffer[18] = {};
  byte size = sizeof(buffer);
  const auto status = rfid.MIFARE_Read(DATA_BLOCK, buffer, &size);
  if (status != MFRC522::STATUS_OK) {
    report("ERROR_LECTURA", "READ bloque 4 (buffer 18 bytes): " + String(rfid.GetStatusCodeName(status)));
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
 for(size_t i=0;i<PERSON_COUNT;++i) if(id==PEOPLE[i].id)return i;
 return -1;
}
int personByCard(const String& card) {
 for(size_t i=0;i<PERSON_COUNT;++i) if(card==PEOPLE[i].card)return i;
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
 for(size_t i=0;i<PERSON_COUNT;++i){
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
 if(!requireAdmin())return;
 if(operationBusy()){reply(409,"Hay una operación pendiente.");return;}
 if(!checkReader(true)){reply(503,"El RC522 no responde.");return;}
 beginAttempt("DIAGNOSTICO");
 suppressAttendance=true;
 setOperation(OperationState::WAIT_DIAGNOSTIC); waitingNotice=false; lastPollMs=millis()-POLL_MS;armedAtMs=millis();
 report("ESPERANDO_TARJETA","Prueba sin escritura ni registro de asistencia. Acercar una tarjeta.");
 reply(202,"Diagnóstico preparado.");
}
void cancelOperation() {
 if(!requireAdmin())return;
 if(!isWritePending()&&!isDiagnosticPending()){reply(409,"No hay una operación esperando tarjeta; revisar el resultado.");return;}
 setOperation(OperationState::IDLE);
 report("CANCELADO","Operación cancelada."); endAttempt(); reply(200,"Cancelado.");
}
void resetReader() {
 if(!requireAdmin())return;
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
void registerCommonRoutes() {
 server.on("/api/people",HTTP_GET,sendPeople);
 server.on("/api/attempts",HTTP_GET,sendAttempts);
 server.on("/api/diagnose",HTTP_POST,diagnoseCard);
 server.on("/api/cancel",HTTP_POST,cancelOperation);
 server.on("/api/reader/reset",HTTP_POST,resetReader);
 server.on("/api/diagnostic/report",HTTP_GET,diagnosticReport);
}


constexpr uint32_t SESSION_MS=600000;
bool sessionValid=false,sessionActive=false;
size_t sessionTeacher=0;
bool present[PERSON_COUNT]={};
uint32_t openedMs=0;
String attendanceMessage="[CERRADO] Pase tarjeta de docente.";
void setAttendanceMessage(const String& text){attendanceMessage=text;++stateRevision;}
void tickAttendance(){
 if(sessionActive&&uint32_t(millis()-openedMs)>=SESSION_MS){
  sessionActive=false;setAttendanceMessage("[CERRADO] Asistencia cerrada.");
 }
}
void attendanceScan(const String& card){
 tickAttendance();
 const int person=personByCard(card);
 if(person<0){setAttendanceMessage("[ERROR] Tarjeta no registrada.");return;}
 if(!sessionActive){
  if(!PEOPLE[person].teacher){setAttendanceMessage("[CERRADO] Solo un docente puede abrir la clase.");return;}
  memset(present,0,sizeof(present));sessionTeacher=person;openedMs=millis();sessionActive=sessionValid=true;
  setAttendanceMessage("[OK] Clase abierta: "+String(CLASSES[PEOPLE[person].group]));return;
 }
 if(PEOPLE[person].teacher){setAttendanceMessage("[ERROR] Ya hay una sesión abierta; no se reemplazó.");return;}
 if(PEOPLE[person].group!=PEOPLE[sessionTeacher].group){setAttendanceMessage("[ERROR] Estudiante de otra clase.");return;}
 if(present[person]){setAttendanceMessage("[OK] Ya estaba registrado; no se duplicó.");return;}
 present[person]=true;setAttendanceMessage("[OK] Alumno registrado: "+String(PEOPLE[person].name));
}
String attendanceJson(){
 const uint32_t elapsed=millis()-openedMs;
 const bool open=sessionActive&&elapsed<SESSION_MS;
 String s="{\"active\":"+String(open?"true":"false")+",\"message\":"+jsonString(attendanceMessage);
 s+=",\"remainingSeconds\":"+String(open?(SESSION_MS-elapsed+999)/1000:0)+",\"session\":";
 if(!sessionValid)return s+"null}";
 size_t capacity=0;
 for(const auto& p:PEOPLE)if(!p.teacher&&p.group==PEOPLE[sessionTeacher].group)++capacity;
 s+="{\"teacherId\":"+jsonString(PEOPLE[sessionTeacher].id)+",\"teacher\":"+jsonString(PEOPLE[sessionTeacher].name)+",\"class\":"+jsonString(CLASSES[PEOPLE[sessionTeacher].group]);
 s+=",\"capacity\":"+String(capacity)+",\"presentStudents\":[";
 bool first=true;
 for(size_t i=0;i<PERSON_COUNT;++i)if(present[i]){
  if(!first)s+=",";
  first=false;
  s+="{\"id\":"+jsonString(PEOPLE[i].id)+",\"name\":"+jsonString(PEOPLE[i].name)+"}";
 }
 return s+"]}}";
}
void sendStatus(){
 server.sendHeader("Cache-Control","no-store");
 server.send(200,"application/json; charset=utf-8",commonStatus("rfid")+",\"attendance\":"+attendanceJson()+"}");
}
void prepareWrite(){
 if(!requireAdmin())return;
 if(operationBusy()){reply(409,"Hay una operación pendiente.");return;}
 const int person=personById(server.arg("personId"));
 if(person<0){reply(400,"La persona no está definida en el firmware.");return;}
 if(server.arg("confirm")!="yes"){reply(400,"Falta confirmar la sobrescritura.");return;}
 if(!checkReader(true)){reply(503,"El RC522 no responde.");return;}
 beginAttempt("ESCRITURA");
 suppressAttendance=true;
 requestedId=PEOPLE[person].card;
 lastPollMs=millis()-POLL_MS;armedAtMs=millis();waitingNotice=false;setOperation(OperationState::WAIT_WRITE);
 report("ESPERANDO_TARJETA","Acercar tarjeta para "+String(PEOPLE[person].name)+" // "+requestedId);
 reply(202,"Escritura preparada.");
}
bool writeSelectedCard() {
  if(!validCardId(requestedId)){
    report("ERROR_ESCRITURA","Card ID inválido; no se escribió la tarjeta.");return false;
  }
  describeCard();
  if (!authenticate()) return stage!="TIPO_NO_COMPATIBLE";

  byte previous[16];
  report("LECTURA_PREVIA", "Leyendo el bloque 4 antes de modificarlo.");
  if (!readBlock(previous)) return true;
  report("CONTENIDO_PREVIO", blockHex);

  byte desired[16] = {0};
  memcpy(desired,requestedId.c_str(),requestedId.length());
  expectedHex = hexBytes(desired, sizeof(desired));
  report("ESCRIBIENDO", "Sobrescribiendo solamente el bloque de datos 4.");
  if(uint32_t(millis()-armedAtMs)>=WRITE_TIMEOUT_MS){report("TIEMPO_AGOTADO","Terminó el plazo antes de escribir.");return false;}
  const auto status = rfid.MIFARE_Write(DATA_BLOCK, desired, sizeof(desired));
  if (status != MFRC522::STATUS_OK) {
    report("ERROR_ESCRITURA", "No se confirmo la escritura. Comprobar la tarjeta con el lector. " +
           String(rfid.GetStatusCodeName(status)));
    return false;
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
  return false;
}

void sendPage(){
 server.sendHeader("Cache-Control","no-store");server.send_P(200,"text/html; charset=utf-8",PAGE);
}

void setup(){
 beginHardware("RFID-ESP32");
 server.on("/",HTTP_GET,sendPage);
 server.on("/admin",HTTP_GET,[]{if(requireAdmin())sendPage();});
 server.on("/admin/",HTTP_GET,[]{if(requireAdmin())sendPage();});
 server.on("/api/write",HTTP_POST,prepareWrite);
 server.on("/api/status",HTTP_GET,sendStatus);
 registerCommonRoutes();server.onNotFound([]{reply(404,"Ruta inexistente.");});server.begin();
}
void finishOperation(const char* code,const char* text){
 setOperation(OperationState::IDLE);report(code,text);endAttempt();
}

void pollCard(bool connected){
 const bool manual=isWritePending()||isDiagnosticPending();
 const uint32_t duration=isWritePending()?WRITE_TIMEOUT_MS:DIAGNOSTIC_TIMEOUT_MS;
 if(manual&&!connected){finishOperation("ERROR_LECTOR","Operación interrumpida: el RC522 no responde.");return;}
 if(manual&&millis()-armedAtMs>=duration){finishOperation("TIEMPO_AGOTADO","Terminó la espera sin completar una operación.");return;}
 if(!connected){lastCardUid="";return;}
 if(millis()-lastPollMs<POLL_MS)return;
 lastPollMs=millis();
 const Selection selection=selectCard();
 // Detection itself takes time. Never start a late write after its deadline.
 if(manual&&millis()-armedAtMs>=duration){
  if(selection==Selection::SELECTED)releaseCard();
  finishOperation("TIEMPO_AGOTADO","Terminó la espera sin completar una operación.");return;
 }
 if(selection==Selection::ABSENT){
  if(!manual&&millis()-lastCardSeenMs>=400){lastCardUid="";suppressAttendance=false;}
  if(manual&&!waitingNotice&&millis()-armedAtMs>=5000){
   waitingNotice=true;
   if(stage=="ESPERANDO_TARJETA")report("ESPERANDO_TARJETA","RC522 conectado; no se recibió una respuesta ATQA de tarjeta.");
  }
  return;
 }
 if(selection==Selection::ERROR){
  if(!manual){const String code=stage,text=message;beginAttempt("LECTURA");report(code.c_str(),text);endAttempt();lastCardUid="";}
  return;
 }
 lastCardSeenMs=millis();
 const String uid=hexBytes(rfid.uid.uidByte,rfid.uid.size);
 if(!manual&&(suppressAttendance||uid==lastCardUid)){releaseCard();return;}
 lastCardUid=uid;
 if(!manual)beginAttempt("ASISTENCIA");
 if(isWritePending()){
  setOperation(OperationState::WRITING);
  const bool retry=writeSelectedCard();
  if(retry){releaseCard();setOperation(OperationState::WAIT_WRITE);return;}
 }else if(readSelectedCard()&&!manual){
  attendanceScan(cardId);trace+="[ASISTENCIA] "+attendanceMessage+"\n";
 }
 // One cleanup path, including authentication/read/write/verification failures.
 releaseCard();
 if(manual)setOperation(OperationState::IDLE);
 endAttempt();
}

void loop(){
 tickAttendance();server.handleClient();tickAttendance();
 pollCard(checkReader());
 tickAttendance();
}
