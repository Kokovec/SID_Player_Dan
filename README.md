| **Phys Pin** | **GPIO** | **Signal** | **Connects to** |
| --- | --- | --- | --- |
| 1 | GP0 | D0 | SID data bit 0 |
| 2 | GP1 | D1 | SID data bit 1 |
| 4 | GP2 | D2 | SID data bit 2 |
| 5 | GP3 | D3 | SID data bit 3 |
| 6 | GP4 | D4 | SID data bit 4 |
| 7 | GP5 | D5 | SID data bit 5 |
| 9 | GP6 | D6 | SID data bit 6 |
| 10 | GP7 | D7 | SID data bit 7 |
| 11 | GP8 | A0 | SID address bit 0 |
| 12 | GP9 | A1 | SID address bit 1 |
| 14 | GP10 | A2 | SID address bit 2 |
| 15 | GP11 | A3 | SID address bit 3 |
| 16 | GP12 | A4 | SID address bit 4 |
| 17 | GP13 | ``/CS`` | SID chip select (active low) |
| 20 | GP15 | ``/RES`` | SID reset (active low) |
| 21 | GP16 | phi2 | SID clock ~985 kHz (PWM) |
| 22 | GP17 | SD ``/CS`` | SD card chip select |
| 24 | GP18 | SD SCK | SD card clock |
| 25 | GP19 | SD MOSI | SD card data in |
| 26 | GP20 | SD MISO | SD card data out |
| 27 | GP21 | NOT USED | NOT USED |
| 29 | GP22 | BTN | Next‑track button (active low) |
| 31 | GP26 | TFT SCK | Display SPI clock |
| 32 | GP27 | TFT SDA | Display SPI data |
| 34 | GP28 | TFT DC | Display data/command select |
| 3, 8, 13, 18, 23, 28, 33, 38 | GND | GND | Ground |
| 36 | 3V3 | 3.3 V | Power, TFT RES, TFT BL |
| 39 | VSYS | 5 V in | Main power input | <br><br>

| **SID Pin** | **Name** | **Description** |
| --- | --- | --- |
| 1 | CAP1A | Filter capacitor 1A |
| 2 | CAP1B | Filter capacitor 1B |
| 3 | CAP2A | Filter capacitor 2A |
| 4 | CAP2B | Filter capacitor 2B |
| 5 | ``/RES`` | Reset (active low) ← GP15 |
| 6 | phi2 | Clock input ~1 MHz ← GP16 |
| 7 | R/W | Read/Write (low = write) ← tied to GND |
| 8 | ``/CS`` | Chip select (active low) ← GP13 |
| 9 | A0 | Address bit 0 ← GP8 |
| 10 | A1 | Address bit 1 ← GP9 |
| 11 | A2 | Address bit 2 ← GP10 |
| 12 | A3 | Address bit 3 ← GP11 |
| 13 | A4 | Address bit 4 ← GP12 |
| 14 | GND | Ground |
| 15 | D0 | Data bit 0 ← GP0 |
| 16 | D1 | Data bit 1 ← GP1 |
| 17 | D2 | Data bit 2 ← GP2 |
| 18 | D3 | Data bit 3 ← GP3 |
| 19 | D4 | Data bit 4 ← GP4 |
| 20 | D5 | Data bit 5 ← GP5 |
| 21 | D6 | Data bit 6 ← GP6 |
| 22 | D7 | Data bit 7 ← GP7 |
| 23 | POTX | Potentiometer X (paddle) — unused |
| 24 | POTY | Potentiometer Y (paddle) — unused |
| 25 | VCC | +5 V supply |
| 26 | EXT_IN | External audio input — unused (tie to GND) |
| 27 | AUDIO OUT | Analog audio output |
| 28 | VDD | +12 V (6581) or +9 V (8580) |
