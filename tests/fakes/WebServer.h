#pragma once
#include <Arduino.h>
#include <functional>
#include <map>
#define HTTP_GET 0
#define HTTP_POST 1
#define CONTENT_LENGTH_UNKNOWN size_t(-1)
class WebServer {
 public:
  std::map<std::string, String> args;
  std::map<std::pair<int, std::string>, std::function<void()>> handlers;
  std::function<void()> notFound;
  std::map<std::string, std::string> headers;
  int code = 0;
  String body;
  explicit WebServer(int) {}
  template<class F> void on(const char* path, int method, F handler) {
    handlers[{method, path}] = handler;
  }
  template<class F> void onNotFound(F handler) { notFound = handler; }
  void begin() {}
  void handleClient() {}
  void sendHeader(const char* key, const char* value) { headers[key] = value; }
  void send(int status, const char*, const String& data) { code = status; body = data; }
  void send_P(int status, const char*, const char* data) { code = status; body = data; }
  void setContentLength(size_t) {}
  void sendContent(const String& data) { body += data; }
  String arg(const char* name) { return args[name]; }
  void dispatch(int method, const std::string& path) {
    headers.clear();
    auto found = handlers.find({method, path});
    if (found == handlers.end()) notFound();
    else found->second();
  }
};
