#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <errno.h>
#include <lwip/sockets.h>

// ESP32 WebServer 3.3.8 normally retries socket writes synchronously. Keep the
// attendance timer serviced while a download waits for a slow receiver.
class CooperativeServer : public WebServer {
 public:
  using Service = void (*)();
  explicit CooperativeServer(int port, Service service)
      : WebServer(port), service_(service) {}

  // WebServer::sendContent is not virtual. Hide BOTH public overloads so all
  // explicit chunked responses use this framing rather than the base class's
  // final direct NetworkClient::write("\r\n"), which can block for seconds.
  void sendContent(const String& content) {
    sendContent(content.c_str(), content.length());
  }

  void sendContent(const char* content, size_t length) {
    if (!_chunked) {
      _currentClientWrite(content, length);
      return;
    }

    char header[20];
    const int headerLength =
        snprintf(header, sizeof(header), "%lx\r\n", static_cast<unsigned long>(length));
    if (headerLength <= 0 || static_cast<size_t>(headerLength) >= sizeof(header)) {
      abortTransfer();
      return;
    }
    if (_currentClientWrite(header, static_cast<size_t>(headerLength)) !=
        static_cast<size_t>(headerLength)) return;
    if (length && _currentClientWrite(content, length) != length) return;
    if (_currentClientWrite("\r\n", 2) != 2) return;
    if (length == 0) _chunked = false;
  }

 protected:
  size_t _currentClientWrite(const char* data, size_t length) override {
    return writeCooperatively(data, length);
  }

  size_t _currentClientWrite_P(PGM_P data, size_t length) override {
    // Flash is memory-mapped on ESP32; NetworkClient::write_P also delegates
    // directly to write(). The same bounded socket path is valid here.
    return writeCooperatively(data, length);
  }

 private:
  static constexpr uint32_t NO_PROGRESS_MS = 250;
  static constexpr uint32_t WRITE_BUDGET_MS = 2000;
  static constexpr size_t MAX_SEND_BYTES = 512;
  Service service_;

  void service() {
    if (service_) service_();
  }

  void abortTransfer() {
    client().stop();
    _chunked = false;
  }

  size_t writeCooperatively(const char* data, size_t length) {
    size_t sent = 0;
    const uint32_t started = millis();
    uint32_t lastProgress = started;
    while (sent < length) {
      service();
      const uint32_t now = millis();
      const int socket = client().fd();
      if (socket < 0 || now - started >= WRITE_BUDGET_MS ||
          now - lastProgress >= NO_PROGRESS_MS) {
        abortTransfer();
        return sent;
      }

      const size_t remaining = length - sent;
      const size_t batch = remaining > MAX_SEND_BYTES ? MAX_SEND_BYTES : remaining;
      const int result = ::send(socket, data + sent, batch, MSG_DONTWAIT);
      const int error = result < 0 ? errno : 0;
      if (result > 0) {
        sent += static_cast<size_t>(result);
        lastProgress = millis();
      }
      service();
      if (result > 0) continue;
      if (result == 0 ||
          (error != EAGAIN && error != EWOULDBLOCK && error != EINTR)) {
        abortTransfer();
        return sent;
      }
      // Give the Wi-Fi stack time without reentering HTTP handlers. millis()
      // subtraction above also keeps the budgets valid across counter wrap.
      yield();
    }
    return sent;
  }
};
