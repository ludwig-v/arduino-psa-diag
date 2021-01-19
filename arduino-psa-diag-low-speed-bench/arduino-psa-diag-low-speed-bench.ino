/*
Copyright 2020-2021, Ludwig V. <https://github.com/ludwig-v>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License at <http://www.gnu.org/licenses/> for
more details.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
*/

/////////////////////
//    Libraries    //
/////////////////////

#include <EEPROM.h>
#include <SPI.h>
#include <mcp2515.h> // https://github.com/autowp/arduino-mcp2515 + https://github.com/watterott/Arduino-Libs/tree/master/digitalWriteFast
#include <TimerOne.h> // https://github.com/PaulStoffregen/TimerOne
#include <Thread.h> // https://github.com/ivanseidel/ArduinoThread slightly modified to unprotect runned() function
#include <ThreadController.h>

/////////////////////
//  Configuration  //
/////////////////////

#define SKETCH_VERSION "1.1"
#define CAN_RCV_BUFFER 32
#define MAX_DATA_LENGTH 512
#define CS_PIN_CAN0 10
#define SERIAL_SPEED 115200
#define CAN_SPEED CAN_125KBPS // Entertainment CAN bus - Low Speed
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

char receiveDiagFrameData[MAX_DATA_LENGTH];
int receiveDiagFrameRead;
int receiveDiagFrameSize;
int receiveDiagDataPos = 0;

bool waitingReplySerialCMD = false;
static unsigned long lastCMDSent = 0;
bool waitingUnlock = false;
unsigned short UnlockKey = 0x0000;
byte UnlockService = 0x00;
char * UnlockCMD = (char * ) malloc(5);
char * SketchVersion = (char * ) malloc(4);
byte framesDelay = 0;

static unsigned long lastBSIemul = 0;

bool Lock = false;
bool readingCAN = false;
bool parsingCAN = false;

struct can_frame canMsgRcvBuffer[CAN_RCV_BUFFER];
ThreadController controllerThread = ThreadController();
Thread readCANThread = Thread();
Thread parseCANThread = Thread();

void setup() {
  Serial.begin(SERIAL_SPEED);

  CAN0.reset();
  CAN0.setBitrate(CAN_SPEED, CAN_FREQ);
  while (CAN0.setNormalMode() != MCP2515::ERROR_OK) {
    delay(100);
  }

  strcpy(SketchVersion, SKETCH_VERSION);

  for (int i = 0; i < CAN_RCV_BUFFER; i++) {
    canMsgRcvBuffer[i].can_id = 0;
    canMsgRcvBuffer[i].can_dlc = 0;
  }

  readCANThread.onRun(readCAN);
  if (Dump) { // We have to be ultra fast to flush MCP2515 buffer and not loosing any frame sent without any delay
    readCANThread.setInterval(0);
  } else {
    readCANThread.setInterval(5);
  }

  parseCANThread.onRun(parseCAN);
  parseCANThread.setInterval(5);

  controllerThread.add( & readCANThread);
  controllerThread.add( & parseCANThread);

  Timer1.attachInterrupt(timerCallback);
  Timer1.initialize(100);
  Timer1.start();
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

  receiveDiagFrameRead = 0;

  for (i = 2; i < frame.can_dlc; i++) {
    snprintf(tmp, 3, "%02X", frame.data[i]);
    receiveDiagFrameData[receiveDiagFrameRead] = tmp[0];
    receiveDiagFrameData[receiveDiagFrameRead + 1] = tmp[1];

    receiveDiagFrameRead += 2;
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

    receiveDiagFrameRead += 2;
  }

  if (receiveDiagFrameRead == (receiveDiagFrameSize * 2)) { // Data complete
    receiveDiagFrameData[receiveDiagFrameRead] = '\0';
    receiveDiagDataPos = receiveDiagFrameRead = 0;

    if (Dump) {
      snprintf(tmp, 4, "%02X", CAN_RECV_ID);
      Serial.print(tmp);
      Serial.print(":");
    }
    Serial.println(receiveDiagFrameData);
  }
}

void sendAdditionalDiagFrames() {
  int i = 0;
  int frameLen = 0;
  byte tmpFrame[8] = {0,0,0,0,0,0,0,0};
  struct can_frame diagFrame;

  Lock = true;
  while (readingCAN); // Do not read & write CAN-BUS at the same time due to protothreading

  for (i = 12; i < receiveDiagFrameRead; i += 2) {
    tmpFrame[frameLen] = ahex2int(receiveDiagFrameData[i], receiveDiagFrameData[(i + 1)]);
    frameLen++;

    delay(framesDelay);
    if (frameLen > 0 && (frameLen % 8) == 0) { // Multi-frames
      delay(framesDelay);

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

      frameLen = 0;
    } else if ((i + 2) == receiveDiagFrameRead) {
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

      receiveDiagDataPos = receiveDiagFrameRead = 0;
    }
  }

  Lock = false;

  return;
}

