#include "rfid_reader.h"
#include "config.h"
#include <SPI.h>
#include <MFRC522.h>
#include <string.h>

namespace { MFRC522 reader(RFID_SS_PIN, RFID_RST_PIN); }
void RfidReader::begin() { SPI.begin(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, RFID_SS_PIN); reader.PCD_Init(); }

bool RfidReader::isConnected() {
  const byte version = reader.PCD_ReadRegister(MFRC522::VersionReg);
  return version != 0x00 && version != 0xFF;
}

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
  // REQA finds a new card; WUPA also finds one halted by an earlier read.
  byte answer[2]; byte answerSize = sizeof(answer);
  auto status = reader.PICC_RequestA(answer, &answerSize);
  if (status != MFRC522::STATUS_OK) {
    answerSize = sizeof(answer);
    status = reader.PICC_WakeupA(answer, &answerSize);
  }
  if (status == MFRC522::STATUS_TIMEOUT) return false; // No card in the field.
  if (status != MFRC522::STATUS_OK) {
    message = String("Error de detección: ") + reader.GetStatusCodeName(status);
    reader.PCD_StopCrypto1();
    return true;
  }
  if (!reader.PICC_ReadCardSerial()) {
    message = "Tarjeta detectada, pero no se pudo seleccionar; acerquela sola al lector";
    reader.PCD_StopCrypto1();
    return true;
  }
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
  if (auth != MFRC522::STATUS_OK) {
    message = "No se pudo autenticar el bloque 4. La tarjeta puede usar otra clave: " + String(reader.GetStatusCodeName(auth));
    reader.PICC_HaltA(); reader.PCD_StopCrypto1();
    return true;
  }
  status = reader.MIFARE_Write(block, buffer, 16);
  if (status != MFRC522::STATUS_OK) {
    message = "No se pudo escribir el bloque 4: " + String(reader.GetStatusCodeName(status));
    reader.PICC_HaltA(); reader.PCD_StopCrypto1();
    return true;
  }
  byte verify[18]; byte verifySize = sizeof(verify);
  const auto readStatus = reader.MIFARE_Read(block, verify, &verifySize);
  const bool verified = readStatus == MFRC522::STATUS_OK && memcmp(buffer, verify, sizeof(buffer)) == 0;
  reader.PICC_HaltA(); reader.PCD_StopCrypto1();
  message = verified ? "Tarjeta configurada y verificada; retirala y acercala de nuevo para usarla" :
      "Se escribio, pero no se pudo verificar el bloque 4; no usar la tarjeta hasta comprobarla";
  return true;
}

