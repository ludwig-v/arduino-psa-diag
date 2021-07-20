
# arduino-psa-diag
Arduino sketch to send diagnostic frames to PSA cars

## How to use
The serial console is used to send raw diagnostic frames, start it using 115200 baud rate

## Choose ECU
You have to choose the ECU you want to communicate with by inputing its diagnostic frame IDs in hexadecimal:

    >CAN_EMIT_ID:CAN_RECV_ID

Telematic unit (UDS) access example:

    >764:664

To access LIN ECUs (LIN over UDS) you must input diagnostic frame IDs in hexadecimal as well as ECU code

DGT7CFF LVDS Screen (LIN) access example:

    >736:716
    L47

Check out [ECU_LIST.md](https://github.com/ludwig-v/arduino-psa-diag/blob/master/ECU_LIST.md) inside the repo for other ECUs IDs pair

## Easy Unlock
Once you are on the chosen ECU you can unlock writing easily using this shortcut:

    :UNLOCK_KEY:UNLOCK_SERVICE:DIAG_SESSION_ID
NAC Telematic unit (**UDS**) unlock example:

    :D91C:03:03
AMPLI_AUDIO Amplifier unit (**KWP**) unlock example:

    :A7D8:83:C0
Check out [ECU_KEYS.md](https://github.com/ludwig-v/psa-seedkey-algorithm/blob/main/ECU_KEYS.md) for other ECUs keys

## UDS Commands

| Command | Description |
|--|--|
| 3E00 | Keep-Alive session |
| 190209 | List of current faults |
| 14FFFFFF | Clear faults |
| 37 | Flash autocontrol (Unit must be unlocked first) |
| 1001 | End of communication |
| 1002 | Open Download session |
| 1003 | Open Diagnostic session |
| 1103 | Reboot |
| 2701 | Unlocking service for download (Diagnostic session must be enabled first) - SEED |
| 2703 | Unlocking service for configuration (Diagnostic session must be enabled first) - SEED |
| 2702XXXXXXXX  | Unlocking response for download - XXXXXXXX = KEY - Must be given within 5 seconds after seed generation |
| 2704XXXXXXXX  | Unlocking response for configuration - XXXXXXXX = KEY - Must be given within 5 seconds after seed generation |
| 22XXXX | Read Zone XXXX (2 bytes) |
| 2EXXXXYYYYYYYYYYYY | Write Zone XXXX with data YYYYYYYYYYYY (Unit must be unlocked first) |
| 3101FF0081F05A | Empty flash memory for .cal upload (Unit must be unlocked first) |
| 3101FF0082F05A | Empty flash memory for .ulp upload (Unit must be unlocked first) - WARNING: If you don't upload a valid file your device will be temporarily bricked |
| 3103FF00 | Empty flash memory (Unit must be unlocked first) |
| 3481110000 | Prepare flash writing for .cal upload (Unit must be unlocked first) |
| 3482110000 | Prepare flash writing for .ulp upload (Unit must be unlocked first) |
| 3101FF04 | Empty ZI Zone (Unit must be unlocked first) |
| 3103FF04 | Empty ZI Zone (Unit must be unlocked first) |
| 3483110000 | Prepare ZI zone writing (Unit must be unlocked first) |

## KWP2000 (HAB - 125Kbits) Commands

| Command | Description |
|--|--|
| 3E | Keep-Alive session |
| 17FF00 | List of current faults |
| 14FF00 | Clear faults |
| 1081 | End of communication |
| 10C0 | Open Diagnostic session |
| 31A800 | Reboot |
| 31A801 | Reboot 2 |
| 2781 | Unlocking service for download (Diagnostic session must be enabled first) - SEED |
| 2783 | Unlocking service for configuration (Diagnostic session must be enabled first) - SEED |
| 2782XXXXXXXX  | Unlocking response for download - XXXXXXXX = KEY - Must be given within 5 seconds after seed generation |
| 2784XXXXXXXX  | Unlocking response for configuration - XXXXXXXX = KEY - Must be given within 5 seconds after seed generation |
| 21XX | Read Zone XX (1 byte) |
| 3BXXYYYYYYYYYYYY | Write Zone XX with data YYYYYYYYYYYY (Unit must be unlocked first) |
| 318181F05A | Empty flash memory for .cal upload (Unit must be unlocked first) |
| 318101 | Empty flash memory for .cal upload (Unit must be unlocked first) |
| 348100000D07 | Prepare flash writing for .cal upload (Unit must be unlocked first) |

## KWP2000 (IS - 500Kbits) Commands

| Command | Description |
|--|--|
| 3E | Keep-Alive session |
| 17FF00 | List of current faults |
| 14FF00 | Clear faults |
| 82 | End of communication |
| 81 | Open Diagnostic session |
| 2781 | Unlocking service for download (Diagnostic session must be enabled first) - SEED |
| 2783 | Unlocking service for configuration (Diagnostic session must be enabled first) - SEED |
| 2782XXXXXXXX  | Unlocking response for download - XXXXXXXX = KEY - Must be given within 5 seconds after seed generation |
| 2784XXXXXXXX  | Unlocking response for configuration - XXXXXXXX = KEY - Must be given within 5 seconds after seed generation |
| 21XX | Read Zone XX (1 byte) |
| 34XXYYYYYYYYYYYY | Write Zone XX with data YYYYYYYYYYYY (Unit must be unlocked first) |

## UDS Answers

| Answer | Description |
|--|--|
| 7E00 | Keep-Alive reply |
| 54 | Faults cleared |
| 7103FF0001 | Flash erasing in progress |
| 7103FF0003 | Flash not erased: error |
| 7101FF0401 | ZI erased successfully |
| 7103FF0402 | ZI erasing in progress |
| 7103FF0403 | ZI not erased: error |
| 77 | Flash autocontrol OK |
| 5001XXXXXXXX | Communication closed |
| 5002XXXXXXXX | Download session opened |
| 5003XXXXXXXX | Diagnostic session opened |
| 5103 | Reboot OK |
| 62XXXXYYYYYYYYYYYY  | Successfull read of Zone XXXX - YYYYYYYYYYYY = DATA |
| 6701XXXXXXXX | Seed generated for download - XXXXXXXX = SEED |
| 6703XXXXXXXX | Seed generated for configuration - XXXXXXXX = SEED |
| 6702 | Unlocked successfully for download - Unit will be locked again if no command is issued within 5 seconds |
| 6704 | Unlocked successfully for configuration - Unit will be locked again if no command is issued within 5 seconds |
| 6EXXXX | Successfull Configuration Write of Zone XXXX |
| 741000 | Download Writing ready |
| 76XX02 | Download frame XX injected with success |
| 76XX0A | Invalid checksum on download frame XX |

## UDS Errors

| Answer | Description |
|--|--|
| 7F3478 | Download Writing in progress |
| 7F3778 | Flash autocontrol in progress |
| 7FXX78 | XX = Service - In progress |
| 7F2231 | Configuration Read - Request out of range |
| 7F2233 | Configuration Read - Security Access Denied |
| 7F2712 | Unlocking - Subfunction not supported |
| 7F2722 | Unlocking - Conditions not correct |
| 7F2724 | Unlocking - Request Sequence Error |
| 7F2736 | Unlocking - Exceeded number of attempts |
| 7F2737 | Unlocking - Required time delay not expired |
| 7F2713 | Unlocking - Invalid SEED Answer (KEY) |
| 7F2E78 | Configuration Write in progress |
| 7F2E72 | Configuration Write - General Programming Failure |
| 7F2E13 | Configuration Write - Invalid Zone data |
| 7F2E7E | Configuration Write - Unit is locked |
| 7F2E31 | Configuration Write - Request out of range |
| 7F2EYY | Failed Configuration Write - YY = Response code |
| 7F2E31 | Request out of range |
| 7F22YY | Failed Configuration Read - YY = Response code |
| 7FXXYY | Error - XX = Service / YY = Response code |

## KWP2000 (HAB - 125Kbits) Answers

| Answer | Description |
|--|--|
| 7E | Keep-Alive reply |
| 5081 | Communication closed |
| 50C0 | Diagnostic session opened |
| 71A801 | Reboot |
| 71A802 | Reboot 2 |
| 57XXYYYYZZ | XX = Number of DTC, YYYY = DTC code, ZZ = DTC data |
| 61XXYYYYYYYYYYYY  | Successfull read of Zone XX - YYYYYYYYYYYY = DATA |
| 6781XXXXXXXX | Seed generated for download - XXXXXXXX = SEED |
| 6783XXXXXXXX | Seed generated for configuration - XXXXXXXX = SEED |
| 6782 | Unlocked successfully for download - Unit will be locked again if no command is issued within 5 seconds |
| 6784 | Unlocked successfully for configuration - Unit will be locked again if no command is issued within 5 seconds |

## KWP2000 (IS - 500Kbits) Answers

| Answer | Description |
|--|--|
| 7E | Keep-Alive reply |
| C2 | Communication closed |
| C1XXXX | Diagnostic session opened |
| 57XXYYYYZZ | XX = Number of DTC, YYYY = DTC code, ZZ = DTC data |
| 61XXYYYYYYYYYYYY  | Successfull read of Zone XX - YYYYYYYYYYYY = DATA |
| 6781XXXXXXXX | Seed generated for download - XXXXXXXX = SEED |
| 6783XXXXXXXX | Seed generated for configuration - XXXXXXXX = SEED |
| 6782 | Unlocked successfully for download - Unit will be locked again if no command is issued within 5 seconds |
| 6784 | Unlocked successfully for configuration - Unit will be locked again if no command is issued within 5 seconds |

##  Secured Traceability

### UDS

After each configuration writing you must write **2901** zone for the so called "Secured Traceability" to avoid *B1003 DTC error* in the ECU

| Command | Zone | [Site](https://github.com/ludwig-v/arduino-psa-diag/blob/master/SITES.md) | [Signature](https://github.com/ludwig-v/arduino-psa-diag/blob/master/SIGNATURES.md) | Day | Month | Year |
| - | - | - | - | - | - | - |
| 2E | 2901 | FD | 000000 | 01 | 01 | 01 |
| *Write* | *Traceability* | *Aftersales* | *Factory* | *01* | *January* | *2001* |

Quick & dirty command that works everytime:

    2E2901FD000000010101

#### Writing counter

Each time you write the traceability zone a counter in increased inside **C000** zone, this zone can't be rewrited

    22C000

| Command | Zone | Number of secured writings | Number of non-secured writings |
| - | - | - | - |
| 62 | C000 | 2A | 00 |
| *Answer* | *Traceability* | *42* | *0* |

### KWP

After each configuration writing you must write **A0** zone for the so called "Secured Traceability" to avoid *F303 DTC error* in the ECU

| Command | Zone |  | [Site](https://github.com/ludwig-v/arduino-psa-diag/blob/master/SITES.md) | [Signature](https://github.com/ludwig-v/arduino-psa-diag/blob/master/SIGNATURES.md) | Day | Month | Year | Number of secured writings | Number of non-secured writings |
| - | - | - | - | - | - | - | - | - | - |
| 3B | A0 | FF | FD | 000000 | 01 | 01 | 01 | 2A | 00 |
| *Write* | *Traceability* |  | *Aftersales* | *Factory* | *01* | *January* | *2001* | *42* | *0* |

*Number of secured writings "should" be increased after each writing*

Quick & dirty command that works everytime:

    3BA0FFFD0000000101010000

## PSA Seed/Key algorithm

[Algorithm can be found here with some example source code](https://github.com/ludwig-v/psa-seedkey-algorithm)

## Diagnostic frames explanation / What the Sketch is doing

CAN-BUS is limited to 8 bytes per frame, to send larger data ~~PSA~~ automotive industry via ISO 15765-2 chose a simple algorythm to truncate the data into multiple parts

### To send data smaller or equal to 7 bytes:
#### [SEND] Frame:
| Byte 1 | Byte 2 | Byte 3 | Byte 4 | Byte 5 | Byte 6 | Byte 7 | Byte 8 |
|--|--|--|--|--|--|--|--|
| Full Data Length | Data[0] | Data[1] | Data[2] | Data[3] | Data[4] | Data[5] | Data[6] |

### To send data larger than 7 bytes:
#### [SEND] First Frame:
| Byte 1 | Byte 2 | Byte 3 | Byte 4 | Byte 5 | Byte 6 | Byte 7 | Byte 8 |
|--|--|--|--|--|--|--|--|
| 0x10 | Full Data Length | Data[0] | Data[1] | Data[2] | Data[3] | Data[4] | Data[5] |
#### [RECEIVE] Write Acknowledgement Frame:
| Byte 1 | Byte 2 | Byte 3 |
|--|--|--|
| 0x30 | 0x00 | Delay between consecutive frames in ms |
#### [SEND] Second Frame:
##### ID starting at 0x21 and increasing by 1 for every extra frame needed, after 0x2F reverting back to 0x20
| Byte 1 | Byte 2 | Byte 3 | Byte 4 | Byte 5 | Byte 6 | Byte 7 | Byte 8 |
|--|--|--|--|--|--|--|--|
| 0x21 | Data[6] | Data[7] | Data[8] | Data[9] | Data[10] | Data[11] | Data[12] |
#### [SEND] Third Frame:
| Byte 1 | Byte 2 | Byte 3 | Byte 4 | Byte 5 | Byte 6 | Byte 7 | Byte 8 |
|--|--|--|--|--|--|--|--|
| 0x22 | Data[13] | Data[14] | Data[15] | Data[16] | Data[17] | Data[18] | Data[19] |

### To receive data smaller or equal to 7 bytes:
#### [RECEIVE] Frame:
| Byte 1 | Byte 2 | Byte 3 | Byte 4 | Byte 5 | Byte 6 | Byte 7 | Byte 8 |
|--|--|--|--|--|--|--|--|
| Full Data Length | Data[0] | Data[1] | Data[2] | Data[3] | Data[4] | Data[5] | Data[6] |

### To receive data larger than 7 bytes:
#### [RECEIVE] First Frame:
| Byte 1 | Byte 2 | Byte 3 | Byte 4 | Byte 5 | Byte 6 | Byte 7 | Byte 8 |
|--|--|--|--|--|--|--|--|
| 0x10 | Full Data Length | Data[0] | Data[1] | Data[2] | Data[3] | Data[4] | Data[5] |
#### [SEND] Read Acknowledgement Frame:
| Byte 1 | Byte 2 | Byte 3 |
|--|--|--|
| 0x30 | 0x00 | Delay between consecutive frames in ms |
#### [SEND] Second Frame:
##### ID starting at 0x21 and increasing by 1 for every extra frame needed, after 0x2F reverting back to 0x20
| Byte 1 | Byte 2 | Byte 3 | Byte 4 | Byte 5 | Byte 6 | Byte 7 | Byte 8 |
|--|--|--|--|--|--|--|--|
| 0x21 | Data[6] | Data[7] | Data[8] | Data[9] | Data[10] | Data[11] | Data[12] |
#### [SEND] Third Frame:
| Byte 1 | Byte 2 | Byte 3 | Byte 4 | Byte 5 | Byte 6 | Byte 7 | Byte 8 |
|--|--|--|--|--|--|--|--|
| 0x22 | Data[13] | Data[14] | Data[15] | Data[16] | Data[17] | Data[18] | Data[19] |

> Received frames could be out-of-order, ID must be used to append parts at the correct position into the final data whose size is known

## Calibration file (.cal / .ulp) explanation

Type of file : **Motorola S-record - https://en.wikipedia.org/wiki/SREC_(file_format)**

*1h = 1 HEX Byte or 2 characters in the .cal/.ulp file*

### Content Size (S2 frames - Zones data block)
| TYPE | LENGTH | ADDRESS | LENGTH2 | ZONE | DATA | CHECKSUM | CHECKSUM2 |
|--|--|--|--|--|--|--|--|
| 1h | 1h | 3h | 2h | 2h | Variable Length | 2h | 1h |

### Content Size (S3 frames - Binary data block)
| TYPE | LENGTH | ADDRESS | LENGTH2 | DATA | CHECKSUM | CHECKSUM2 |
|--|--|--|--|--|--|--|
| 1h | 1h | 4h | 2h | Variable Length | 2h | 1h |

### Special frames

#### S0 (Hardware info)

| TYPE | LENGTH | NOT_USED | FAMILY_CODE | ISO_LINE | INTERBYTE_TX | INTERBYTE_RX | INTER_TXRX | INTER_RXTX | CAL_TYPE | LOGICAL_MARK | K_LINE_MANAGEMENT | CHECKSUM2 |
|--|--|--|--|--|--|--|--|--|--|--|--|--|
| 1h | 1h | 2h | 1h | 1h | 1h | 1h | 1h | 1h | 1h | 1h | 2h | 1h |

#### S1 (Identification Zone - ZI)

| TYPE | LENGTH | NOT_USED | FLASH_SIGNATURE | [UNLOCK_KEY](https://github.com/ludwig-v/psa-seedkey-algorithm/blob/main/ECU_KEYS.md) | [SUPPLIER](https://github.com/ludwig-v/arduino-psa-diag/blob/master/ECU_SUPPLIERS.md) | SYSTEM | APPLICATION | SOFTWARE_VERSION | SOFTWARE_EDITION | CAL_NUMBER | CHECKSUM2 |
|--|--|--|--|--|--|--|--|--|--|--|--|
| 1h | 1h | 2h | 2h | 1h | 1h | 1h | 1h | 1h | 2h | 3h | 1h |

#### S5 (Optional control frame - Number of frames in previous data block)

| TYPE | LENGTH | NB_FRAMES | CHECKSUM2 |
|--|--|--|--|
| 1h | 1h | Variable Length | 1h |

#### S7 (End of S3 frames data block)

| TYPE | LENGTH | ADDRESS | CHECKSUM2 |
|--|--|--|--|
| 1h | 1h | Optional | 1h |

#### S8 (End of S2 frames data block)

| TYPE | LENGTH | ADDRESS | CHECKSUM2 |
|--|--|--|--|
| 1h | 1h | Optional | 1h |

#### S9 (End of S1 frame data block)

| TYPE | LENGTH | ADDRESS | CHECKSUM2 |
|--|--|--|--|
| 1h | 1h | Optional | 1h |

### Content Data
| Line Part | Line Detail |
|--|--|
| **TYPE** | S0 = Hardware info / S1 = ZI Zone / S2 / S3 / S8 |
| **LENGTH** | Hex Length of ADDRESS+ZONE+DATA+CHECKSUM+CHECKSUM2 |
| **ADDRESS** | 16bits / 24bits / 32bits |
| **LENGTH2** | Hex Length of ZONE+DATA+CHECKSUM |
| **ZONE** |  |
| **DATA** |  |
| **CHECKSUM** | *CRC-16/X-25*(DATA) with this order CRC[1] CRC[0] |
| **CHECKSUM2** | *CRC-8/2s_complement*(ADDRESS+ZONE+DATA+CHECKSUM) - 1 |
| **FAMILY_CODE** | Family Code |
| **ISO_LINE** | 00h: CAN / 01h: LIN / 05h: ISO 5 / 08h: ISO 8 |
| **INTERBYTE_TX** | Delay between bytes sending (0.1ms step) |
| **INTERBYTE_RX** | Delay between bytes receiving (0.1ms step) |
| **INTER_TXRX** | Delay between sent and received frames (1ms step) |
| **INTER_RXTX** | Delay between received and sent frames (1ms step) |
| **CAL_TYPE** | 81h: .cal / 82h: .ulp / 92h: .ulp new gen |
| **LOGICAL_MARK** |  |
| **K_LINE_MANAGEMENT** | 0000h: yes / 0FXXh: no, timeout of XXs |
| **FLASH_SIGNATURE** | Flash Signature |
| **[UNLOCK_KEY](https://github.com/ludwig-v/psa-seedkey-algorithm/blob/main/ECU_KEYS.md)** | ECU Unlock key |
| **[SUPPLIER](https://github.com/ludwig-v/arduino-psa-diag/blob/master/ECU_SUPPLIERS.md)** | Supplier ID |
| **SYSTEM** |  |
| **APPLICATION** |  |
| **SOFTWARE_VERSION** |  |
| **SOFTWARE_EDITION** | FF.XX |
| **CAL_NUMBER** | 96 XXXXXX 80 |

## Dump Mode

This sketch also provides a way to dump Diagbox frames (read & write) very easily, it is merging split CAN-BUS frames together to have a clean and "human readable" output. Just enable it this way in the source code:

    bool Dump = true;
	
You can also use the serial console typing:

    X
	
To revert into normal mode:

    N

### Example
You can build yourself that kind of cable using an OBD2 Extender cable to use both the Arduino and PSA Interface:
![Dump cable](https://i.imgur.com/UVQRsyr.png)
#### PINOUT

| PIN | Description |
|--|--|
| 3 | CAN-BUS Diagnostic High |
| 8 | CAN-BUS Diagnostic Low |

![OBD2 PINOUT](https://i.imgur.com/sWJF8gg.png)

> Reminder: In Dump mode the termination resistor should not be activated
