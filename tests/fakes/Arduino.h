#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <type_traits>
using byte = uint8_t;
#define HEX 16
#define PROGMEM
namespace Fake { inline uint32_t now = 10000; }
inline uint32_t millis() { return Fake::now; }
inline void delay(unsigned long n) { Fake::now += n; }
class String {
  std::string value;
 public:
  String() = default;
  String(const char* s) : value(s ? s : "") {}
  String(const std::string& s) : value(s) {}
  template<class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  String(T n, int base = 10) {
    std::ostringstream out;
    if (base == 16) out << std::hex;
    out << static_cast<unsigned long long>(n);
    value = out.str();
  }
  bool isEmpty() const { return value.empty(); }
  size_t length() const { return value.length(); }
  const char* c_str() const { return value.c_str(); }
  char operator[](size_t i) const { return value.at(i); }
  String& operator+=(const String& other) { value += other.value; return *this; }
  String& operator+=(char c) { value += c; return *this; }
  friend String operator+(const String& a, const String& b) { return a.value + b.value; }
  friend bool operator==(const String& a, const String& b) { return a.value == b.value; }
  friend bool operator!=(const String& a, const String& b) { return !(a == b); }
  void remove(size_t start, size_t count = std::string::npos) { value.erase(start, count); }
  int indexOf(char c) const {
    const auto pos = value.find(c);
    return pos == value.npos ? -1 : static_cast<int>(pos);
  }
  void toUpperCase() {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::toupper(c); });
  }
};
struct SerialFake {
  void begin(int) {}
  void println() {}
  void println(const String&) {}
  void print(const String&) {}
  template<class... T> void printf(const char*, T...) {}
};
inline SerialFake Serial;
