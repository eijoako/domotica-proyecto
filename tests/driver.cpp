#include <iostream>
#include <sstream>
#include <stdexcept>
#ifdef TEST_WRITER
#include "../escritor/escritor.ino"
#else
#include "../lector/lector.ino"
#endif

std::string unhex(const std::string& hex) {
  if (hex == "-") return "";
  std::string result;
  for (size_t i = 0; i < hex.size(); i += 2)
    result += static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16));
  return result;
}

void resetProgram() {
  Fake::reset();
  stage = "INICIO"; message = ""; trace = ""; clearCard();
  readerConnected = false;
  readerVersion = 0;
  lastProbeMs = lastPollMs = lastDetectionErrorMs = 0;
#ifdef TEST_WRITER
  pending = false; waitingNotice = false; requestedId = ""; armedAtMs = 0;
#else
  lastCardUid = ""; lastCardSeenMs = 0;
#endif
  server.args.clear(); server.handlers.clear();
  setup();
}

void configure(const std::string& name, int value) {
  if (name == "version") Fake::version = static_cast<byte>(value);
  else if (name == "authFail") Fake::authFail = value;
  else if (name == "readFail") Fake::readFail = value;
  else if (name == "writeFail") Fake::writeFail = value;
  else if (name == "verifyFail") Fake::verifyFail = value;
  else if (name == "corrupt") Fake::corrupt = value;
  else if (name == "selectFail") Fake::selectFail = value;
  else if (name == "detectFail") Fake::detectFail = value;
  else if (name == "unsupported") Fake::unsupported = value;
  else if (name == "shortRead") Fake::shortRead = value;
  else if (name == "removeOnWrite") Fake::removeOnWrite = value;
  else if (name == "lostAck") Fake::lostAck = value;
  else throw std::runtime_error("Unknown fake setting: " + name);
}

int main() {
  resetProgram();
  std::string input;
  while (std::getline(std::cin, input)) {
    std::istringstream line(input);
    std::string op;
    line >> op;
    int responseCode = 0;
    String responseBody;
    std::string cacheControl;
    if (op == "RESET") resetProgram();
    else if (op == "CARD") {
      int present;
      std::string bytes;
      line >> present >> bytes;
      Fake::presentCard(present);
      if (!bytes.empty() && bytes != "-") {
        const auto data = unhex(bytes);
        if (data.size() != 16) throw std::runtime_error("Card block must have 16 bytes");
        memcpy(Fake::memory[4].data(), data.data(), 16);
      }
    } else if (op == "CONFIG") {
      std::string name; int value;
      line >> name >> value;
      configure(name, value);
    } else if (op == "TIME") {
      unsigned long long ms; line >> ms;
      Fake::now += static_cast<uint32_t>(ms);
      loop();
    } else if (op == "CLOCK") {
      unsigned long long ms; line >> ms;
      Fake::now = static_cast<uint32_t>(ms);
    } else if (op == "LOG") {
      int count; line >> count;
      for (int i = 0; i < count; ++i) report("TEST", "bounded log: quotes \" and newline\n");
    } else if (op == "HTTP") {
      std::string method, path, id, confirmation;
      line >> method >> path >> id >> confirmation;
      server.args.clear();
      if (!id.empty()) server.args["cardId"] = unhex(id).c_str();
      if (!confirmation.empty()) server.args["confirm"] = unhex(confirmation).c_str();
      server.dispatch(method == "GET" ? HTTP_GET : HTTP_POST, path);
      responseCode = server.code;
      responseBody = server.body;
      cacheControl = server.headers["Cache-Control"];
    } else if (op != "STATUS") throw std::runtime_error("Unknown command: " + op);

    sendStatus();
    bool protectedBlocksUntouched = Fake::safe;
    for (int b = 0; b < 64; ++b)
      if (b != 4) for (byte value : Fake::memory[b]) protectedBlocksUntouched &= value == 0xA5;
    std::cout << "{\"status\":" << server.body.c_str()
              << ",\"code\":" << responseCode
              << ",\"response\":" << jsonString(responseBody).c_str()
              << ",\"cache\":" << jsonString(cacheControl.c_str()).c_str()
              << ",\"block\":" << jsonString(hexBytes(Fake::memory[4].data(), 16)).c_str()
              << ",\"writes\":" << Fake::writes << ",\"attempts\":" << Fake::attempts
              << ",\"reads\":" << Fake::reads
              << ",\"safe\":" << (protectedBlocksUntouched ? "true" : "false")
              << "}" << std::endl;
  }
}
