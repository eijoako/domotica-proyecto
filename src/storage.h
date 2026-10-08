#pragma once
#include <Arduino.h>
#include "models.h"

class Storage {
 public:
  bool begin();
  bool loadEntities();
  const Person* findStudent(const String& cardId) const;
  const Person* findTeacher(const String& cardId) const;
  const ClassInfo* findClass(const String& id) const;
  bool saveSession(const String& date, const String& id, const String& classId,
                   const String& teacherId, const String& openedAt,
                   const String& closedAt, const char* closeReason,
                   const Presence* present, size_t presentCount);
  size_t studentCount() const { return studentCount_; }
  size_t teacherCount() const { return teacherCount_; }

 private:
  bool ensureSeedData();
  bool writeAtomically(const String& path, const String& body);
  Person students_[MAX_STUDENTS];
  Person teachers_[20];
  ClassInfo classes_[20];
  size_t studentCount_ = 0, teacherCount_ = 0, classCount_ = 0;
};

