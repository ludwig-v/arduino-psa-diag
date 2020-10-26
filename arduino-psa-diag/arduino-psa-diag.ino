/////////////////////
//    Libraries    //
/////////////////////

#include <EEPROM.h>
#include <SPI.h>
#include <mcp2515.h> // https://github.com/autowp/arduino-mcp2515 + https://github.com/watterott/Arduino-Libs/tree/master/digitalWriteFast

/////////////////////
//  Configuration  //
/////////////////////

#define CS_PIN_CAN0 10
#define SERIAL_SPEED 115200
#define CAN_SPEED CAN_500KBPS // Diagnostic CAN bus - High Speed
#define CAN_FREQ MCP_16MHZ // Switch to 8MHZ if you have a 8Mhz module

////////////////////
// Initialization //
////////////////////

MCP2515 CAN0(CS_PIN_CAN0); // CAN-BUS Shield

////////////////////
//   Variables    //
////////////////////

// My variables
bool Dump = false; // Passive dump mode, dump Diagbox frames

// CAN-BUS Messages
struct can_frame canMsgRcv;
char tmp[4];

int CAN_EMIT_ID = 0x752; // BSI
int CAN_RECV_ID = 0x652; // BSI

int additionalFrameID;
int additionalFrameSize;
byte DiagSess = 0x03;

char receiveDiagFrameData[512];
int receiveDiagFrameSize;
int receiveDiagDataPos = 0;

bool waitingReplySerialCMD = false;
static unsigned long lastCMDSent = 0;
bool waitingUnlock = false;
unsigned short UnlockKey = 0x0000;
byte UnlockService = 0x00;
char * UnlockCMD = (char * ) malloc(5);
char * SketchVersion = (char * ) malloc(4);

void setup() {
  Serial.begin(SERIAL_SPEED);

  CAN0.reset();
  CAN0.setBitrate(CAN_SPEED, CAN_FREQ);
  while (CAN0.setNormalMode() != MCP2515::ERROR_OK) {
    delay(100);
  }

  strcpy(SketchVersion, "1.0");
}

/* https://github.com/ludwig-v/psa-seedkey-algorithm */
long transform(byte data_msb, byte data_lsb, byte sec[]) {
  long data = (data_msb << 8) | data_lsb;
  long result = ((data % sec[0]) * sec[2]) - ((data / sec[0]) * sec[1]);
  if (result < 0)
    result += (sec[0] * sec[2]) + sec[1];
  return result;
}

unsigned long compute_response(unsigned short pin, unsigned long chg) {
  byte sec_1[3] = {0xB2,0x3F,0xAA};
  byte sec_2[3] = {0xB1,0x02,0xAB};

  long res_msb = transform((pin >> 8), (pin & 0xFF), sec_1) | transform(((chg >> 24) & 0xFF), (chg & 0xFF), sec_2);
  long res_lsb = transform(((chg >> 16) & 0xFF), ((chg >> 8) & 0xFF), sec_1) | transform((res_msb >> 8), (res_msb & 0xFF), sec_2);
  return (res_msb << 16) | res_lsb;
}

int int_pow(int base, int exp) {
  int result = 1;
  while (exp) {
    if (exp % 2)
      result *= base;
    exp /= 2;
    base *= base;
  }
  return result;
}

int ahex2int(char a, char b) {
  a = (a <= '9') ? a - '0' : (a & 0x7) + 9;
  b = (b <= '9') ? b - '0' : (b & 0x7) + 9;

  return (a << 4) + b;
}

void receiveDiagMultiFrame(can_frame frame) {
  int i = 0;
  char tmp[4];

  memset(receiveDiagFrameData, '\0', 512); // Reset

  for (i = 2; i < frame.can_dlc; i++) {
    snprintf(tmp, 3, "%02X", frame.data[i]);
    receiveDiagFrameData[strlen(receiveDiagFrameData)] = tmp[0];
    receiveDiagFrameData[strlen(receiveDiagFrameData)] = tmp[1];
  }
}

