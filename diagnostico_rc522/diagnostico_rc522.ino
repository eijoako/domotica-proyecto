#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>

// Optional read-only hardware isolation test. No Wi-Fi, web, attendance or FS.
MFRC522 rfid(5,22);

void result(const char* operation,MFRC522::StatusCode status){
  Serial.print(operation);Serial.print(": ");Serial.println(rfid.GetStatusCodeName(status));
}

void setup(){
  Serial.begin(115200);delay(200);
  SPI.begin(18,19,23,5);
  rfid.PCD_Init();delay(50);
  Serial.printf("PRUEBA MINIMA // SOLO LECTURA // Version: %02X\n",
                rfid.PCD_ReadRegister(MFRC522::VersionReg));
  Serial.println("Presentar una tarjeta. Retirarla antes de repetir.");
}

void loop(){
  delay(500);
  // Same setup as PICC_IsNewCardPresent, exposing the actual response status.
  rfid.PCD_WriteRegister(MFRC522::TxModeReg,0);
  rfid.PCD_WriteRegister(MFRC522::RxModeReg,0);
  rfid.PCD_WriteRegister(MFRC522::ModWidthReg,0x26);
  byte atqa[2]={};byte length=sizeof(atqa);
  auto status=rfid.PICC_RequestA(atqa,&length);result("REQA/ATQA (buffer 2)",status);
  if(status==MFRC522::STATUS_TIMEOUT){
    length=sizeof(atqa);status=rfid.PICC_WakeupA(atqa,&length);result("WUPA/ATQA (buffer 2)",status);
  }
  if(status!=MFRC522::STATUS_OK&&status!=MFRC522::STATUS_COLLISION){
    Serial.printf("FIFOLevelReg=%02X ErrorReg=%02X VersionReg=%02X\n",
      rfid.PCD_ReadRegister(MFRC522::FIFOLevelReg),rfid.PCD_ReadRegister(MFRC522::ErrorReg),
      rfid.PCD_ReadRegister(MFRC522::VersionReg));
    return;
  }
  status=rfid.PICC_Select(&rfid.uid);
  result("SELECT/anticolision",status);
  if(status!=MFRC522::STATUS_OK)return;
  Serial.print("UID:");
  if(rfid.uid.size==4||rfid.uid.size==7||rfid.uid.size==10){
    for(byte i=0;i<rfid.uid.size;++i)Serial.printf(" %02X",rfid.uid.uidByte[i]);
    Serial.println();
  }else{Serial.println(" longitud invalida");rfid.PICC_HaltA();rfid.PCD_StopCrypto1();return;}
  if(rfid.PICC_GetType(rfid.uid.sak)==MFRC522::PICC_TYPE_MIFARE_1K){
    MFRC522::MIFARE_Key key;
    for(byte& b:key.keyByte)b=0xFF;
    status=rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A,4,&key,&rfid.uid);
    result("AUTH bloque 4",status);
    if(status==MFRC522::STATUS_OK){
      byte data[18]={};byte size=sizeof(data);
      status=rfid.MIFARE_Read(4,data,&size);result("READ bloque 4 (buffer 18)",status);
      if(status==MFRC522::STATUS_OK&&size>=16&&size<=sizeof(data)){
        for(byte i=0;i<16;++i)Serial.printf("%02X ",data[i]);
        Serial.println();
      }
    }
  }else Serial.println("Tipo distinto de MIFARE Classic 1K; no se autentica.");
  rfid.PICC_HaltA();rfid.PCD_StopCrypto1();
}
