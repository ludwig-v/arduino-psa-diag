/*
Copyright 2020-2025, Ludwig V. <https://github.com/ludwig-v>
Date: 2025-06-07

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
#include <Thread.h> // https://github.com/ivanseidel/ArduinoThread slightly modified to unprotect runned() function
#include <ThreadController.h>

/////////////////////
//  Configuration  //
/////////////////////

#define SKETCH_VERSION "2.0"
#define CAN_RCV_BUFFER 40
#define CAN_DEFAULT_DELAY 5 // Delay between multiframes (ms)
#define MAX_DATA_LENGTH 512
#define CS_PIN_CAN0 10
#define SERIAL_SPEED 115200
#define CAN_SPEED CAN_500KBPS // Diagnostic CAN bus - High Speed
#define CAN_FREQ MCP_16MHZ // Switch to 8MHZ if you have a 8Mhz module
#define GETSEED_MAX_ATTEMPTS 8

////////////////////
// Initialization //
////////////////////

MCP2515 CAN0(CS_PIN_CAN0); // CAN-BUS Shield

////////////////////
//   Variables    //
////////////////////

// My variables
bool Dump = false; // Passive dump mode, dump Diagbox frames

char tmp[4];

int CAN_EMIT_ID = 0x752; // BSI
int CAN_RECV_ID = 0x652; // BSI

int additionalFrameID;
byte DiagSess = 0x03;

char receiveDiagFrameData[MAX_DATA_LENGTH];
int receiveDiagFrameRead = 0;
int receiveDiagFrameSize = 0;
byte receiveDiagDataPos = 0;
bool multiframeOverflow = false;
int receiveDiagFrameAlreadyFlushed = 0;
byte LIN = 0;

bool waitingReplySerialCMD = false;
unsigned long lastCMDSent = 0;
bool waitingUnlock = false;
byte getSeedAttempts = 0;
unsigned short UnlockKey = 0x0000;
byte UnlockService = 0x00;
char UnlockCMD[5];
char SketchVersion[4];
byte framesDelayInput = CAN_DEFAULT_DELAY;
byte framesDelay = CAN_DEFAULT_DELAY;

bool customFrameSize = false;
int sendingAdditionalDiagFramesPos = 0;
bool sendingAdditionalDiagFrames = false;
unsigned long lastSendingAdditionalDiagFrames = 0;

bool readingCAN = false;
bool parsingCAN = false;
bool Lock = false;

unsigned long lastKeepAliveReceived = 0;
bool sendKeepAlives = false;
byte sendKeepAliveType = 'U';

unsigned long lastRoutineAnswerReceived = 0;
bool sendRoutineChecks = false;
byte sendRoutineCheckType = 'U';
byte routineID[2];

struct can_frame canMsgRcvBuffer[CAN_RCV_BUFFER];
ThreadController controllerThread = ThreadController();
Thread readCANThread = Thread();
Thread everySecondThread = Thread();
Thread parseCANThread = Thread();
Thread sendAdditionalDiagFramesThread = Thread();

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
  readCANThread.setInterval(0);

  everySecondThread.onRun(everySecond);
  everySecondThread.setInterval(1000);

  parseCANThread.onRun(parseCAN);
  parseCANThread.setInterval(4);

  sendAdditionalDiagFramesThread.onRun(sendAdditionalDiagFrames);
  sendAdditionalDiagFramesThread.setInterval(1);

  controllerThread.add( & readCANThread);
  controllerThread.add( & everySecondThread);
  controllerThread.add( & parseCANThread);
  controllerThread.add( & sendAdditionalDiagFramesThread);
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

  receiveDiagFrameAlreadyFlushed = 0;
  multiframeOverflow = false;
  receiveDiagFrameRead = 0;

  for (i = 2; i < frame.can_dlc; i++) {
    snprintf(tmp, 3, "%02X", frame.data[i]);
    receiveDiagFrameData[receiveDiagFrameRead] = tmp[0];
    receiveDiagFrameData[receiveDiagFrameRead + 1] = tmp[1];

    receiveDiagFrameRead += 2;
  }
}

void receiveAdditionalDiagFrame(can_frame frame, bool encap) {
  int i = 0;
  int frameOrder = 0;
  int framePos = 0;
  byte maxBytesperFrame = 8;

  if (encap) // LIN
    maxBytesperFrame--;

  if (frame.data[0] == 0x20) {
    if (receiveDiagDataPos == 0) {
      receiveDiagDataPos += 15; // 21 > 2F
    } else {
      receiveDiagDataPos += 16; // 20 > 2F
    }
  }
  if (receiveDiagFrameAlreadyFlushed > 0) {
    frameOrder = frame.data[0] - 0x20;
  } else {
    frameOrder = frame.data[0] - 0x21 + receiveDiagDataPos;
  }

  for (i = 1; i < frame.can_dlc; i++) {
    snprintf(tmp, 3, "%02X", frame.data[i]);

    if (receiveDiagFrameAlreadyFlushed > 0) {
      framePos = (frameOrder * (maxBytesperFrame - 1) * 2) + ((i - 1) * 2) + 1;
    } else {
      // 6 bytes already received + 7 bytes max per frame
      framePos = ((maxBytesperFrame - 2) * 2) + (frameOrder * (maxBytesperFrame - 1) * 2) + ((i - 1) * 2) + 1;
    }

    if (framePos > MAX_DATA_LENGTH) { // Avoid overflow
      multiframeOverflow = true;
      if (Dump) {
        i = (framePos - 1) / 2;
        Serial.print("Warning: Truncated data ");
        Serial.print(i);
        Serial.print("/");
        Serial.println(receiveDiagFrameSize);
      }
      receiveDiagFrameSize = 0;
      break;
    }

    receiveDiagFrameData[framePos - 1] = tmp[0];
    receiveDiagFrameData[framePos] = tmp[1];

    receiveDiagFrameRead += 2;
  }

  if (framesDelay > 0) { // Can't flush buffer fast enough if no delay (some frames will be lost, data will be truncated)
    if (frame.data[0] == 0x2F) {
      if (Dump && receiveDiagFrameAlreadyFlushed == 0) {
        snprintf(tmp, 4, "%02X", frame.can_id);
        Serial.print(tmp);
        Serial.print(":");
      }

      receiveDiagFrameAlreadyFlushed += receiveDiagFrameRead;

      receiveDiagFrameData[framePos + 1] = '\0';
      receiveDiagDataPos = receiveDiagFrameRead = 0;

      Serial.print(receiveDiagFrameData);
    }
  }

  if ((receiveDiagFrameRead + receiveDiagFrameAlreadyFlushed) == (receiveDiagFrameSize * 2) || framePos > MAX_DATA_LENGTH) { // Data complete or overflow
    if (Dump && receiveDiagFrameAlreadyFlushed == 0) {
      snprintf(tmp, 4, "%02X", frame.can_id);
      Serial.print(tmp);
      Serial.print(":");
    }

    receiveDiagFrameData[receiveDiagFrameRead] = '\0';
    receiveDiagDataPos = receiveDiagFrameRead = receiveDiagFrameAlreadyFlushed = receiveDiagFrameSize = 0;

    Serial.println(receiveDiagFrameData);

    framesDelay = CAN_DEFAULT_DELAY; // Restore default delay
  }
}

void sendAdditionalDiagFrames() {
  if (!Lock && sendingAdditionalDiagFrames && millis() - lastSendingAdditionalDiagFrames >= framesDelay) {
    lastSendingAdditionalDiagFrames = millis();

    int i = 0;
    int frameLen = 0;
    byte tmpFrame[8] = {0,0,0,0,0,0,0,0};
    struct can_frame diagFrame;

    for (i = sendingAdditionalDiagFramesPos; i < receiveDiagFrameRead; i += 2) {
      sendingAdditionalDiagFramesPos = i;
      tmpFrame[frameLen] = ahex2int(receiveDiagFrameData[i], receiveDiagFrameData[(i + 1)]);
      frameLen++;

      if (LIN > 0) {
        if (frameLen > 0 && (frameLen % 7) == 0) { // Multi-frames
          diagFrame.data[0] = LIN;
          diagFrame.data[1] = additionalFrameID;
          diagFrame.data[2] = tmpFrame[0];
          diagFrame.data[3] = tmpFrame[1];
          diagFrame.data[4] = tmpFrame[2];
          diagFrame.data[5] = tmpFrame[3];
          diagFrame.data[6] = tmpFrame[4];
          diagFrame.data[7] = tmpFrame[5];

          frameLen = 8;
          i -= 4; // First bytes are used by diag data

          additionalFrameID++;
          if (additionalFrameID > 0x2F) {
            additionalFrameID = 0x20;
          }

          diagFrame.can_id = CAN_EMIT_ID;
          diagFrame.can_dlc = frameLen;
          CAN0.sendMessage( & diagFrame);

          frameLen = 0;

          return;
        } else if ((i + 2) == receiveDiagFrameRead) {
          diagFrame.data[0] = LIN;
          diagFrame.data[1] = additionalFrameID;
          diagFrame.data[2] = tmpFrame[0];
          diagFrame.data[3] = tmpFrame[1];
          diagFrame.data[4] = tmpFrame[2];
          diagFrame.data[5] = tmpFrame[3];
          diagFrame.data[6] = tmpFrame[4];
          diagFrame.data[7] = tmpFrame[5];

          frameLen = frameLen + 2;
          additionalFrameID++;

          diagFrame.can_id = CAN_EMIT_ID;
          diagFrame.can_dlc = frameLen;
          CAN0.sendMessage( & diagFrame);

          receiveDiagDataPos = receiveDiagFrameRead = 0;

          sendingAdditionalDiagFramesPos = 0;
          lastSendingAdditionalDiagFrames = 0;
          sendingAdditionalDiagFrames = false;
        }

      } else {
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

          frameLen = 0;

          return;
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
          additionalFrameID++;

          diagFrame.can_id = CAN_EMIT_ID;
          diagFrame.can_dlc = frameLen;
          CAN0.sendMessage( & diagFrame);

          receiveDiagDataPos = receiveDiagFrameRead = 0;

          sendingAdditionalDiagFramesPos = 0;
          lastSendingAdditionalDiagFrames = 0;
          sendingAdditionalDiagFrames = false;
        }
      }
    }
    sendingAdditionalDiagFrames = false;
  }
  return;
}

void sendDiagFrame(char * data, int frameFullLen, bool Raw) {
  int i = 0;
  int frameLen = 0;
  byte tmpFrame[8] = {0,0,0,0,0,0,0,0};
  struct can_frame diagFrame;

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

  if (tmpFrame[0] == 0x31) {
    if (tmpFrame[1] == 0x01) { // Start Routine
      sendRoutineChecks = true;
      sendRoutineCheckType = 'U'; // UDS
  
      routineID[0] = tmpFrame[2];
      routineID[1] = tmpFrame[3];
    } else if (tmpFrame[1] != 0x03) {
      sendRoutineChecks = true;
      sendRoutineCheckType = 'K'; // KWP
  
      routineID[0] = tmpFrame[1];
      routineID[1] = 0x01;
    }
  }

  if (LIN > 0 && !Raw) {
    if (frameLen > 6) { // Multi-frames
      diagFrame.data[0] = LIN;
      diagFrame.data[1] = (0x10 + ((frameFullLen / 2) / 256));
      diagFrame.data[2] = ((frameFullLen / 2) % 256);
      diagFrame.data[3] = tmpFrame[0];
      diagFrame.data[4] = tmpFrame[1];
      diagFrame.data[5] = tmpFrame[2];
      diagFrame.data[6] = tmpFrame[3];
      diagFrame.data[7] = tmpFrame[4];

      frameLen = 8;
      additionalFrameID = 0x21;
    } else {

      diagFrame.data[0] = LIN;
      diagFrame.data[1] = frameLen;
      diagFrame.data[2] = tmpFrame[0];
      diagFrame.data[3] = tmpFrame[1];
      diagFrame.data[4] = tmpFrame[2];
      diagFrame.data[5] = tmpFrame[3];
      diagFrame.data[6] = tmpFrame[4];
      diagFrame.data[7] = tmpFrame[5];

      frameLen = frameLen + 2;
      additionalFrameID = 0x00;

      receiveDiagDataPos = receiveDiagFrameRead = 0;
    }
  } else {
    if (frameLen > 7 && !Raw) { // Multi-frames
      diagFrame.data[0] = (0x10 + ((frameFullLen / 2) / 256));
      diagFrame.data[1] = ((frameFullLen / 2) % 256);
      diagFrame.data[2] = tmpFrame[0];
      diagFrame.data[3] = tmpFrame[1];
      diagFrame.data[4] = tmpFrame[2];
      diagFrame.data[5] = tmpFrame[3];
      diagFrame.data[6] = tmpFrame[4];
      diagFrame.data[7] = tmpFrame[5];

      frameLen = 8;
      additionalFrameID = 0x21;
    } else {

      if (Raw) {
        diagFrame.data[0] = tmpFrame[0];
        diagFrame.data[1] = tmpFrame[1];
        diagFrame.data[2] = tmpFrame[2];
        diagFrame.data[3] = tmpFrame[3];
        diagFrame.data[4] = tmpFrame[4];
        diagFrame.data[5] = tmpFrame[5];
        diagFrame.data[6] = tmpFrame[6];
        diagFrame.data[7] = tmpFrame[7];
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
      }

      additionalFrameID = 0x00;

      receiveDiagDataPos = receiveDiagFrameRead = 0;
    }
  }

  diagFrame.can_id = CAN_EMIT_ID;
  diagFrame.can_dlc = frameLen;

  CAN0.sendMessage( & diagFrame);

  return;
}

void everySecond() {
  struct can_frame diagFrame;

  if (sendKeepAlives) {
    if (sendKeepAliveType == 'K') { // KWP
      if (LIN > 0) {
        diagFrame.data[0] = LIN;
        diagFrame.data[1] = 0x01;
        diagFrame.data[2] = 0x3E;

        diagFrame.can_id = CAN_EMIT_ID;
        diagFrame.can_dlc = 3;
      } else {
        diagFrame.data[0] = 0x01;
        diagFrame.data[1] = 0x3E;

        diagFrame.can_id = CAN_EMIT_ID;
        diagFrame.can_dlc = 2;
      }
    } else { // UDS
      if (LIN > 0) {
        diagFrame.data[0] = LIN;
        diagFrame.data[1] = 0x02;
        diagFrame.data[2] = 0x3E;
        diagFrame.data[3] = 0x00;

        diagFrame.can_id = CAN_EMIT_ID;
        diagFrame.can_dlc = 4;
      } else {
        diagFrame.data[0] = 0x02;
        diagFrame.data[1] = 0x3E;
        diagFrame.data[2] = 0x00;

        diagFrame.can_id = CAN_EMIT_ID;
        diagFrame.can_dlc = 3;
      }
    }

    CAN0.sendMessage( & diagFrame);
  }

  if (sendRoutineChecks) {
    if (sendRoutineCheckType == 'K') { // KWP
      diagFrame.data[0] = 0x03;
      diagFrame.data[1] = 0x31;
      diagFrame.data[3] = routineID[0];
      diagFrame.data[4] = 0x01;

      diagFrame.can_id = CAN_EMIT_ID;
      diagFrame.can_dlc = 4;
    } else { // UDS
      diagFrame.data[0] = 0x04;
      diagFrame.data[1] = 0x31;
      diagFrame.data[2] = 0x03;
      diagFrame.data[3] = routineID[0];
      diagFrame.data[4] = routineID[1];

      diagFrame.can_id = CAN_EMIT_ID;
      diagFrame.can_dlc = 5;
    }

    CAN0.sendMessage( & diagFrame);
  }

  return;
}

int pos = 0;
void recvWithTimeout() {
  unsigned long lastCharMillis = 0;
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
        LIN = 0;
        Dump = false;
        sendKeepAlives = false;
        Serial.println("OK");
      } else if (receiveDiagFrameData[0] == 'T') { // Change CAN multiframes delay
        framesDelayInput = strtoul(receiveDiagFrameData + 1, NULL, 10);
        Serial.println("OK");
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
        snprintf(tmp, 3, "%02X", UnlockService);

        strcpy(UnlockCMD, "27");
        strcat(UnlockCMD, tmp);

        char diagCMD[5] = "10";
        snprintf(tmp, 3, "%02X", DiagSess);
        strcat(diagCMD, tmp);
        sendDiagFrame(diagCMD, strlen(diagCMD), false);

        if (DiagSess == 0xC0) { // KWP
          sendKeepAliveType = 'K';
        } else { // UDS
          sendKeepAliveType = 'U';
        }
        sendKeepAlives = true;

        waitingUnlock = true;
      } else if (receiveDiagFrameData[0] == 'V') {
        Serial.println(SketchVersion);
      } else if (receiveDiagFrameData[0] == 'L') {
        LIN = strtoul(receiveDiagFrameData + 1, NULL, 16);
        Serial.println("OK");
      } else if (receiveDiagFrameData[0] == 'W') {
        customFrameSize = true;
        receiveDiagFrameSize = strtoul(receiveDiagFrameData + 1, NULL, 10) * 2;
        Serial.println("WOK");
      } else if (receiveDiagFrameData[0] == 'U') {
        LIN = 0;
        Serial.println("OK");
      } else if (receiveDiagFrameData[0] == 'N') {
        Dump = false;
        Serial.println("OK");
      } else if (receiveDiagFrameData[0] == 'X') {
        Dump = true;
        Serial.println("OK");
      } else if (receiveDiagFrameData[0] == 'K') {
        sendKeepAlives = true;
        if (receiveDiagFrameData[1] == 'U' || receiveDiagFrameData[1] == 'K') {
          sendKeepAliveType = receiveDiagFrameData[1];
        }
        Serial.println("OK");
      } else if (receiveDiagFrameData[0] == 'S') {
        sendKeepAlives = false;
        Serial.println("OK");
      } else if (receiveDiagFrameData[0] == 'R') {
        CAN0.reset();
        CAN0.setBitrate(CAN_SPEED, CAN_FREQ);
        while (CAN0.setNormalMode() != MCP2515::ERROR_OK) {
          delay(100);
        }
        Serial.println("OK");
      } else if (receiveDiagFrameData[0] == '?') {
        snprintf(tmp, 4, "%02X", CAN_EMIT_ID);
        Serial.print(tmp);
        Serial.print(":");
        snprintf(tmp, 4, "%02X", CAN_RECV_ID);
        Serial.println(tmp);
      } else if (receiveDiagFrameData[0] == '#' && receiveDiagFrameRead <= (16 + 1) && (receiveDiagFrameRead - 1) % 2 == 0) { // Send RAW frames
        sendDiagFrame(receiveDiagFrameData + 1, receiveDiagFrameRead - 1, true);

        waitingReplySerialCMD = true;
        lastCMDSent = millis();
      } else if (receiveDiagFrameData[0] == '+') { // Large writing frame splitting
        sendingAdditionalDiagFramesPos = 1;
        sendingAdditionalDiagFrames = true;

        waitingReplySerialCMD = true;
        lastCMDSent = millis();
      } else if (receiveDiagFrameRead % 2 == 0) {
        char tmpFrame[16];
        for (int i = 0; i < receiveDiagFrameRead && i < 16; i++) {
          tmpFrame[i] = receiveDiagFrameData[i];
        }
        if (customFrameSize) { // Large writing frame splitting, recommended split size for continuous data = (receiveDiagFrameRead - 5) % 7 = 0 (ex: 250)
          sendDiagFrame(tmpFrame, receiveDiagFrameSize, false);
          receiveDiagFrameSize = 0;
          customFrameSize = false;
        } else {
          sendDiagFrame(tmpFrame, receiveDiagFrameRead, false);
        }

        waitingReplySerialCMD = true;
        lastCMDSent = millis();
      } else {
        Serial.println("7F0000");
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
  } else {
    timerCallback();
  }
}

void parseCAN() {
  if (!Lock && !parsingCAN && !sendingAdditionalDiagFrames) {
    parsingCAN = true;
    for (int t = 0; t < CAN_RCV_BUFFER; t++) {
      if (canMsgRcvBuffer[t].can_id > 0) {
        int id = canMsgRcvBuffer[t].can_id;

        bool encap = false;
        if (canMsgRcvBuffer[t].data[0] >= 0x40 && canMsgRcvBuffer[t].data[0] <= 0x70) { // UDS or KWP with LIN ECUs, remove encapsulation
          for (int i = 1; i < canMsgRcvBuffer[t].can_dlc; i++) {
            canMsgRcvBuffer[t].data[i - 1] = canMsgRcvBuffer[t].data[i];
          }
          canMsgRcvBuffer[t].can_dlc--;
          encap = true;
        }

        int len = canMsgRcvBuffer[t].can_dlc;

        if (sendRoutineChecks) {
          if (canMsgRcvBuffer[t].data[0] < 0x10 && canMsgRcvBuffer[t].data[1] == 0x71) { // Routine Reply
            lastRoutineAnswerReceived = millis();

            if (len >= 6) { // UDS
              if (canMsgRcvBuffer[t].data[5] != 0x01) { // Routine not running state
                sendRoutineChecks = false;
              }
            } else if (len <= 4) { // KWP
               if (canMsgRcvBuffer[t].data[3] != 0x01) { // Routine not running state
                sendRoutineChecks = false;
              }
            }
          }
        }

        if (canMsgRcvBuffer[t].data[0] < 0x10 && canMsgRcvBuffer[t].data[1] == 0x7E) {
          lastKeepAliveReceived = millis();
        } else if (canMsgRcvBuffer[t].data[0] < 0x10 && canMsgRcvBuffer[t].data[1] == 0x3E) {
          sendKeepAlives = false; // Diagbox or external tool sending keep-alives, stop sending ours
        } else if (Dump) {
          if (waitingReplySerialCMD && len == 3 && canMsgRcvBuffer[t].data[0] == 0x30 && canMsgRcvBuffer[t].data[1] == 0x00) { // Acknowledgement Write
            framesDelay = canMsgRcvBuffer[t].data[2];

            if (LIN > 0)
              sendingAdditionalDiagFramesPos = 10; // 5 bytes already sent
            else
              sendingAdditionalDiagFramesPos = 12; // 6 bytes already sent
            sendingAdditionalDiagFrames = true;

            waitingReplySerialCMD = false;
            lastCMDSent = 0;
          } else if (len > 2 && canMsgRcvBuffer[t].data[0] >= 0x10 && canMsgRcvBuffer[t].data[0] <= 0x19) { // Acknowledgement Read
            receiveDiagFrameSize = ((canMsgRcvBuffer[t].data[0] - 0x10) * 256) + canMsgRcvBuffer[t].data[1];

            if (waitingReplySerialCMD && LIN) {
              struct can_frame diagFrame;
              diagFrame.data[0] = LIN;
              diagFrame.data[1] = 0x30;
              diagFrame.data[2] = 0x00;
              diagFrame.data[3] = framesDelayInput;
              diagFrame.can_id = CAN_EMIT_ID;
              diagFrame.can_dlc = 4;
              CAN0.sendMessage( & diagFrame);

              waitingReplySerialCMD = false;
              lastCMDSent = 0;
            } else if (waitingReplySerialCMD) {
              struct can_frame diagFrame;
              diagFrame.data[0] = 0x30;
              diagFrame.data[1] = 0x00;
              diagFrame.data[2] = framesDelayInput;
              diagFrame.can_id = CAN_EMIT_ID;
              diagFrame.can_dlc = 3;
              CAN0.sendMessage( & diagFrame);

              waitingReplySerialCMD = false;
              lastCMDSent = 0;
            }
            receiveDiagMultiFrame(canMsgRcvBuffer[t]);
          } else if (len > 1 && canMsgRcvBuffer[t].data[0] >= 0x20 && canMsgRcvBuffer[t].data[0] <= 0x2F) {
            if (!multiframeOverflow)
              receiveAdditionalDiagFrame(canMsgRcvBuffer[t], encap);
          } else if (len == 3 && canMsgRcvBuffer[t].data[0] == 0x30 && canMsgRcvBuffer[t].data[1] == 0x00) {
            // Ignore in dump mode
            framesDelay = canMsgRcvBuffer[t].data[2];
          } else if (id != 0x353) { // Ignore 0x353 blank frame from some BSI
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

              if (LIN > 0)
                sendingAdditionalDiagFramesPos = 10; // 5 bytes already sent
              else
                sendingAdditionalDiagFramesPos = 12; // 6 bytes already sent
              sendingAdditionalDiagFrames = true;

              waitingReplySerialCMD = false;
              lastCMDSent = 0;
            } else if (len > 2 && canMsgRcvBuffer[t].data[0] >= 0x10 && canMsgRcvBuffer[t].data[0] <= 0x19) { // Acknowledgement Read
              receiveDiagFrameSize = ((canMsgRcvBuffer[t].data[0] - 0x10) * 256) + canMsgRcvBuffer[t].data[1];

              if (waitingReplySerialCMD && LIN) {
                struct can_frame diagFrame;
                diagFrame.data[0] = LIN;
                diagFrame.data[1] = 0x30;
                diagFrame.data[2] = 0x00;
                diagFrame.data[3] = framesDelayInput;
                diagFrame.can_id = CAN_EMIT_ID;
                diagFrame.can_dlc = 4;
                CAN0.sendMessage( & diagFrame);

                waitingReplySerialCMD = false;
                lastCMDSent = 0;
              } else if (waitingReplySerialCMD) {
                struct can_frame diagFrame;
                diagFrame.data[0] = 0x30;
                diagFrame.data[1] = 0x00;
                diagFrame.data[2] = framesDelayInput;
                diagFrame.can_id = CAN_EMIT_ID;
                diagFrame.can_dlc = 3;
                CAN0.sendMessage( & diagFrame);

                waitingReplySerialCMD = false;
                lastCMDSent = 0;
              }
              receiveDiagMultiFrame(canMsgRcvBuffer[t]);
            } else if (len > 1 && canMsgRcvBuffer[t].data[0] >= 0x20 && canMsgRcvBuffer[t].data[0] <= 0x2F) {
              if (!multiframeOverflow)
                receiveAdditionalDiagFrame(canMsgRcvBuffer[t], encap);
            } else {
              for (int i = 1; i < len; i++) { // Strip first byte = Data length
                snprintf(tmp, 3, "%02X", canMsgRcvBuffer[t].data[i]);
                Serial.print(tmp);
              }
              Serial.println();
            }
          }
        }

        if (canMsgRcvBuffer[t].data[0] < 0x10 && (canMsgRcvBuffer[t].data[1] == 0x7F || (lastCMDSent > 0 && millis() - lastCMDSent >= 1000))) { // Error / No answer
          if (canMsgRcvBuffer[t].data[2] == 0x3E) {
            sendKeepAlives = false; // Stop sending Keep-Alives
          }
          if (canMsgRcvBuffer[t].data[2] == 0x31) {
            sendRoutineChecks = false; // Stop sending Routine Checks
          }
          if (waitingUnlock && canMsgRcvBuffer[t].data[1] == 0x7F && canMsgRcvBuffer[t].data[2] == 0x27 && canMsgRcvBuffer[t].data[3] != 0x37) {
            waitingUnlock = false;
          }
          if (waitingUnlock && getSeedAttempts <= GETSEED_MAX_ATTEMPTS) {
            if (lastCMDSent > 0 && millis() - lastCMDSent >= 2000) {
              sendDiagFrame(UnlockCMD, strlen(UnlockCMD), false);
              lastCMDSent = millis();
              getSeedAttempts++;
            }
          } else {
            waitingUnlock = false;
            waitingReplySerialCMD = false;
            lastCMDSent = 0;
            getSeedAttempts = 0;
          }
        } else if (waitingUnlock && canMsgRcvBuffer[t].data[0] < 0x10 && canMsgRcvBuffer[t].data[1] == 0x50 && canMsgRcvBuffer[t].data[2] == DiagSess) {
          sendDiagFrame(UnlockCMD, strlen(UnlockCMD), false);
          lastCMDSent = millis();
        }

        if (sendRoutineChecks) {
          if (lastRoutineAnswerReceived > 0 && millis() - lastRoutineAnswerReceived >= 4000) { // ECU connection lost, no answer
            sendRoutineChecks = false; // Stop sending Routine Checks
          }
        } else if (sendKeepAlives) {
          if (lastKeepAliveReceived > 0 && millis() - lastKeepAliveReceived >= 4000) { // ECU connection lost, no answer
            Serial.println("7F3E03"); // Custom error
  
            sendKeepAlives = false; // Stop sending Keep-Alives
          }
        }

        if (waitingUnlock && canMsgRcvBuffer[t].data[0] < 0x10 && canMsgRcvBuffer[t].data[1] == 0x67 && canMsgRcvBuffer[t].data[2] == UnlockService) {
          char SeedKey[9];
          char UnlockCMD_Seed[16];

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

          strcpy(UnlockCMD_Seed, "27");
          snprintf(tmp, 3, "%02X", (UnlockService + 1)); // Answer
          strcat(UnlockCMD_Seed, tmp);
          strcat(UnlockCMD_Seed, SeedKey);

          if (Dump) {
            snprintf(tmp, 4, "%02X", CAN_EMIT_ID);
            Serial.print(tmp);
            Serial.print(":");
            Serial.println(UnlockCMD_Seed);
          }

          sendDiagFrame(UnlockCMD_Seed, strlen(UnlockCMD_Seed), false);

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
    struct can_frame canMsgRcv;
    readingCAN = true;
    while (CAN0.readMessage( & canMsgRcv) == MCP2515::ERROR_OK) {
      for (int i = 0; i < CAN_RCV_BUFFER; i++) {
        if (canMsgRcvBuffer[i].can_id == 0) { // Free in buffer
          canMsgRcvBuffer[i] = canMsgRcv;
          parseCANThread.runned(); // Delay thread execution to avoid loosing messages
          break;
        }
      }
    }
    readingCAN = false;
  }
}
