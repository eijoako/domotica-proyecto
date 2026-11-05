#include <iostream>
#include <sstream>
#include <stdexcept>
#include <LittleFS.h>
#include "../lector/lector.ino"

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
  operationState=OperationState::IDLE;attemptOpen=false;suppressAttendance=false;
  historyRevision=stateRevision=0;bootId="";
  FakeHTTP::reset();
  attemptHead=attemptCount=activeSlot=0;attemptSerial=0;
  for(auto& item:attempts)item=Attempt();
  waitingNotice = false; requestedId = ""; armedAtMs = 0;
  currentRecord={};storageReady=sessionValid=false;attendanceState=AttendanceState::CLOSED;
  sessionPath="";attendanceMessage="[CERRADO] Pase tarjeta de docente.";
  openedMs=lastCloseAttempt=clockAtMs=0;clockEpoch=0;
  server.args.clear(); server.handlers.clear();
  setup();
}

void configure(const std::string& name, int value) {
  if (name == "version") Fake::version = static_cast<byte>(value);
  else if(name=="uidSize") Fake::uidSize=static_cast<byte>(value);
  else if(name=="oversizedRead") Fake::oversizedRead=value;
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
  else if (name == "streamAdvance") FakeHTTP::streamAdvanceMs=value;
  else if (name == "networkStall") FakeHTTP::stallSocket=value;
  else if (name == "networkFail") FakeHTTP::failSocket=value;
  else if (name == "networkZero") FakeHTTP::zeroSocket=value;
  else if (name == "networkPartial") FakeHTTP::partialBytes=value;
  else if (name == "networkAdvance") FakeHTTP::sendAdvanceMs=value;
  else if (name == "networkInterrupts") FakeHTTP::interruptRemaining=value;
  else throw std::runtime_error("Unknown fake setting: " + name);
}

uint32_t historyFingerprint() {
 uint32_t hash=2166136261U;
 auto add=[&](const String& text){for(size_t i=0;i<text.length();++i){hash^=static_cast<byte>(text[i]);hash*=16777619U;}hash^=0xFF;hash*=16777619U;};
 add(String(attemptHead));add(String(attemptCount));add(String(activeSlot));add(String(attemptSerial));add(String(attemptOpen));
 for(const auto& a:attempts){add(String(a.number));add(String(a.at));add(a.kind);add(a.stage);add(a.message);add(a.trace);add(a.uid);add(a.type);add(a.card);add(a.block);add(a.expected);}
 return hash;
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
    bool checkHistory=false;
    uint32_t historyBefore=0;
    bool transportClosed=false,transportComplete=true;
    if (op == "RESET") resetProgram();
    else if(op=="LOADRECORD"){
      std::string encoded;line>>encoded;const auto bytes=unhex(encoded);
      FakeFS::files["/sessions/legacy.bin"]=std::vector<byte>(bytes.begin(),bytes.end());
    }
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
      checkHistory=method=="GET";
      if(checkHistory)historyBefore=historyFingerprint();
      const char* key=path=="/api/time"?"epoch":path=="/api/storage/init"?"confirmation":"personId";
      if (!id.empty()) server.args[key] = unhex(id).c_str();
      if (!confirmation.empty()) server.args["confirm"] = unhex(confirmation).c_str();
      server.dispatch(method == "GET" ? HTTP_GET : HTTP_POST, path);
      responseCode = server.code;
      responseBody = server.body;
      if(!FakeHTTP::wire.empty()||FakeHTTP::stopped){
       const auto decoded=FakeHTTP::decoded();responseBody=decoded.first.c_str();transportComplete=decoded.second;
      }
      transportClosed=FakeHTTP::stopped;
      cacheControl = server.headers["Cache-Control"];
    } else if (op != "STATUS") throw std::runtime_error("Unknown command: " + op);

    // This is harness introspection, not another physical HTTP send.
    FakeHTTP::bypassSocket=true;
    sendStatus();
    FakeHTTP::bypassSocket=false;
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
              << ",\"initializations\":" << Fake::initializations
              << ",\"earlyProbes\":" << Fake::earlyProbes
              << ",\"formats\":" << FakeFS::formats
              << ",\"historyUnchanged\":" << (!checkHistory||historyBefore==historyFingerprint()?"true":"false")
              << ",\"httpReentered\":" << (FakeHTTP::reentered?"true":"false")
              << ",\"streamChunks\":" << FakeHTTP::chunks
              << ",\"transportClosed\":" << (transportClosed?"true":"false")
              << ",\"transportComplete\":" << (transportComplete?"true":"false")
              << ",\"maxWriteBatch\":" << FakeHTTP::maxBatch
              << ",\"socketCalls\":" << FakeHTTP::socketCalls
              << ",\"nowMs\":" << Fake::now
              << ",\"safe\":" << (protectedBlocksUntouched ? "true" : "false")
              << "}" << std::endl;
  }
}
