#include "rfid_reader.h"
#include "config.h"
#include <SPI.h>
#include <MFRC522.h>

namespace { MFRC522 reader(RFID_SS_PIN, RFID_RST_PIN); }
void RfidReader::begin() { SPI.begin(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, RFID_SS_PIN); reader.PCD_Init(); }

bool RfidReader::readCardId(String& cardId) {
  if (!reader.PICC_IsNewCardPresent() || !reader.PICC_ReadCardSerial()) return false;
  MFRC522::MIFARE_Key key; for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
  byte buffer[18]; byte size = sizeof(buffer); const byte block = 4; // Sector 1 data block; never manufacturer/trailer.
  const auto auth = reader.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(reader.uid));
  const auto status = auth == MFRC522::STATUS_OK ? reader.MIFARE_Read(block, buffer, &size) : MFRC522::STATUS_ERROR;
  reader.PICC_HaltA(); reader.PCD_StopCrypto1();
  if (status != MFRC522::STATUS_OK) return false;
  cardId = "";
  for (byte i = 0; i < 16; ++i) { char c = static_cast<char>(buffer[i]); if (c == '\0' || c == 0xFF) break; if (c >= 32 && c <= 126) cardId += c; }
  cardId.trim();
  return !cardId.isEmpty();
}

