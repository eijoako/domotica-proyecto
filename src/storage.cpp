#include "storage.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

bool Storage::writeAtomically(const String& path, const String& body) {
  const String temp = path + ".tmp";
  File file = LittleFS.open(temp, "w");
  if (!file) return false;
  const bool complete = file.print(body) == body.length();
  file.close();
  if (!complete) { LittleFS.remove(temp); return false; }
  LittleFS.remove(path);
  return LittleFS.rename(temp, path);
}

bool Storage::ensureSeedData() {
  if (!LittleFS.exists("/data/students.json")) {
    if (!writeAtomically("/data/students.json", "[\n  {\"id\":\"STUDENT_017\",\"name\":\"Alumno de prueba\",\"cardId\":\"A7F3C912\",\"active\":true}\n]\n")) return false;
  }
  if (!LittleFS.exists("/data/teachers.json")) {
    if (!writeAtomically("/data/teachers.json", "[\n  {\"id\":\"TEACHER_01\",\"name\":\"Profesor Pérez\",\"cardId\":\"T1A2B3C4\",\"classId\":\"3EMS-ECO-A\",\"active\":true}\n]\n")) return false;
  }
  if (!LittleFS.exists("/data/classes.json"))
    return writeAtomically("/data/classes.json", "[\n  {\"id\":\"3EMS-ECO-A\",\"name\":\"3° EMS Economía A\"}\n]\n");
  return true;
}

bool Storage::begin() {
  if (!LittleFS.begin(true)) return false;
  if (!LittleFS.exists("/data")) LittleFS.mkdir("/data");
  if (!LittleFS.exists("/attendances")) LittleFS.mkdir("/attendances");
  return ensureSeedData() && loadEntities();
}

bool Storage::loadEntities() {
  auto loadPeople = [](const char* path, Person* target, size_t capacity, size_t& count, bool teacher) {
    File file = LittleFS.open(path, "r"); if (!file) return false;
    JsonDocument doc; const auto error = deserializeJson(doc, file); file.close();
    if (error || !doc.is<JsonArray>()) return false;
    count = 0;
    for (JsonObject row : doc.as<JsonArray>()) {
      if (count >= capacity) break;
      auto& person = target[count++];
      person.id = row["id"].as<String>(); person.name = row["name"].as<String>();
      person.cardId = row["cardId"].as<String>(); person.active = row["active"] | false;
      if (teacher) person.classId = row["classId"].as<String>();
    }
    return true;
  };
  if (!loadPeople("/data/students.json", students_, MAX_STUDENTS, studentCount_, false) ||
      !loadPeople("/data/teachers.json", teachers_, 20, teacherCount_, true)) return false;
  File file = LittleFS.open("/data/classes.json", "r"); if (!file) return false;
  JsonDocument doc; auto error = deserializeJson(doc, file); file.close();
  if (error || !doc.is<JsonArray>()) return false;
  classCount_ = 0;
  for (JsonObject row : doc.as<JsonArray>()) { if (classCount_ < 20) classes_[classCount_++] = {row["id"].as<String>(), row["name"].as<String>()}; }
  // A physical card may belong to at most one active person.
  for (size_t i = 0; i < studentCount_; ++i) {
    if (students_[i].cardId.isEmpty()) continue;
    for (size_t j = i + 1; j < studentCount_; ++j)
      if (students_[i].active && students_[j].active && students_[i].cardId == students_[j].cardId) return false;
    for (size_t j = 0; j < teacherCount_; ++j)
      if (students_[i].active && teachers_[j].active && students_[i].cardId == teachers_[j].cardId) return false;
  }
  for (size_t i = 0; i < teacherCount_; ++i)
    for (size_t j = i + 1; j < teacherCount_; ++j)
      if (!teachers_[i].cardId.isEmpty() && teachers_[i].active && teachers_[j].active && teachers_[i].cardId == teachers_[j].cardId) return false;
  return true;
}

const Person* Storage::findStudent(const String& cardId) const {
  for (size_t i = 0; i < studentCount_; ++i) if (students_[i].active && students_[i].cardId == cardId) return &students_[i];
  return nullptr;
}
const Person* Storage::findTeacher(const String& cardId) const {
  for (size_t i = 0; i < teacherCount_; ++i) if (teachers_[i].active && teachers_[i].cardId == cardId) return &teachers_[i];
  return nullptr;
}
const ClassInfo* Storage::findClass(const String& id) const {
  for (size_t i = 0; i < classCount_; ++i) if (classes_[i].id == id) return &classes_[i];
  return nullptr;
}

bool Storage::saveSession(const String& date, const String& id, const String& classId,
                          const String& teacherId, const String& openedAt,
                          const String& closedAt, const char* closeReason,
                          const Presence* present, size_t presentCount) {
  const String path = "/attendances/" + date + ".json";
  JsonDocument doc;
  if (LittleFS.exists(path)) {
    File existing = LittleFS.open(path, "r"); if (!existing || deserializeJson(doc, existing)) { if (existing) existing.close(); return false; } existing.close();
  } else { doc["date"] = date; doc["sessions"].to<JsonArray>(); }
  JsonArray sessions = doc["sessions"].as<JsonArray>();
  JsonObject session;
  // Replacing the same session makes an accepted scan durable immediately,
  // without creating a second record when the timeout finalizes it.
  for (JsonObject candidate : sessions) {
    if (candidate["id"].as<String>() == id) { session = candidate; session.clear(); break; }
  }
  if (session.isNull()) session = sessions.add<JsonObject>();
  session["id"] = id; session["classId"] = classId; session["teacherId"] = teacherId;
  session["openedAt"] = openedAt; session["closedAt"] = closedAt; session["closeReason"] = closeReason;
  JsonArray list = session["presentStudents"].to<JsonArray>();
  for (size_t i = 0; i < presentCount; ++i) { JsonObject item = list.add<JsonObject>(); item["studentId"] = present[i].studentId; item["scannedAt"] = present[i].scannedAt; }
  String output; serializeJsonPretty(doc, output);
  return writeAtomically(path, output);
}

