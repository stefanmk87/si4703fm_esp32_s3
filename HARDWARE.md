# ESP32-S3 FM Radio Hardware

## Connections

Use the GPIO numbers below, not the header position numbers. Connect all grounds together.

### SI4703 FM receiver (I2C)

| SI4703 pin | ESP32-S3 GPIO | Notes |
|---|---:|---|
| SDA / SDIO | GPIO4 | I2C data |
| SCL / SCLK | GPIO5 | I2C clock |
| RST / RESET | GPIO6 | Radio reset |
| VCC | 3V3 | SI4703 is a 3.3 V device |
| GND | GND | Common ground |

### ILI9341 2.8-inch display (TFT_eSPI / SPI)

| Display pin | ESP32-S3 GPIO | Notes |
|---|---:|---|
| CS | GPIO10 | Chip select |
| RST | GPIO9 | Display reset |
| DC / RS | GPIO11 | Data / command select |
| MOSI / SDI | GPIO13 | SPI data to display |
| SCK / SCL | GPIO12 | SPI clock |
| MISO / SDO | Not connected | TFT_eSPI is configured with `TFT_MISO=-1`; display reads are unused |
| VCC | 3V3 | Check the display breakout's power requirements |
| GND | GND | Common ground |
| LED / BL | Per display module | Connect as specified by the module; no ESP32 GPIO is assigned in firmware |

### Physical seek buttons

Use normally-open momentary buttons. One terminal of each button connects to its GPIO; the other terminal connects to GND. Firmware enables the ESP32 internal pull-up resistors, so no external resistors are required.

| Button | ESP32-S3 GPIO | Other button terminal | Action |
|---|---:|---|---|
| SEEK UP | GPIO14 | GND | Seeks up to the next station |
| SEEK DOWN | GPIO15 | GND | Seeks down to the next station |

The SI4703 uses I2C on GPIO4/5. The display uses SPI on GPIO13/12. These are separate buses. The firmware configures the display pins and ILI9341 driver through `platformio.ini` and initializes the screen in landscape mode (`setRotation(1)`).

## ASCII Wiring Diagram

```text
                    ESP32-S3 DevKitC-1
                 +-----------------------+
 SI4703 SDA/SDIO |-----------------------| GPIO4
 SI4703 SCL/SCLK |-----------------------| GPIO5
 SI4703 RST      |-----------------------| GPIO6
                 |                       |
 TFT CS          |-----------------------| GPIO10
 TFT RST         |-----------------------| GPIO9
 TFT DC / RS     |-----------------------| GPIO11
 TFT MOSI / SDI  |-----------------------| GPIO13
 TFT SCK         |-----------------------| GPIO12
                 |                       |
 SI4703 VCC -----| 3V3                   |
 TFT VCC --------| 3V3 (if supported)   |
 SI4703 GND -----| GND                   |
 TFT GND --------| GND                   |
                 +-----------------------+

ESP32 GPIO14 ----[ SEEK UP button ]------ GND
ESP32 GPIO15 ----[ SEEK DOWN button ]---- GND

##Или ако нема место на плоча стави ги овие GPIO за екранот

TFT CS	        GPIO1	
TFT RST	        GPIO2	
TFT DC / RS 	GPIO42	
TFT MOSI / SDI	GPIO41	
TFT SCK / SCL	GPIO40	

build_flags =
    -DUSER_SETUP_LOADED
    -DILI9341_DRIVER
    -DTFT_MISO=-1
    -DTFT_MOSI=41
    -DTFT_SCLK=40
    -DTFT_CS=1
    -DTFT_DC=42
    -DTFT_RST=2
    -DLOAD_GLCD
    -DLOAD_FONT2
    -DLOAD_FONT4
    -DLOAD_FONT7


TFT MISO / SDO: not connected
Buttons are normally open and active low. Pressing a button connects its GPIO to GND.
All GND pins, including the button grounds, share a common ground.
```

## Radio Firmware

- Starts tuned to 92.9 MHz. Serial commands `u` / `d` step the frequency; `S` / `s` seek up or down; `+` / `-` change volume.
- GPIO14 and GPIO15 perform the same Seek Up / Seek Down actions as Serial `S` / `s`. Button input is debounced and triggers once per press.
- Displays the tuned frequency, the confirmed 8-character RDS Program Service name (PS), RadioText (RT), PI code, PTY program type, TP/TA traffic flags, Music/Speech flag, RDS clock when broadcast, RSSI in dBµV, and the receiver's Stereo/Mono status.
- PS is assembled from its four RDS segments and accepted after repeated complete readings. RDS groups with excessive block errors are ignored.
- `F` scans the FM band with the low-pass filter marked ON; `N` scans with it marked OFF. Each scan covers 87.5-108.0 MHz in 100 kHz steps and writes a table to SPIFFS as `/fm_filter_on.txt` or `/fm_filter_off.txt`. `L` lists the saved files in Serial Monitor.
- `TYYYY-MM-DD HH:MM` sets the local timestamp used in later scan files. `0` prints receiver status and `?` prints the command list.
- Candidate scan points require a valid AFC status and RSSI of at least 40 dBµV. RSSI is received RF signal strength, not audio loudness.

The scan files are stored in ESP32 flash (SPIFFS), not on the computer. Use `download_scans.ps1` after closing Serial Monitor to copy them to the Desktop.
