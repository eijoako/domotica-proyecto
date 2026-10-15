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

bool Storage::validId(const String& value, size_t maximum) {
  return !value.isEmpty() && value.length() <= maximum;
}

bool Storage::cardAvailable(const String& cardId, const String& ownerId, bool teacher) const {
  if (cardId.isEmpty()) return true;
  for (size_t i = 0; i < studentCount_; ++i)
    if (students_[i].active && students_[i].cardId == cardId && (teacher || students_[i].id != ownerId)) return false;
  for (size_t i = 0; i < teacherCount_; ++i)
    if (teachers_[i].active && teachers_[i].cardId == cardId && (!teacher || teachers_[i].id != ownerId)) return false;
  return true;
}

bool Storage::saveStudents() {
  JsonDocument doc; JsonArray rows = doc.to<JsonArray>();
  for (size_t i = 0; i < studentCount_; ++i) { JsonObject row = rows.add<JsonObject>(); row["id"] = students_[i].id; row["name"] = students_[i].name; row["cardId"] = students_[i].cardId; row["active"] = students_[i].active; }
  String out; serializeJsonPretty(doc, out); return writeAtomically("/data/students.json", out);
}
bool Storage::saveTeachers() {
  JsonDocument doc; JsonArray rows = doc.to<JsonArray>();
  for (size_t i = 0; i < teacherCount_; ++i) { JsonObject row = rows.add<JsonObject>(); row["id"] = teachers_[i].id; row["name"] = teachers_[i].name; row["cardId"] = teachers_[i].cardId; row["classId"] = teachers_[i].classId; row["active"] = teachers_[i].active; }
  String out; serializeJsonPretty(doc, out); return writeAtomically("/data/teachers.json", out);
}
bool Storage::saveClasses() {
  JsonDocument doc; JsonArray rows = doc.to<JsonArray>();
  for (size_t i = 0; i < classCount_; ++i) { JsonObject row = rows.add<JsonObject>(); row["id"] = classes_[i].id; row["name"] = classes_[i].name; }
  String out; serializeJsonPretty(doc, out); return writeAtomically("/data/classes.json", out);
}

bool Storage::upsertStudent(const Person& person, String& error) {
  if (!validId(person.id, 24) || !validId(person.name, 64) || person.cardId.length() > 16) { error = "Datos de estudiante inválidos"; return false; }
  if (person.active && !cardAvailable(person.cardId, person.id, false)) { error = "La tarjeta ya está asignada a una persona activa"; return false; }
  for (size_t i = 0; i < studentCount_; ++i) if (students_[i].id == person.id) { Person old = students_[i]; students_[i] = person; if (saveStudents()) return true; students_[i] = old; error = "No se pudo guardar LittleFS"; return false; }
  if (studentCount_ >= MAX_STUDENTS) { error = "Límite de estudiantes alcanzado"; return false; }
  students_[studentCount_++] = person; if (saveStudents()) return true; --studentCount_; error = "No se pudo guardar LittleFS"; return false;
}

bool Storage::upsertTeacher(const Person& person, String& error) {
  if (!validId(person.id, 24) || !validId(person.name, 64) || !validId(person.classId, 24) || person.cardId.length() > 16) { error = "Datos de docente inválidos"; return false; }
  if (!findClass(person.classId)) { error = "La clase indicada no existe"; return false; }
  if (person.active && !cardAvailable(person.cardId, person.id, true)) { error = "La tarjeta ya está asignada a una persona activa"; return false; }
  for (size_t i = 0; i < teacherCount_; ++i) if (teachers_[i].id == person.id) { Person old = teachers_[i]; teachers_[i] = person; if (saveTeachers()) return true; teachers_[i] = old; error = "No se pudo guardar LittleFS"; return false; }
  if (teacherCount_ >= 20) { error = "Límite de docentes alcanzado"; return false; }
  teachers_[teacherCount_++] = person; if (saveTeachers()) return true; --teacherCount_; error = "No se pudo guardar LittleFS"; return false;
}

bool Storage::upsertClass(const ClassInfo& classInfo, String& error) {
  if (!validId(classInfo.id, 24) || !validId(classInfo.name, 64)) { error = "Datos de clase inválidos"; return false; }
  for (size_t i = 0; i < classCount_; ++i) if (classes_[i].id == classInfo.id) { ClassInfo old = classes_[i]; classes_[i] = classInfo; if (saveClasses()) return true; classes_[i] = old; error = "No se pudo guardar LittleFS"; return false; }
  if (classCount_ >= 20) { error = "Límite de clases alcanzado"; return false; }
  classes_[classCount_++] = classInfo; if (saveClasses()) return true; --classCount_; error = "No se pudo guardar LittleFS"; return false;
}

bool Storage::removeStudent(const String& id, String& error) {
  for (size_t i = 0; i < studentCount_; ++i) if (students_[i].id == id) { Person old = students_[i]; for (size_t j = i + 1; j < studentCount_; ++j) students_[j - 1] = students_[j]; --studentCount_; if (saveStudents()) return true; for (size_t j = studentCount_; j > i; --j) students_[j] = students_[j - 1]; students_[i] = old; ++studentCount_; error = "No se pudo guardar LittleFS"; return false; }
  error = "Estudiante no encontrado"; return false;
}
bool Storage::removeTeacher(const String& id, String& error) {
  for (size_t i = 0; i < teacherCount_; ++i) if (teachers_[i].id == id) { Person old = teachers_[i]; for (size_t j = i + 1; j < teacherCount_; ++j) teachers_[j - 1] = teachers_[j]; --teacherCount_; if (saveTeachers()) return true; for (size_t j = teacherCount_; j > i; --j) teachers_[j] = teachers_[j - 1]; teachers_[i] = old; ++teacherCount_; error = "No se pudo guardar LittleFS"; return false; }
  error = "Docente no encontrado"; return false;
}
bool Storage::removeClass(const String& id, String& error) {
  for (size_t i = 0; i < teacherCount_; ++i) if (teachers_[i].classId == id) { error = "No se puede borrar una clase asignada a un docente"; return false; }
  for (size_t i = 0; i < classCount_; ++i) if (classes_[i].id == id) { ClassInfo old = classes_[i]; for (size_t j = i + 1; j < classCount_; ++j) classes_[j - 1] = classes_[j]; --classCount_; if (saveClasses()) return true; for (size_t j = classCount_; j > i; --j) classes_[j] = classes_[j - 1]; classes_[i] = old; ++classCount_; error = "No se pudo guardar LittleFS"; return false; }
  error = "Clase no encontrada"; return false;
}

void Storage::writeAdminData(JsonDocument& doc) const {
  JsonArray students = doc["students"].to<JsonArray>();
  for (size_t i = 0; i < studentCount_; ++i) { JsonObject row = students.add<JsonObject>(); row["id"] = students_[i].id; row["name"] = students_[i].name; row["cardId"] = students_[i].cardId; row["active"] = students_[i].active; }
  JsonArray teachers = doc["teachers"].to<JsonArray>();
  for (size_t i = 0; i < teacherCount_; ++i) { JsonObject row = teachers.add<JsonObject>(); row["id"] = teachers_[i].id; row["name"] = teachers_[i].name; row["cardId"] = teachers_[i].cardId; row["classId"] = teachers_[i].classId; row["active"] = teachers_[i].active; }
  JsonArray classes = doc["classes"].to<JsonArray>();
  for (size_t i = 0; i < classCount_; ++i) { JsonObject row = classes.add<JsonObject>(); row["id"] = classes_[i].id; row["name"] = classes_[i].name; }
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

