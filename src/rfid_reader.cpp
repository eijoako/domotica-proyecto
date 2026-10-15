#include "rfid_reader.h"
#include "config.h"
#include <SPI.h>
#include <MFRC522.h>
#include <string.h>

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
  for (byte i = 0; i < 16; ++i) {
    const char c = static_cast<char>(buffer[i]);
    if (c == '\0' || static_cast<byte>(c) == 0xFF) break;
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')) {
      cardId = "";
      break;
    }
    cardId += c;
  }
  return true; // An empty/unassigned card must still produce a visible rejection.
}

bool RfidReader::writeCardId(const String& cardId, String& message) {
  // WUPA also selects a card previously halted by readCardId(). This matters
  // when /admin arms a write while that same card remains on the reader.
  byte answer[2]; byte answerSize = sizeof(answer);
  if (reader.PICC_WakeupA(answer, &answerSize) != MFRC522::STATUS_OK || !reader.PICC_ReadCardSerial()) return false;
  if (cardId.isEmpty() || cardId.length() > 16) { message = "Card ID inválido (máximo 16 caracteres)"; reader.PICC_HaltA(); reader.PCD_StopCrypto1(); return true; }
  if (reader.PICC_GetType(reader.uid.sak) != MFRC522::PICC_TYPE_MIFARE_1K) {
    message = "Se necesita una tarjeta MIFARE Classic 1K";
    reader.PICC_HaltA(); reader.PCD_StopCrypto1(); return true;
  }
  MFRC522::MIFARE_Key key; for (byte i = 0; i < 6; ++i) key.keyByte[i] = 0xFF;
  const byte block = 4; // Permitted data block in sector 1.
  byte buffer[16] = {0};
  for (size_t i = 0; i < cardId.length(); ++i) buffer[i] = static_cast<byte>(cardId[i]);
  const auto auth = reader.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(reader.uid));
  const auto status = auth == MFRC522::STATUS_OK ? reader.MIFARE_Write(block, buffer, 16) : MFRC522::STATUS_ERROR;
  byte verify[18]; byte verifySize = sizeof(verify);
  const bool verified = status == MFRC522::STATUS_OK &&
      reader.MIFARE_Read(block, verify, &verifySize) == MFRC522::STATUS_OK &&
      memcmp(buffer, verify, sizeof(buffer)) == 0;
  reader.PICC_HaltA(); reader.PCD_StopCrypto1();
  message = verified ? "Tarjeta configurada y verificada" : "No se pudo escribir o verificar el bloque 4";
  return true;
}