void receiveAdditionalDiagFrame(can_frame frame) {
  int i = 0;
  char tmp[4];
  int frameOrder = 0;

  if (frame.data[0] == 0x20) {
    if (receiveDiagDataPos == 0) {
      receiveDiagDataPos += 15; // 21 > 2F
    } else {
      receiveDiagDataPos += 16; // 20 > 2F
    }
  }
  frameOrder = frame.data[0] - 0x21 + receiveDiagDataPos;

  for (i = 1; i < frame.can_dlc; i++) {
    snprintf(tmp, 3, "%02X", frame.data[i]);

    // 6 bytes already received + 7 bytes max per frame
    receiveDiagFrameData[(6 * 2) + (frameOrder * 7 * 2) + ((i - 1) * 2)] = tmp[0];
    receiveDiagFrameData[(6 * 2) + (frameOrder * 7 * 2) + ((i - 1) * 2) + 1] = tmp[1];
  }

  if (strlen(receiveDiagFrameData) == (receiveDiagFrameSize * 2)) { // Data complete
    if (Dump) {
      snprintf(tmp, 4, "%02X", CAN_RECV_ID);
      Serial.print(tmp);
      Serial.print(":");
    }
    Serial.println(receiveDiagFrameData);
  }
}

void sendAdditionalDiagFrames(char * data, int pos) {
  int i = 0;
  int frameLen = 0;
  byte tmpFrame[8] = {0,0,0,0,0,0,0,0};
  struct can_frame diagFrame;
  int dataSize = strlen(data);

  for (i = pos; i < dataSize; i += 2) {
    tmpFrame[frameLen] = ahex2int(data[i], data[(i + 1)]);
    frameLen++;

    if (frameLen > 0 && (frameLen % 8) == 0) { // Multi-frames
      diagFrame.data[0] = additionalFrameID;
      diagFrame.data[1] = tmpFrame[0];
      diagFrame.data[2] = tmpFrame[1];
      diagFrame.data[3] = tmpFrame[2];
      diagFrame.data[4] = tmpFrame[3];
      diagFrame.data[5] = tmpFrame[4];
      diagFrame.data[6] = tmpFrame[5];
      diagFrame.data[7] = tmpFrame[6];

      frameLen = 8;

      i -= 2; // First byte is used by diag data

      additionalFrameID++;
      if (additionalFrameID > 0x2F) {
        additionalFrameID = 0x20;
      }

      diagFrame.can_id = CAN_EMIT_ID;
      diagFrame.can_dlc = frameLen;
      CAN0.sendMessage( & diagFrame);

      delay(10);

      frameLen = 0;
    } else if ((i + 2) == dataSize) {
      diagFrame.data[0] = additionalFrameID;
      diagFrame.data[1] = tmpFrame[0];
      diagFrame.data[2] = tmpFrame[1];
      diagFrame.data[3] = tmpFrame[2];
      diagFrame.data[4] = tmpFrame[3];
      diagFrame.data[5] = tmpFrame[4];
      diagFrame.data[6] = tmpFrame[5];
      diagFrame.data[7] = tmpFrame[6];

      frameLen = frameLen + 1;
      additionalFrameID = 0x00;

      diagFrame.can_id = CAN_EMIT_ID;
      diagFrame.can_dlc = frameLen;
      CAN0.sendMessage( & diagFrame);
    }
  }

  return;
}

void sendDiagFrame(char * data) {
  int i = 0;
  int frameLen = 0;
  byte tmpFrame[8] = {0,0,0,0,0,0,0,0};
  struct can_frame diagFrame;

  for (i = 0; i < strlen(data) && i < 16; i += 2) {
    if (isxdigit(data[i]) && isxdigit(data[i + 1])) {
      tmpFrame[frameLen] = ahex2int(data[i], data[(i + 1)]);
      frameLen++;
    } else {
      if (data[i] == -16) {
        Serial.println("000000");
      } else {
        Serial.println("7F0000");
      }
      return;
    }
  }

  if (frameLen > 7) { // Multi-frames
    diagFrame.data[0] = 0x10;
    diagFrame.data[1] = (strlen(data) / 2);
    diagFrame.data[2] = tmpFrame[0];
    diagFrame.data[3] = tmpFrame[1];
    diagFrame.data[4] = tmpFrame[2];
    diagFrame.data[5] = tmpFrame[3];
    diagFrame.data[6] = tmpFrame[4];
    diagFrame.data[7] = tmpFrame[5];

    frameLen = 8;

    i -= 4; // First bytes are used by diag data
    additionalFrameSize = strlen(data) - i;
    additionalFrameID = 0x21;

  } else {

    diagFrame.data[0] = frameLen;
    diagFrame.data[1] = tmpFrame[0];
    diagFrame.data[2] = tmpFrame[1];
    diagFrame.data[3] = tmpFrame[2];
    diagFrame.data[4] = tmpFrame[3];
    diagFrame.data[5] = tmpFrame[4];
    diagFrame.data[6] = tmpFrame[5];
    diagFrame.data[7] = tmpFrame[6];

    frameLen = frameLen + 1;
    additionalFrameID = 0x00;
  }

  diagFrame.can_id = CAN_EMIT_ID;
  diagFrame.can_dlc = frameLen;
  CAN0.sendMessage( & diagFrame);

  return;
}

