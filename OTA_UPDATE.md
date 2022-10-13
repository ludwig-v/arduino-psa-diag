##  NEA2020 - OTA (Over-The-Air) Update

### Step 1 - Unlock VSM (New BSI)

>\>752:652

| Command | Detail |
|--|--|
| 2703 | Unlocking service for coding |
| 2704 XXXXXXXX | Unlocking response for coding - XXXXXXXX = KEY - Must be given within 5 seconds after seed generation |

>:C6E2:03:03

### Step 2 - Start OTA update on VSM
| Command | Detail |
|--|--|
| 3101061001 | Start OTA update |

*Expected answer: 710106100200*

---

### IVI / BSRF Zones Description

| Zone | Detail | Example | Decoded |
|--|--|--|--|
| F181 | Application Software Identification (ASCII Encoded) | 010A393639353637353038300001 | 9695675080 |
| F182 | Application Data Identification (ASCII Encoded) | 010A393639353136393938300001 | 9695169980 |
| F191 | ECU Hardware Number (ASCII Encoded) | 010A39383434333538303830 | 9844358080 |

---

### Step 3 - Unlock BSRF (New BTA)

>\>77C:67C

| Command | Detail |
|--|--|
| 2703 | Unlocking service for coding |
| 2704 XXXXXXXX | Unlocking response for coding - XXXXXXXX = KEY - Must be given within 5 seconds after seed generation |

>:B7A9:03:03

### Step 4 - Send WIFI settings to BSRF

| Command | Detail |
|--|--|
| 2E F020 45423D6124567A2F4B7644562B7155207B6A45546D44494147424F585F7373696403 | Write WIFI settings |

Decoded (expected) default WIFI settings:

| SSID | Password ( Take care of the space before { ) | Type |
|--|--|--|
| DIAGBOX_ssid | EB=a$Vz/KvDV+qU {jETm | WPA2-PSK |

Wifi password is limited by design to 21 characters and Wifi SSID to 12 characters

*Expected answer: 6EF020, returning 7F2278 during connection process*

### Step 5 - OTA Update Started, monitoring the progress

| Command | Detail |
|--|--|
| 22 F021 | WIFI AP connection state |
| 22 F022 | WIFI Internet connection state |
| 22 F023 | Download status |
| 22 F024 | Installation status |

