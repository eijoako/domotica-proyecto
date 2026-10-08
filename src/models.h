#pragma once
#include <Arduino.h>

struct Person {
  String id;
  String name;
  String cardId;
  bool active = false;
  String classId; // used only by teachers
};

struct ClassInfo { String id; String name; };
struct Presence { String studentId; String scannedAt; };

