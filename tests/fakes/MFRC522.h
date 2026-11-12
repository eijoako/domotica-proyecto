#pragma once
#include <Arduino.h>
#include <array>
#include <cstring>

// Stateful model of the API boundary, not an emulation of RF/electronics.
// Selection and authentication are required before reads or writes.
namespace Fake {
enum class Tag { IDLE, READY, ACTIVE, HALTED };
inline Tag tag = Tag::IDLE;
inline bool present = false, unsupported = false, authFail = false;
inline bool readFail = false, writeFail = false, verifyFail = false;
inline bool corrupt = false, selectFail = false, detectFail = false;
inline bool shortRead = false, removeOnWrite = false, lostAck = false;
inline bool crypto = false, safe = true;
inline byte version = 0x92;
inline byte uidSize = 4;
inline bool oversizedRead = false;
inline uint32_t initializedAt = 0;
inline unsigned earlyProbes = 0;
inline byte txMode=0,rxMode=0,modWidth=0x26;
inline bool antennaOn=true,wakeupTimeout=false;
inline unsigned fieldResets=0;
inline bool detectCollision=false,selectCollision=false,detectNoRoom=false,selectNoRoom=false,readNoRoom=false;
inline byte uid[4] = {1, 2, 3, 4};
inline int writes = 0, attempts = 0, reads = 0, initializations = 0;
inline uint32_t authDelay=0,readDelay=0;
inline uint32_t selectDelayMs=0;
inline int halts=0;
inline std::array<std::array<byte, 16>, 64> memory;
inline void reset() {
  now = 10000; tag = Tag::IDLE; present = false;
  unsupported = authFail = readFail = writeFail = verifyFail = false;
  corrupt = selectFail = detectFail = shortRead = removeOnWrite = lostAck = false;
  crypto = false; safe = true; version = 0x92;
  uidSize=4;oversizedRead=false;initializedAt=0;earlyProbes=0;
  txMode=rxMode=0;modWidth=0x26;antennaOn=true;wakeupTimeout=false;fieldResets=0;
  detectCollision=selectCollision=detectNoRoom=selectNoRoom=readNoRoom=false;
  writes = attempts = reads = initializations = 0;
  selectDelayMs=0;authDelay=readDelay=0;halts=0;
  for (auto& block : memory) block.fill(0xA5);
  memory[4].fill(0);
}
inline void presentCard(bool on) {
  if (on != present) { tag = Tag::IDLE; crypto = false; }
  present = on;
}
}
class MFRC522 {
 public:
  enum StatusCode { STATUS_OK, STATUS_ERROR, STATUS_TIMEOUT, STATUS_COLLISION, STATUS_NO_ROOM };
  enum PICC_Type { PICC_TYPE_MIFARE_1K, PICC_TYPE_OTHER };
  enum PCD_Register { VersionReg, TxModeReg, RxModeReg, ModWidthReg, FIFOLevelReg };
  static constexpr byte PICC_CMD_MF_AUTH_KEY_A = 0x60;
  struct MIFARE_Key { byte keyByte[6]; };
  struct Uid { byte size = 4; byte uidByte[10] = {}; byte sak = 0x08; } uid;
  MFRC522(int, int) {}
  void PCD_Init() {
    ++Fake::initializations;
    Fake::initializedAt=millis();
    Fake::crypto = false;
    Fake::tag = Fake::Tag::IDLE;
  }
  void PCD_AntennaOff() { ++Fake::fieldResets;Fake::antennaOn=false;Fake::crypto = false; Fake::tag = Fake::Tag::IDLE; }
  void PCD_AntennaOn() {Fake::antennaOn=true;}
  void PCD_WriteRegister(PCD_Register reg,byte value){
    if(reg==TxModeReg)Fake::txMode=value;
    else if(reg==RxModeReg)Fake::rxMode=value;
    else if(reg==ModWidthReg)Fake::modWidth=value;
  }
  bool radioReady(){return Fake::antennaOn&&!Fake::crypto&&Fake::txMode==0&&Fake::rxMode==0&&Fake::modWidth==0x26;}
  byte PCD_ReadRegister(PCD_Register reg) {
    if(reg==FIFOLevelReg)return Fake::detectNoRoom?3:0;
    if(millis()-Fake::initializedAt<50)++Fake::earlyProbes;
    return Fake::version;
  }
  StatusCode PICC_RequestA(byte*, byte*) {
    if(!radioReady())return STATUS_TIMEOUT;
    if (Fake::detectFail) return STATUS_ERROR;
    if(Fake::detectNoRoom)return STATUS_NO_ROOM;
    if (!Fake::present || Fake::tag != Fake::Tag::IDLE) return STATUS_TIMEOUT;
    Fake::tag = Fake::Tag::READY;
    if(Fake::detectCollision)return STATUS_COLLISION;
    return STATUS_OK;
  }
  StatusCode PICC_WakeupA(byte*, byte*) {
    if(Fake::detectFail)return STATUS_ERROR;
    if(Fake::detectNoRoom)return STATUS_NO_ROOM;
    if(Fake::wakeupTimeout||!radioReady())return STATUS_TIMEOUT;
    if (!Fake::present || (Fake::tag != Fake::Tag::IDLE && Fake::tag != Fake::Tag::HALTED))
      return STATUS_TIMEOUT;
    Fake::tag = Fake::Tag::READY;
    if(Fake::detectCollision)return STATUS_COLLISION;
    return STATUS_OK;
  }
  StatusCode PICC_Select(Uid* target) {
    Fake::now+=Fake::selectDelayMs;
    if(Fake::selectCollision)return STATUS_COLLISION;
    if(Fake::selectNoRoom)return STATUS_NO_ROOM;
    if (Fake::selectFail || Fake::tag != Fake::Tag::READY) return STATUS_ERROR;
    Fake::tag = Fake::Tag::ACTIVE;
    target->size=Fake::uidSize;
    memcpy(target->uidByte, Fake::uid, 4);
    return STATUS_OK;
  }
  PICC_Type PICC_GetType(byte) { return Fake::unsupported ? PICC_TYPE_OTHER : PICC_TYPE_MIFARE_1K; }
  const char* PICC_GetTypeName(PICC_Type type) {
    return type == PICC_TYPE_MIFARE_1K ? "MIFARE Classic 1K" : "Other";
  }
  static const char* GetStatusCodeName(StatusCode status) {
    return status == STATUS_OK ? "Success" : status == STATUS_TIMEOUT ? "Timeout" : status == STATUS_COLLISION ? "Collision detected." : status == STATUS_NO_ROOM ? "A buffer is not big enough." : "Simulated failure";
  }
  StatusCode PCD_Authenticate(byte command, byte block, MIFARE_Key* key, Uid*) {
    Fake::now+=Fake::authDelay;
    bool correctKey = true;
    for (byte value : key->keyByte) correctKey = correctKey && value == 0xFF;
    if (Fake::authFail || !Fake::present || Fake::tag != Fake::Tag::ACTIVE ||
        block / 4 != 1 || command != PICC_CMD_MF_AUTH_KEY_A || !correctKey) return STATUS_ERROR;
    Fake::crypto = true;
    return STATUS_OK;
  }
  StatusCode MIFARE_Read(byte block, byte* data, byte* size) {
    Fake::now+=Fake::readDelay;
    if(Fake::readNoRoom)return STATUS_NO_ROOM;
    if (block != 4 || !Fake::present || Fake::tag != Fake::Tag::ACTIVE ||
        !Fake::crypto || *size < 18 || Fake::readFail ||
        (Fake::verifyFail && Fake::writes > 0)) return STATUS_ERROR;
    memcpy(data, Fake::memory[4].data(), 16);
    *size = Fake::oversizedRead ? 255 : Fake::shortRead ? 15 : 18;
    ++Fake::reads;
    return STATUS_OK;
  }
  StatusCode MIFARE_Write(byte block, byte* data, byte size) {
    ++Fake::attempts;
    if (block != 4 || size != 16) { Fake::safe = false; return STATUS_ERROR; }
    if (Fake::removeOnWrite) { Fake::presentCard(false); return STATUS_TIMEOUT; }
    if (Fake::writeFail || !Fake::present || Fake::tag != Fake::Tag::ACTIVE || !Fake::crypto)
      return STATUS_ERROR;
    memcpy(Fake::memory[4].data(), data, 16);
    if (Fake::corrupt) Fake::memory[4][0] ^= 1;
    ++Fake::writes;
    return Fake::lostAck ? STATUS_TIMEOUT : STATUS_OK;
  }
  void PICC_HaltA() { ++Fake::halts;if (Fake::tag == Fake::Tag::ACTIVE) Fake::tag = Fake::Tag::HALTED; }
  void PCD_StopCrypto1() { Fake::crypto = false; }
};
