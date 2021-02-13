##  .ulp / .cal Flashing  (UDS)

*Data[I] = I positions are in bytes (2 hex characters = 1 byte)*

*Spaces in commands are only for readability*

### Step 1 - Download session
| Command | Detail |
|--|--|
| 1002 | Open Download session |

### Step 2 - ECU Unlocking

| Command | Detail |
|--|--|
| 2701 | Unlocking service for download |
| 2702 XXXXXXXX | Unlocking response for download - XXXXXXXX = KEY - Must be given within 5 seconds after seed generation |

### Step 3 - Empty flash

| Command | Detail |
|--|--|
| 3101FF00 XX F05A | Empty flash memory - XX = Value from "S0" line inside calibration file (Data[10]) - most of times 81 or 82 |

Resend the same command until you get the response 7101FF000**2** (*7101FF000**1** = Erasing in progress*)

### Step 4 - Prepare writing

| Command | Detail |
|--|--|
| 34 XX 110000 | Prepare flash writing - XX = Value from "S0" line inside calibration file (Data[10]) - most of times 81 or 82 |

Wait until you get the response 741000

### Step 5 - Writing !

All lines are sent one by one, in order
#### For S2 frames:
| Command | Detail |
|--|--|
| 36 XX YY ZZZZZZZZZZ LL CCCC | XX = Line counter % 256 (Starting from 01 to FF then 00 to FF - if big file -) / YY = Data[3] = Flash address / ZZZZZZZZZZ = Data[5] to Data[I] where I = Line length - 5 bytes - 1 byte / LL = Length of ZZZZZZZZZZ (Max 255 bytes) / CCCC = _CRC-16/X-25_(36 XX YY ZZZZZZZZZZ LL) |

#### For S3 frames:
| Command | Detail |
|--|--|
| 36 XX YYYY ZZZZZZZZZZ LL CCCC | XX = Line counter % 256 (Starting from 01 to FF then 00 to FF - if big file -) / YYYY = Data[3] Data[4] = Flash address / ZZZZZZZZZZ = Data[6] to Data[I] where I = Line length - 6 bytes - 1 byte / LL = Length of ZZZZZZZZZZ (Max 255 bytes) / CCCC = _CRC-16/X-25_(36 XX YYYY ZZZZZZZZZZ LL) |

Each command should return 76**XX**02 where XX is the line counter sent

### Step 6 - Flash autocontrol

| Command | Detail |
|--|--|
| 37 | Flash autocontrol |

Wait until you get the response 77 (*7F3778 = Autocontrol in progress*)

### Step 7 - Write ZI (F0FE zone)

| Command | Detail |
|--|--|
| 3101FF04 | Empty ZI Zone |

Wait until you get the response 7101FF0401

| Command | Detail |
|--|--|
| 3103FF04 | Empty ZI Zone |

Wait until you get the response 7103FF0402

| Command | Detail |
|--|--|
| 34 XX 110000 | Prepare ZI zone writing - XX = Value from "S0" line inside calibration file (Data[10]) +2 - most of times 83 or 84 |

Wait until you get the response 741000

#### For S1 frame:
| Command | Detail |
|--|--|
| 3601 XX 0000 YYYY FFFFFF VVVVVVVV TTTTTT SSSSSSSS NN KKKKKK FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF5C CCCC | XX = Data[2] = Flash address / YYYY = Data[8] Data[9] / VVVVVVVV = Data[10] Data[11] Data[12] Data[13] / TTTTTT = Date of the download DD MM YY or 000000 for empty date / SSSSSSSS = FDC7B7E3 = Diagbox signature / NN = Number of downloads, "should" be increased / KKKKKK = Data[14] Data[15] Data[16] = Calibration number / CCCC = _CRC-16/X-25_(XX 0000 YYYY FFFFFF VVVVVVVV TTTTTT SSSSSSSS NN KKKKKK FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF5C) |

Wait until you get the response 760102

### Step 8 - Flash autocontrol (again)

| Command | Detail |
|--|--|
| 37 | Flash autocontrol |

Wait until you get the response 77 (*7F3778 = Autocontrol in progress*)

### DONE

> All those informations have been found and deducted by reverse engineering, some things might be incorrect, no warranty is provided

> ® Ludwig V. <https://github.com/ludwig-v>