void sendDiagFrame(char * data, int frameFullLen) {
  int i = 0;
  int frameLen = 0;
  byte tmpFrame[8] = {0,0,0,0,0,0,0,0};
  struct can_frame diagFrame;

  Lock = true;
  while (readingCAN); // Do not read & write CAN-BUS at the same time due to protothreading

  for (i = 0; i < frameFullLen && i < 16; i += 2) {
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
    diagFrame.data[1] = (frameFullLen / 2);
    diagFrame.data[2] = tmpFrame[0];
    diagFrame.data[3] = tmpFrame[1];
    diagFrame.data[4] = tmpFrame[2];
    diagFrame.data[5] = tmpFrame[3];
    diagFrame.data[6] = tmpFrame[4];
    diagFrame.data[7] = tmpFrame[5];

    frameLen = 8;

    i -= 4; // First bytes are used by diag data
    additionalFrameSize = frameFullLen - i;
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

    receiveDiagDataPos = receiveDiagFrameRead = 0;
  }

  diagFrame.can_id = CAN_EMIT_ID;
  diagFrame.can_dlc = frameLen;

  CAN0.sendMessage( & diagFrame);

  Lock = false;

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
    receiveDiagFrameRead = pos;

    if (millis() - lastCharMillis >= 1000 || rc == '\n' || pos >= MAX_DATA_LENGTH) {
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
        sendDiagFrame(diagCMD, strlen(diagCMD));

        waitingUnlock = true;
      } else if (receiveDiagFrameData[0] == 'V') {
        Serial.println(SketchVersion);
      } else if (receiveDiagFrameData[0] == 'N') {
        Dump = false;
        Serial.println("Normal mode");
        readCANThread.setInterval(5);
      } else if (receiveDiagFrameData[0] == 'X') {
        Dump = true;
        Serial.println("Dump mode");
        readCANThread.setInterval(0); // We have to be ultra fast to flush MCP2515 buffer and not loosing any frame sent without any delay
      } else if (receiveDiagFrameData[0] == '?') {
        char tmp[4];
        snprintf(tmp, 4, "%02X", CAN_EMIT_ID);
        Serial.print(tmp);
        Serial.print(":");
        snprintf(tmp, 4, "%02X", CAN_RECV_ID);
        Serial.println(tmp);
      } else {
        char tmp[16];
        for (int i = 0; i < receiveDiagFrameRead && i < 16; i++) {
          tmp[i] = receiveDiagFrameData[i];
        }
        sendDiagFrame(tmp, receiveDiagFrameRead);

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
    readCANThread.setInterval(5);

    noInterrupts();
    recvWithTimeout();
    interrupts();

    if (Dump) {
      readCANThread.setInterval(0); // We have to be ultra fast to flush MCP2515 buffer and not loosing any frame sent without any delay
    } else {
      readCANThread.setInterval(5);
    }
  }
}

