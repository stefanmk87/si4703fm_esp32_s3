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
| CS | GPIO1 | Chip select |
| RST | GPIO2 | Display reset |
| DC / RS | GPIO42 | Data / command select |
| MOSI / SDI | GPIO41 | SPI data to display |
| SCK / SCL | GPIO40 | SPI clock |
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
| FREQ UP | GPIO16 | GND | Steps the tuned frequency up (same as Serial `U`) |
| FREQ DOWN | GPIO17 | GND | Steps the tuned frequency down (same as Serial `D`) |

### IR remote receiver (VS1838B)

Used to control the radio with a salvaged remote instead of WiFi/Bluetooth (both are avoided here to keep RF noise away from the FM tuner). Verify the pin order on the actual module with a multimeter/datasheet before wiring — reversing VCC and GND can destroy it.

| VS1838B pin | ESP32-S3 GPIO / net | Notes |
|---|---:|---|
| OUT | GPIO18 | Digital IR data signal |
| GND | GND | Common ground |
| VCC | 3V3 (through filter below) | Needs clean 3.3 V, see filter |

**Required filter (do not skip):** VS1838B's internal AGC is sensitive to supply noise and can report phantom IR pulses from a noisy rail. Add, right at the receiver's pins:

- 100 Ω resistor in series on the VCC line.
- 10 µF electrolytic capacitor between VCC and GND, after the resistor (closest to the receiver).
- 100 nF ceramic capacitor in parallel with the 10 µF, same two points.

Keep the receiver and its wiring away from the MP2307 and the antenna, same rule as the other EMI-sensitive parts on this board (see [emi_fitler.md](emi_fitler.md)).

For wide-angle reception (not just pointed straight at it), mount the receiver with a clear, unobstructed view of the room rather than recessed in a narrow opening, and keep direct sunlight/fluorescent light off the lens (another common source of false IR triggers).

**Firmware / mapping the remote buttons:** the firmware uses the `Arduino-IRremote` library and decodes any protocol automatically (NEC, Sony, etc. — works with both the Android box remote and the small 3-button remote). Six button slots are supported: Volume Up/Down, Seek Up/Down, Frequency Up/Down.

1. Flash the firmware and open Serial Monitor.
2. Press a remote button. You'll see a line like `[IR] Protocol=NEC Address=0x0 Command=0x18 Raw-Data=0xE718FF00 ...`.
3. Copy the `Raw-Data=0x........` value for that button.
4. In `src/main.cpp`, find the six `constexpr uint32_t IR_CODE_...` constants near the top and paste the matching hex value (e.g. `IR_CODE_VOLUME_UP = 0xE718FF00;`) for each of the 6 actions.
5. Rebuild and re-upload. Repeat presses (holding a button) work for Volume; Seek/Frequency ignore IR repeat frames so a held button does not run away.

The SI4703 uses I2C on GPIO4/5. The display uses SPI on GPIO41/12. These are separate buses. The firmware configures the display pins and ILI9341 driver through `platformio.ini` and initializes the screen in landscape mode (`setRotation(1)`).

## ASCII Wiring Diagram

```text
                    ESP32-S3 DevKitC-1
                 +-----------------------+
 SI4703 SDA/SDIO |-----------------------| GPIO4
 SI4703 SCL/SCLK |-----------------------| GPIO5
 SI4703 RST      |-----------------------| GPIO6
                 |                       |
 TFT CS          |-----------------------| GPIO1
 TFT RST         |-----------------------| GPIO2
 TFT DC / RS     |-----------------------| GPIO42
 TFT MOSI / SDI  |-----------------------| GPIO41
 TFT SCK         |-----------------------| GPIO40
                 |                       |
 SI4703 VCC -----| 3V3                   |
 TFT VCC --------| 3V3 (if supported)   |
 SI4703 GND -----| GND                   |
 TFT GND --------| GND                   |
                 +-----------------------+

ESP32 GPIO14 ----[ SEEK UP button ]------ GND
ESP32 GPIO15 ----[ SEEK DOWN button ]---- GND
ESP32 GPIO16 ----[ FREQ UP button ]------ GND
ESP32 GPIO17 ----[ FREQ DOWN button ]---- GND

VS1838B OUT -----------------------------------------------GPIO18
VS1838B GND -------------------------------+-----------------GND
                                            |
                 3V3 --[100R]--+----------- VS1838B VCC
                                |
                           +----+----+
                           |         |
                        [10uF]    [100nF]
                           |         |
                           +----+----+
                                |
                               GND (same net as VS1838B GND above)

##Или ако нема место на плоча стави ги овие GPIO за екранот

TFT CS	        GPIO1	
TFT RST	        GPIO2	
TFT DC / RS 	GPIO42	
TFT MOSI / SDI	GPIO41	
TFT SCK / SCL	GPIO40	

build_flags =
    -DUSER_SETUP_LOADED
    -DILI9341_DRIVER=ооо
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

- The IR remote receiver on GPIO18 decodes any protocol and prints `[IR] Protocol=... Raw-Data=0x...` to Serial for every button press, to help you map new buttons. See the IR remote receiver section above for the mapping steps.
