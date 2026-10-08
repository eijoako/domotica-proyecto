#include "attendance_manager.h"
#include "config.h"
#include <time.h>

bool AttendanceManager::clockReady() const { return time(nullptr) > 1704067200; } // 2024-01-01 UTC
String AttendanceManager::dateNow() const { time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm); char b[11]; strftime(b, sizeof(b), "%Y-%m-%d", &tm); return String(b); }
String AttendanceManager::timeNow() const { time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm); char b[9]; strftime(b, sizeof(b), "%H:%M:%S", &tm); return String(b); }

void AttendanceManager::tick() { if (active_ && millis() - openedMs_ >= SESSION_DURATION_MS) finalize("TIMEOUT"); }

String AttendanceManager::scan(const String& cardId) {
  tick();
  if (!active_) {
    if (!clockReady()) return lastMessage_ = "Configure la hora desde el panel antes de abrir asistencia";
    const Person* teacher = storage_.findTeacher(cardId);
    if (!teacher) return lastMessage_ = "Solo una tarjeta de profesor puede abrir una clase";
    const ClassInfo* group = storage_.findClass(teacher->classId);
    if (!group) return lastMessage_ = "Profesor sin clase válida configurada";
    active_ = true; teacherId_ = teacher->id; teacherName_ = teacher->name; classId_ = group->id;
    date_ = dateNow(); openedAt_ = timeNow(); openedMs_ = millis(); presentCount_ = 0;
    id_ = date_ + "_" + openedAt_; id_.replace(":", "") ; id_ += "_" + classId_;
    if (!storage_.saveSession(date_, id_, classId_, teacherId_, openedAt_, "", "OPEN", present_, presentCount_)) {
      active_ = false;
      return lastMessage_ = "ERROR: no se pudo crear la asistencia";
    }
    return lastMessage_ = "Clase abierta: " + group->name;
  }
  if (storage_.findTeacher(cardId)) return lastMessage_ = "La asistencia ya está abierta";
  const Person* student = storage_.findStudent(cardId);
  if (!student) return lastMessage_ = "Tarjeta no registrada";
  for (size_t i = 0; i < presentCount_; ++i) if (present_[i].studentId == student->id) return lastMessage_ = "Ya estaba registrado";
  if (presentCount_ >= MAX_PRESENT) return lastMessage_ = "Límite de asistentes alcanzado";
  present_[presentCount_++] = {student->id, timeNow()};
  if (!storage_.saveSession(date_, id_, classId_, teacherId_, openedAt_, "", "OPEN", present_, presentCount_)) {
    --presentCount_;
    return lastMessage_ = "ERROR: no se pudo guardar el registro";
  }
  return lastMessage_ = "Alumno registrado: " + student->name;
}

bool AttendanceManager::finalize(const char* reason) {
  if (!active_) return true;
  const String closedAt = timeNow();
  const bool stored = storage_.saveSession(date_, id_, classId_, teacherId_, openedAt_, closedAt, reason, present_, presentCount_);
  active_ = false;
  lastClosedSummary_ = String(presentCount_) + " presentes registrados" + (stored ? "" : " (ERROR al guardar)");
  lastMessage_ = stored ? "ASISTENCIA CERRADA AUTOMÁTICAMENTE" : "ERROR: no se pudo guardar la asistencia";
  return stored;
}

void AttendanceManager::writeStatus(JsonDocument& doc) const {
  doc["active"] = active_; doc["clockReady"] = clockReady(); doc["message"] = lastMessage_;
  doc["presentCount"] = active_ ? presentCount_ : 0;
  doc["lastClosedSummary"] = lastClosedSummary_;
  if (active_) {
    doc["classId"] = classId_; doc["teacher"] = teacherName_;
    const uint32_t elapsed = millis() - openedMs_;
    doc["remainingSeconds"] = elapsed >= SESSION_DURATION_MS ? 0 : (SESSION_DURATION_MS - elapsed) / 1000;
  }
}

