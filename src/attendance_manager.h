#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "storage.h"

class AttendanceManager {
 public:
  explicit AttendanceManager(Storage& storage) : storage_(storage) {}
  void tick();
  String scan(const String& cardId);
  void writeStatus(JsonDocument& doc) const;
  bool clockReady() const;
 private:
  bool finalize(const char* reason);
  String dateNow() const; String timeNow() const;
  Storage& storage_;
  bool active_ = false;
  String id_, date_, classId_, teacherId_, teacherName_, openedAt_;
  uint32_t openedMs_ = 0;
  Presence present_[MAX_PRESENT]; size_t presentCount_ = 0;
  String lastMessage_ = "CLASE CERRADA — Pase tarjeta de profesor";
  String lastClosedSummary_;
};

