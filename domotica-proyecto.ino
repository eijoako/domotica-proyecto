#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <sys/time.h>
#include <stdlib.h>
#include "src/config.h"
#include "src/storage.h"
#include "src/rfid_reader.h"
#include "src/attendance_manager.h"
#include "src/web_ui.h"

WebServer server(80);
Storage storage;
RfidReader rfid;
AttendanceManager attendance(storage);
String pendingWriteCardId;
String adminMessage;
uint32_t pendingWriteStartedMs = 0;

void sendStatus() {
  JsonDocument doc;
  attendance.writeStatus(doc);
  String body;
  serializeJson(doc, body);
  server.send(200, "application/json", body);
}

void setClock() {
  if (!server.hasArg("epoch")) {
    server.send(400, "application/json", "{\"error\":\"epoch required\"}");
    return;
  }
  const String value = server.arg("epoch");
  char* end;
  const long long epoch = strtoll(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0' || epoch < 1704067200LL || epoch > 4102444800LL) {
    server.send(400, "application/json", "{\"error\":\"fecha inválida\"}");
    return;
  }
  timeval tv = {static_cast<time_t>(epoch), 0};
  settimeofday(&tv, nullptr);
  server.send(204);
}

void sendJson(bool ok, const String& message) {
  JsonDocument doc;
  doc[ok ? "message" : "error"] = message;
  String body; serializeJson(doc, body);
  server.send(ok ? 200 : 400, "application/json", body);
}

Person personFromRequest(bool teacher) {
  Person person;
  person.id = server.arg("id"); person.name = server.arg("name"); person.cardId = server.arg("cardId");
  person.active = server.arg("active") == "true";
  if (teacher) person.classId = server.arg("classId");
  return person;
}

void saveStudent() { String error; const bool ok = storage.upsertStudent(personFromRequest(false), error); sendJson(ok, ok ? "Estudiante guardado" : error); }
void saveTeacher() { String error; const bool ok = storage.upsertTeacher(personFromRequest(true), error); sendJson(ok, ok ? "Docente guardado" : error); }
void saveClass() { String error; ClassInfo info = {server.arg("id"), server.arg("name")}; const bool ok = storage.upsertClass(info, error); sendJson(ok, ok ? "Clase guardada" : error); }

void deleteEntity() {
  const String type = server.arg("type"), id = server.arg("id"); String error; bool ok = false;
  if (type == "student") ok = storage.removeStudent(id, error);
  else if (type == "teacher") ok = storage.removeTeacher(id, error);
  else if (type == "class") ok = storage.removeClass(id, error);
  else error = "Tipo inválido";
  sendJson(ok, ok ? "Registro borrado" : error);
}

void armCardWrite() {
  const String cardId = server.arg("cardId");
  if (cardId.isEmpty() || cardId.length() > 16) { sendJson(false, "Card ID inválido"); return; }
  if (!storage.findStudent(cardId) && !storage.findTeacher(cardId)) { sendJson(false, "Primero guarde una persona activa con este Card ID"); return; }
  if (!rfid.isConnected()) { sendJson(false, "El RC522 no responde; revisar alimentación a 3.3 V y cables SPI"); return; }
  pendingWriteCardId = cardId;
  pendingWriteStartedMs = millis();
  adminMessage = "Acerque una tarjeta para escribir " + cardId;
  sendJson(true, adminMessage);
}

void sendAdminData() {
  JsonDocument doc;
  storage.writeAdminData(doc);
  doc["pendingCardId"] = pendingWriteCardId;
  doc["adminMessage"] = rfid.isConnected() ? adminMessage : "El RC522 no responde; revisar alimentación a 3.3 V y cables SPI";
  String body; serializeJson(doc, body);
  server.send(200, "application/json", body);
}

void setup() {
  Serial.begin(115200);
  setenv("TZ", "UYT3", 1); // Uruguay, UTC-3 (POSIX TZ signs are reversed).
  tzset();

  const bool storageOk = storage.begin();
  rfid.begin();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  server.on("/", HTTP_GET, [] {
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/admin", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", ADMIN_HTML); });
  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/api/time", HTTP_POST, setClock);
  server.on("/api/admin/data", HTTP_GET, sendAdminData);
  server.on("/api/admin/student", HTTP_POST, saveStudent);
  server.on("/api/admin/teacher", HTTP_POST, saveTeacher);
  server.on("/api/admin/class", HTTP_POST, saveClass);
  server.on("/api/admin/delete", HTTP_POST, deleteEntity);
  server.on("/api/admin/write-card", HTTP_POST, armCardWrite);
  server.onNotFound([] {
    server.send(404, "application/json", "{\"error\":\"not found\"}");
  });
  server.begin();

  Serial.printf("AP: %s | IP: %s | storage: %s\n", AP_SSID,
                WiFi.softAPIP().toString().c_str(), storageOk ? "OK" : "ERROR");
}

void loop() {
  server.handleClient();
  attendance.tick();

  if (!pendingWriteCardId.isEmpty()) {
    if (millis() - pendingWriteStartedMs >= 60000) {
      pendingWriteCardId = "";
      adminMessage = "Escritura cancelada: el RC522 no detectó ninguna tarjeta en 60 segundos. Revisá el tipo de tarjeta, la posición y los cables";
      return;
    }
    String message;
    if (rfid.writeCardId(pendingWriteCardId, message)) {
      adminMessage = message;
      Serial.println(message);
      pendingWriteCardId = "";
    } else if (millis() - pendingWriteStartedMs >= 5000 && adminMessage.startsWith("Acerque una tarjeta")) {
      adminMessage = "El RC522 todavía no detecta la tarjeta. Apoyala sola sobre el lector; si persiste, revisá que sea de 13,56 MHz y los cables";
      Serial.println(adminMessage);
    }
    return;
  }

  String cardId;
  if (rfid.readCardId(cardId)) {
    Serial.println(attendance.scan(cardId));
  }
}
