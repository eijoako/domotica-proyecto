#include <iostream>
#include <sstream>
#include <stdexcept>
#include <LittleFS.h>
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

void resetProgram(bool erase=true) {
  if(erase)FakeFS::reset();
  Fake::reset();
  stage = "INICIO"; message = ""; trace = ""; clearCard();
  readerConnected = false;
  readerVersion = 0;
  lastProbeMs = lastPollMs = lastDetectionErrorMs = 0;
  lastCardUid="";lastCardSeenMs=0;
  diagnosticPending=false;writing=false;attemptOpen=false;suppressAttendance=false;
  attemptHead=attemptCount=activeSlot=0;attemptSerial=0;
  for(auto& item:attempts)item=Attempt();
#ifdef TEST_WRITER
  pending = false; waitingNotice = false; requestedId = ""; armedAtMs = 0;
#else
  currentRecord={};storageReady=sessionValid=sessionActive=closePending=false;
  sessionPath="";attendanceMessage="[CERRADO] Pase tarjeta de docente.";
  openedMs=lastCloseAttempt=clockAtMs=0;clockEpoch=0;
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
  else if (name == "fsWrite") FakeFS::failWrite=value;
  else if (name == "fsRename") FakeFS::failRename=value;
  else if (name == "fsMount") FakeFS::mounted=value;
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
    else if(op=="REBOOT")resetProgram(false);
    else if(op=="CORRUPT"){
      for(auto& item:FakeFS::files)if(item.first.find(".bin")!=std::string::npos&&!item.second.empty()){item.second[0]^=1;break;}
    }
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
    } else if(op=="ATTEMPTS"){
      int count;line>>count;for(int i=0;i<count;++i){beginAttempt("TEST");report("TEST","historical error preserved");endAttempt();}
    } else if (op == "HTTP") {
      std::string method, path, id, confirmation;
      line >> method >> path >> id >> confirmation;
      server.args.clear();
      const char* key=path=="/api/time"?"epoch":path=="/api/storage/init"?"confirmation":"personId";
      if (!id.empty()) server.args[key] = unhex(id).c_str();
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
              << ",\"formats\":" << FakeFS::formats
              << ",\"safe\":" << (protectedBlocksUntouched ? "true" : "false")
              << "}" << std::endl;
  }
}
