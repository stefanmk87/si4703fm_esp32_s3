# ESP32-S3 SI4703 FM Radio

A portable FM radio project built around an ESP32-S3 and the Silicon Labs SI4703 receiver. It uses an ILI9341 TFT for station and RDS information, physical buttons and an IR remote for control, and SPIFFS for band-scan logs. Wi-Fi and Bluetooth are not required.

## Features

- FM reception across the European 87.5-108.0 MHz band, with 100 kHz tuning steps and 50 µs deemphasis.
- RDS display for station name (PS), RadioText (RT), PI, PTY, TP/TA, Music/Speech, and broadcast clock when available.
- Signal strength in dBµV and Stereo/Mono status.
- Three display themes: Modern, Retro, and Classic. Classic is the default; the selected theme is saved in NVS.
- IR volume, seek, frequency-step, mute, theme, and reboot controls. IR codes are also printed to Serial for debugging/remapping.
- A slim volume overlay that appears when volume changes and hides automatically.
- FM band scans saved to SPIFFS, with scan results available through Serial.
- Frequency, volume, and display theme persist across reboots using ESP32 Preferences (NVS).
- SI4703 and TFT libraries are vendored in `lib/`, so the project libraries do not need to be downloaded separately.

## Hardware

| Component | Connection |
|---|---|
| ESP32-S3 to SI4703 SDA | GPIO4 |
| ESP32-S3 to SI4703 SCL | GPIO5 |
| ESP32-S3 to SI4703 RESET | GPIO6 |
| TFT MOSI / SCLK | GPIO41 / GPIO40 |
| TFT CS / DC / RESET | GPIO1 / GPIO42 / GPIO2 |
| Seek Up / Seek Down buttons | GPIO14 / GPIO15 to GND |
| Frequency Up / Frequency Down buttons | GPIO16 / GPIO17 to GND |
| VS1838B IR receiver OUT | GPIO18 |

Buttons are normally-open momentary switches wired between their GPIO and GND. The firmware enables internal pull-ups; no external button resistors are needed.

Power the ESP32-S3 from the board's supported 5 V input (`5VCC` or `VIN`). The SI4703 and TFT may use a separate regulated 3.3 V rail as supported by their breakout boards. All devices need a common ground. **Do not connect the external regulator's 3.3 V output to the ESP32-S3 3V3 pin.** Check the exact voltage requirements of your display breakout before powering it.

For the IR receiver, verify the pin order of the salvaged part before connecting it. The current wiring uses a 100 Ω series resistor on VCC and 10 µF plus 100 nF capacitors in parallel between the filtered VCC node and GND, close to the receiver.

See [HARDWARE.md](HARDWARE.md) for wiring details and [emi_fitler.md](emi_fitler.md) for the power/EMI schematic.

## External Dipole Antenna

For improved reception with an external antenna, the matching **Si4703-external-antenna-mod.** board requires the hardware modification shown in [Si4703-external-antenna-mod.png](Si4703-external-antenna-mod.png). The drawing shows a 1 nF coupling capacitor (`C5`) in the external antenna feed to the SI4703 `ANT` input, plus a 270 nH inductor (`L1`) in the audio-jack ground path. In the PCB drawing, blue marks indicate cuts and purple marks indicate added solder connections. Follow the PDF's exact board-side routing before attaching the antenna.

This modification is specific to the Si470x-Eval v1.1 layout. **Do not copy its trace cuts or solder points onto a different SI4703 breakout** unless that board has been checked against its own schematic. The PDF shows the receiver-board modification; it does not provide a universal balanced-dipole connector or matching network.

As a starting point for an FM half-wave dipole, make the two equal arms about a quarter wavelength each:

```text
Arm length (cm) ~= 7500 / frequency (MHz)
```

That is about 76 cm per arm near 98 MHz. Telescopic elements can be adjusted equally for the stations/band of interest. Connect the feed and return according to the modified board's RF input arrangement; do not assume that an arbitrary GND point is automatically a suitable balanced dipole terminal.

## Software and Build

The project targets `esp32-s3-devkitc-1` with the Arduino framework. Open the project folder in VS Code with the PlatformIO extension, then build and upload the firmware:

```sh
pio run
pio run --target upload
```

The local libraries are stored under `lib/` and are detected by PlatformIO from the source includes. The project configuration uses `lib_ldf_mode = chain+`. PlatformIO may still need internet access on a fresh computer to install the ESP32 platform, framework, and toolchain; the four project libraries themselves are included locally.

The optional boot image is `data/family.jpg`. Upload the SPIFFS image after changing files in `data/`:

```sh
pio run --target uploadfs
```

Use the PlatformIO Serial Monitor at **115200 baud**:

```sh
pio device monitor
```

## Serial Commands

| Command | Action |
|---|---|
| `U` / `D` | Step frequency up / down |
| `S` / `s` | Seek up / down |
| `+` / `-` | Volume up / down |
| `F` / `N` | Scan the FM band with the scan mode marked ON / OFF |
| `L` | Print scan files stored in SPIFFS |
| `TYYYY-MM-DD HH:MM` | Set the local timestamp used for later scans |
| `R` | Cycle Modern → Retro → Classic themes |
| `I` | Run a 5-second raw GPIO18 test for IR receiver wiring |
| `0` | Print current frequency, RSSI, volume, and stereo status |
| `?` | Print the command list |

The scan covers 87.5-108.0 MHz in 100 kHz steps. A scan point is marked as a station candidate when AFC is valid and RSSI is at least 40 dBµV. Scan files are stored on the ESP32 in SPIFFS, not on the computer.

To download the scan files on Windows, close Serial Monitor and run PowerShell from the project folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\download_scans.ps1
```

The script uses COM4 by default and writes the files to the Desktop. The port can be overridden with `-PortName COM5`.

## IR Remote Mapping

The firmware uses Arduino-IRremote and prints a decoded line for every received frame, for example:

```text
[IR] Protocol=NEC Address=0x80 Command=0x40 Raw-Data=0xBF407F80 ...
```

Current mappings are defined near the top of `src/main.cpp`:

| Action | Code |
|---|---|
| Volume Up | `0xB44BFE01` |
| Volume Down | `0xB04FFE01` |
| Seek Right / Up | `0xF807FE01` |
| Seek Left / Down | `0xB847FE01` |
| Frequency Up | `0xF609FE01` |
| Frequency Down | `0xFA05FE01` |
| Mute toggle | `0xBD42FE01` |
| Theme change | `0xC33CFE01` |
| Reboot | `0xE31CFE01` |

Remote models can send different codes. Use the Serial output to identify a button's `Raw-Data` value, then update its `IR_CODE_...` constant in `src/main.cpp`. Volume supports held-button repeats; seek and frequency changes ignore repeat frames to avoid repeated unintended actions.

## Saved Settings

The last tuned frequency, receiver volume, and display theme are stored in NVS. Frequency and volume writes are deferred until 2.5 seconds after the last change to reduce flash wear. Defaults are 92.90 MHz, volume 8, and the Classic theme.
