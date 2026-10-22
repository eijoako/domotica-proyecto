#pragma once
#include <Arduino.h>

class RfidReader {
 public:
  void begin();
  bool isConnected();
  bool readCardId(String& cardId);
  // Returns true only when a card was presented and a write attempt finished.
  bool writeCardId(const String& cardId, String& message);
};

