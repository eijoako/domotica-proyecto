#pragma once
#include <Arduino.h>
#define WIFI_AP 2
struct FakeIP { String toString() { return "192.168.4.1"; } };
struct WiFiFake {
  void mode(int) {}
  bool softAP(const char*, const char*) { return true; }
  FakeIP softAPIP() { return {}; }
};
inline WiFiFake WiFi;