int pos = 0;
void recvWithTimeout() {
  static unsigned long lastCharMillis = 0;
  char rc;

  lastCharMillis = millis();
  while (Serial.available() > 0) {
    rc = Serial.read();

    receiveDiagFrameData[pos] = rc;

    if (millis() - lastCharMillis >= 1000 || rc == '\n' || pos >= 512) {
      receiveDiagFrameData[pos] = '\0';

      if (receiveDiagFrameData[0] == '>') { // IDs Pair changing
        pos = 0;
        char * ids = strtok(receiveDiagFrameData + 1, ":");
        while (ids != NULL) {
          if (pos == 0) {
            CAN_EMIT_ID = strtoul(ids, NULL, 16);
          } else if (pos == 1) {
            CAN_RECV_ID = strtoul(ids, NULL, 16);
          }
          pos++;
          ids = strtok(NULL, ":");
        }
      } else if (receiveDiagFrameData[0] == ':') { // Unlock with key
        pos = 0;
        char * ids = strtok(receiveDiagFrameData + 1, ":");
        while (ids != NULL) {
          if (pos == 0) {
            UnlockKey = strtoul(ids, NULL, 16);
          } else if (pos == 1) {
            UnlockService = strtoul(ids, NULL, 16);
          } else if (pos == 2) {
            DiagSess = strtoul(ids, NULL, 16);
          }
          pos++;
          ids = strtok(NULL, ":");
        }
        char tmp[3];
        snprintf(tmp, 3, "%02X", UnlockService);

        strcpy(UnlockCMD, "27");
        strcat(UnlockCMD, tmp);

        char diagCMD[5] = "10";
        snprintf(tmp, 3, "%02X", DiagSess);
        strcat(diagCMD, tmp);
        sendDiagFrame(diagCMD);

        waitingUnlock = true;
      } else if (receiveDiagFrameData[0] == 'V') {
        Serial.println(SketchVersion);
      } else if (receiveDiagFrameData[0] == 'N') {
        Dump = false;
        Serial.println("Normal mode");
      } else if (receiveDiagFrameData[0] == 'X') {
        Dump = true;
        Serial.println("Dump mode");
      } else if (receiveDiagFrameData[0] == '?') {
        char tmp[4];
        snprintf(tmp, 4, "%02X", CAN_EMIT_ID);
        Serial.print(tmp);
        Serial.print(":");
        snprintf(tmp, 4, "%02X", CAN_RECV_ID);
        Serial.println(tmp);
      } else {
        sendDiagFrame(receiveDiagFrameData);
        waitingReplySerialCMD = true;
        lastCMDSent = millis();
      }
      pos = 0;
    } else {
      pos++;
    }
  }

  return;
}

