#pragma once
#include <Arduino.h>

class RfidReader {
 public:
  void begin();
  bool readCardId(String& cardId);
};

