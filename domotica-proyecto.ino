#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <sys/time.h>
#include "src/config.h"
#include "src/storage.h"
#include "src/rfid_reader.h"
#include "src/attendance_manager.h"
#include "src/web_ui.h"

WebServer server(80);
Storage storage;
RfidReader rfid;
AttendanceManager attendance(storage);

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
  timeval tv = {static_cast<time_t>(server.arg("epoch").toInt()), 0};
  settimeofday(&tv, nullptr);
  server.send(204);
}

void setup() {
  Serial.begin(115200);
  setenv("TZ", "UTC0", 1);
  tzset();

  const bool storageOk = storage.begin();
  rfid.begin();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  server.on("/", HTTP_GET, [] {
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/api/time", HTTP_POST, setClock);
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

  String cardId;
  if (rfid.readCardId(cardId)) {
    Serial.println(attendance.scan(cardId));
  }
}