void loop() {
  if (Serial.available() > 0) {
    recvWithTimeout();
  }

  if (CAN0.readMessage( & canMsgRcv) == MCP2515::ERROR_OK) {
    int id = canMsgRcv.can_id;
    int len = canMsgRcv.can_dlc;

    if (Dump) {
      if (canMsgRcv.data[1] == 0x7E || canMsgRcv.data[1] == 0x3E) {
        // Diag session keep-alives (useless, do not print)
      } else if (waitingReplySerialCMD && len == 3 && canMsgRcv.data[0] == 0x30 && canMsgRcv.data[1] == 0x00) { // Acknowledgement Write
        sendAdditionalDiagFrames(receiveDiagFrameData, 12);

        waitingReplySerialCMD = false;
        lastCMDSent = 0;
      } else if (len > 2 && canMsgRcv.data[0] == 0x10) { // Acknowledgement Read
        receiveDiagFrameSize = canMsgRcv.data[1];
        if (waitingReplySerialCMD) {
          struct can_frame diagFrame;
          diagFrame.data[0] = 0x30;
          diagFrame.data[1] = 0x00;
          diagFrame.data[2] = 0x05;
          diagFrame.can_id = CAN_EMIT_ID;
          diagFrame.can_dlc = 3;
          CAN0.sendMessage( & diagFrame);

          waitingReplySerialCMD = false;
          lastCMDSent = 0;
        }
        receiveDiagMultiFrame(canMsgRcv);
      } else if (len > 1 && canMsgRcv.data[0] >= 0x20) {
        receiveAdditionalDiagFrame(canMsgRcv);
      } else {
        char tmp[4];
        snprintf(tmp, 4, "%02X", id);
        Serial.print(tmp);
        Serial.print(":");
        for (int i = 1; i < len; i++) { // Strip first byte = Data length
          snprintf(tmp, 3, "%02X", canMsgRcv.data[i]);
          Serial.print(tmp);
        }
        Serial.println();
      }
    } else {
      if (id == CAN_RECV_ID) {
        if (waitingReplySerialCMD && len == 3 && canMsgRcv.data[0] == 0x30 && canMsgRcv.data[1] == 0x00) { // Acknowledgement Write
          sendAdditionalDiagFrames(receiveDiagFrameData, 12);

          waitingReplySerialCMD = false;
          lastCMDSent = 0;
        } else if (len > 2 && canMsgRcv.data[0] == 0x10) { // Acknowledgement Read
          receiveDiagFrameSize = canMsgRcv.data[1];
          if (waitingReplySerialCMD) {
            struct can_frame diagFrame;
            diagFrame.data[0] = 0x30;
            diagFrame.data[1] = 0x00;
            diagFrame.data[2] = 0x05;
            diagFrame.can_id = CAN_EMIT_ID;
            diagFrame.can_dlc = 3;
            CAN0.sendMessage( & diagFrame);

            waitingReplySerialCMD = false;
            lastCMDSent = 0;
          }
          receiveDiagMultiFrame(canMsgRcv);
        } else if (len > 1 && canMsgRcv.data[0] >= 0x20) {
          receiveAdditionalDiagFrame(canMsgRcv);
        } else {
          char tmp[3];
          for (int i = 1; i < len; i++) { // Strip first byte = Data length
            snprintf(tmp, 3, "%02X", canMsgRcv.data[i]);
            Serial.print(tmp);
          }
          Serial.println();
        }
      }
    }

    if (canMsgRcv.data[1] == 0x7F || (lastCMDSent > 0 && millis() - lastCMDSent >= 1000)) { // Error / No answer
      waitingUnlock = false;
      waitingReplySerialCMD = false;
      lastCMDSent = 0;
    } else if (waitingUnlock && canMsgRcv.data[1] == 0x50 && canMsgRcv.data[2] == DiagSess) {
      sendDiagFrame(UnlockCMD);
    }

    if (waitingUnlock && canMsgRcv.data[1] == 0x67 && canMsgRcv.data[2] == UnlockService) {
      char tmp[4];
      char * SeedKey = (char * ) malloc(9);
      char * UnlockCMD = (char * ) malloc(16);

      snprintf(tmp, 3, "%02X", canMsgRcv.data[3]);
      strcpy(SeedKey, tmp);
      snprintf(tmp, 3, "%02X", canMsgRcv.data[4]);
      strcat(SeedKey, tmp);
      snprintf(tmp, 3, "%02X", canMsgRcv.data[5]);
      strcat(SeedKey, tmp);
      snprintf(tmp, 3, "%02X", canMsgRcv.data[6]);
      strcat(SeedKey, tmp);
      unsigned long Key = compute_response(UnlockKey, strtoul(SeedKey, NULL, 16));
      snprintf(SeedKey, 9, "%08lX", Key);

      strcpy(UnlockCMD, "27");
      snprintf(tmp, 3, "%02X", (UnlockService + 1)); // Answer
      strcat(UnlockCMD, tmp);
      strcat(UnlockCMD, SeedKey);

      if (Dump) {
        snprintf(tmp, 4, "%02X", CAN_EMIT_ID);
        Serial.print(tmp);
        Serial.print(":");
        Serial.println(UnlockCMD);
      }

      sendDiagFrame(UnlockCMD);

      waitingUnlock = false;
    }
  }
}