void parseCAN() {
  if (!Lock && !parsingCAN) {
    parsingCAN = true;

    // To be improved
    if (millis() - lastBSIemul >= 100) { // Every 100ms, minimal frames required to power up a telematic unit
      struct can_frame diagFrame;

      diagFrame.data[0] = 0x80;
      diagFrame.data[1] = 0x00;
      diagFrame.data[2] = 0x02;
      diagFrame.data[3] = 0x00;
      diagFrame.data[4] = 0x00;
      diagFrame.can_id = 0x18;
      diagFrame.can_dlc = 5;
      CAN0.sendMessage( & diagFrame); // Original delay : 1000ms

      diagFrame.data[0] = 0x0E;
      diagFrame.data[1] = 0x00;
      diagFrame.data[2] = 0x03;
      diagFrame.data[3] = 0x2A;
      diagFrame.data[4] = 0x31;
      diagFrame.data[5] = 0x00;
      diagFrame.data[6] = 0x81;
      diagFrame.data[7] = 0xAC;
      diagFrame.can_id = 0x36;
      diagFrame.can_dlc = 8;
      CAN0.sendMessage( & diagFrame); // Original delay : 100ms

      diagFrame.data[0] = 0x8E;
      diagFrame.data[1] = 0x61;
      diagFrame.data[2] = 0x01;
      diagFrame.data[3] = 0x88;
      diagFrame.data[4] = 0xAC;
      diagFrame.data[5] = 0x7B;
      diagFrame.data[6] = 0x7B;
      diagFrame.data[7] = 0x20;
      diagFrame.can_id = 0xF6;
      diagFrame.can_dlc = 8;
      CAN0.sendMessage( & diagFrame); // Original delay : 50ms

      lastBSIemul = millis();
    }

    for (int t = 0; t < CAN_RCV_BUFFER; t++) {
      if (canMsgRcvBuffer[t].can_id > 0) {
        int id = canMsgRcvBuffer[t].can_id;
        int len = canMsgRcvBuffer[t].can_dlc;

        if (Dump) {
          if (canMsgRcvBuffer[t].data[1] == 0x7E || canMsgRcvBuffer[t].data[1] == 0x3E) {
            // Diag session keep-alives (useless, do not print)
          } else if (waitingReplySerialCMD && len == 3 && canMsgRcvBuffer[t].data[0] == 0x30 && canMsgRcvBuffer[t].data[1] == 0x00) { // Acknowledgement Write
            framesDelay = canMsgRcvBuffer[t].data[2];

            sendAdditionalDiagFrames();

            waitingReplySerialCMD = false;
            lastCMDSent = 0;
          } else if (len > 2 && canMsgRcvBuffer[t].data[0] == 0x10) { // Acknowledgement Read
            receiveDiagFrameSize = canMsgRcvBuffer[t].data[1];

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
            receiveDiagMultiFrame(canMsgRcvBuffer[t]);
          } else if (len > 1 && canMsgRcvBuffer[t].data[0] >= 0x20 && canMsgRcvBuffer[t].data[0] <= 0x2F) {
            receiveAdditionalDiagFrame(canMsgRcvBuffer[t]);
          } else if (len == 3 && canMsgRcvBuffer[t].data[0] == 0x30 && canMsgRcvBuffer[t].data[1] == 0x00) {
            // Ignore in dump mode
          } else {
            char tmp[4];
            snprintf(tmp, 4, "%02X", id);
            Serial.print(tmp);
            Serial.print(":");
            for (int i = 1; i < len; i++) { // Strip first byte = Data length
              snprintf(tmp, 3, "%02X", canMsgRcvBuffer[t].data[i]);
              Serial.print(tmp);
            }
            Serial.println();
          }
        } else {
          if (id == CAN_RECV_ID) {
            if (waitingReplySerialCMD && len == 3 && canMsgRcvBuffer[t].data[0] == 0x30 && canMsgRcvBuffer[t].data[1] == 0x00) { // Acknowledgement Write
              framesDelay = canMsgRcvBuffer[t].data[2];

              sendAdditionalDiagFrames();

              waitingReplySerialCMD = false;
              lastCMDSent = 0;
            } else if (len > 2 && canMsgRcvBuffer[t].data[0] == 0x10) { // Acknowledgement Read
              receiveDiagFrameSize = canMsgRcvBuffer[t].data[1];
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
              receiveDiagMultiFrame(canMsgRcvBuffer[t]);
            } else if (len > 1 && canMsgRcvBuffer[t].data[0] >= 0x20 && canMsgRcvBuffer[t].data[0] <= 0x2F) {
              receiveAdditionalDiagFrame(canMsgRcvBuffer[t]);
            } else {
              char tmp[3];
              for (int i = 1; i < len; i++) { // Strip first byte = Data length
                snprintf(tmp, 3, "%02X", canMsgRcvBuffer[t].data[i]);
                Serial.print(tmp);
              }
              Serial.println();
            }
          }
        }

        if (canMsgRcvBuffer[t].data[1] == 0x7F || (lastCMDSent > 0 && millis() - lastCMDSent >= 1000)) { // Error / No answer
          waitingUnlock = false;
          waitingReplySerialCMD = false;
          lastCMDSent = 0;
        } else if (waitingUnlock && canMsgRcvBuffer[t].data[1] == 0x50 && canMsgRcvBuffer[t].data[2] == DiagSess) {
          sendDiagFrame(UnlockCMD, strlen(UnlockCMD));
        }

        if (waitingUnlock && canMsgRcvBuffer[t].data[1] == 0x67 && canMsgRcvBuffer[t].data[2] == UnlockService) {
          char tmp[4];
          char * SeedKey = (char * ) malloc(9);
          char * UnlockCMD = (char * ) malloc(16);

          snprintf(tmp, 3, "%02X", canMsgRcvBuffer[t].data[3]);
          strcpy(SeedKey, tmp);
          snprintf(tmp, 3, "%02X", canMsgRcvBuffer[t].data[4]);
          strcat(SeedKey, tmp);
          snprintf(tmp, 3, "%02X", canMsgRcvBuffer[t].data[5]);
          strcat(SeedKey, tmp);
          snprintf(tmp, 3, "%02X", canMsgRcvBuffer[t].data[6]);
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

          sendDiagFrame(UnlockCMD, strlen(UnlockCMD));

          waitingUnlock = false;
        }

        canMsgRcvBuffer[t].can_id = 0;
      }
    }
    parsingCAN = false;
  }
}

void timerCallback() {
  controllerThread.run();
}

void readCAN() {
  if (!Lock && !readingCAN) {
    readingCAN = true;
    while (CAN0.readMessage( & canMsgRcv) == MCP2515::ERROR_OK) {
      parseCANThread.runned(); // Delay thread execution to avoid loosing messages

      for (int i = 0; i < CAN_RCV_BUFFER; i++) {
        if (canMsgRcvBuffer[i].can_id == 0) { // Free in buffer
          canMsgRcvBuffer[i] = canMsgRcv;
          break;
        }
      }
    }
    readingCAN = false;
  }
}