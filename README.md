# ESP32 E-Drum Trigger System

A high-performance electronic drum trigger system based on **ESP32-S3** with native USB MIDI support and **positional sensing** via TDOA (Time Difference of Arrival). Port of [Edrumulus](https://github.com/corrados/edrumulus) detection algorithms to ESP-IDF.

## Features

### Core Functionality
- **Native USB MIDI**: Direct USB MIDI communication using `esp_tinyusb` (Note On/Off + CC)
- **Positional Sensing**: TDOA + Amplitude Ratio hybrid algorithm for hit position (0-127)
- **Dual-Core Architecture**: ADC DMA on Core 0, DSP pipeline on Core 1
- **Advanced Detection**: Ported Edrumulus algorithms with 3-phase signal processing
- **Low Latency**: <5ms target from pad hit to MIDI output
- **Multi-Pad Support**: Up to 4 pads with individual dual-piezo configuration
- **Real-time Processing**: 8kHz ADC sampling with DMA + ring buffer

### Detection Pipeline
1. **ADC DMA** (Core 0): 8kHz continuous sampling, 2 channels, 12-bit
2. **Bandpass Filter**: 40-400Hz Butterworth IIR for noise reduction
3. **Rebound Detection**: Edge analysis, exponential decay, velocity validation
4. **Positional Sensing**: TDOA (Δt between piezo peaks) + Amplitude Ratio
5. **MIDI Output**: Note On + CC 16 (position) + Note Off

### Hardware Interface
- **Target**: ESP32-S3 development board
- **2 Piezo Inputs**: GPIO4 (piezo1), GPIO5 (piezo2) with analog frontend
- **User Interface**: Rotary encoder with push-button + boot button
- **Status Display**: Integrated addressable RGB LED (WS2812)
- **Connectivity**: USB-C for MIDI and power

## Quick Start

### Prerequisites
- ESP-IDF v5.5.1 or later
- ESP32-S3 DevKit (e.g., ESP32-S3-DevKitC-1)

### Build & Flash
```bash
cd esp32-drumpad
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

**Git Bash wrapper (Windows):** `scripts/build_idf.py` runs idf.py from Git
Bash (it scrubs the unsupported `MSYSTEM` env var and exports the IDF 5.5.1
toolchain environment):
```bash
/c/Users/Luis/.espressif/python_env/idf5.5_py3.13_env/Scripts/python.exe scripts/build_idf.py build
/c/Users/Luis/.espressif/python_env/idf5.5_py3.13_env/Scripts/python.exe scripts/build_idf.py -p COM24 flash monitor
```

**PowerShell scripts (Windows):**
```powershell
.\build.ps1              # Build only
.\flash.ps1              # Flash only
.\build-flash-monitor.ps1 # Build, flash & monitor
```

**Wokwi simulation:**
```powershell
.\build_wokwi.ps1        # Build without PSRAM for simulator
```
Then use `build/esp32-edrumulus.bin` in [Wokwi](https://wokwi.com/) with ESP32-S3.

**Testing without hardware:** the synthetic test mode injects fake piezo
hits into the pipeline (`test hit 100 64` over serial, or enable
`CONFIG_EDRUMULUS_SYNTHTEST_AUTOSTART`). See
**[docs/TESTING-SYNTHETIC.md](docs/TESTING-SYNTHETIC.md)**.

## Architecture

### Dual-Core Task Distribution

| Core | Task | Description |
|------|------|-------------|
| Core 0 | ADC DMA ISR | Fills ring buffer with 8kHz samples (2 channels) |
| Core 1 | DSP Task (`dsp_task`) | Consumes ring buffer, runs detection pipeline |
| Any | Main Loop | Event-driven, receives hit events, sends MIDI |

```
ADC DMA (Core 0) → Ring Buffer → DSP Task (Core 1) → Main Loop → MIDI
```

### Signal Processing Pipeline

```
Raw ADC (12-bit) → Normalize → Bandpass Filter (40-400Hz)
    → Edge Detector → Decay Analyzer → Velocity Validator
    → TDOA + Amp Ratio → Position → MIDI Note + CC
```

## Project Structure

```
esp32-drumpad/
├── main/                           # Main application
│   ├── esp32-edrumulus.c          # Application entry point
│   └── CMakeLists.txt             # Build config
├── components/                     # Modular components
│   ├── edrumulus_core/            # System init and management
│   ├── edrumulus_detection/       # ADC, DSP, TDOA, rebound detection
│   ├── edrumulus_midi/            # USB MIDI interface
│   ├── edrumulus_config/          # NVS configuration persistence
│   ├── edrumulus_console/         # Serial command interface
│   ├── edrumulus_led/             # WS2812 RGB LED control
│   └── edrumulus_input/           # Rotary encoder + buttons
├── managed_components/             # IDF-managed (TinyUSB, LED strip)
├── ARQUITECTURA.md                # Full architecture document
├── sdkconfig.defaults             # ESP-IDF defaults
├── sdkconfig.wokwi                # Wokwi simulation config
├── build_wokwi.ps1                # Wokwi build helper
└── README.md                      # This file
```

## Pin Assignment (ESP32-S3)

| Signal | GPIO | Notes |
|--------|------|-------|
| Piezo 1 (ADC) | GPIO4 | ADC1_CH4, primary sensor |
| Piezo 2 (ADC) | GPIO5 | ADC1_CH5, secondary sensor |
| Status Status LED.*GPIO21 | WS2812 addressable RGB |
| Encoder A | GPIO1 | Rotary encoder |
| Encoder B | GPIO2 | Rotary encoder |
| Encoder Button | GPIO3 | Push-button |
| Boot Button | GPIO0 | System function (active low) |
| USB D- | GPIO19 | Native USB (fixed) |
| USB D+ | GPIO20 | Native USB (fixed) |

## MIDI Implementation

| Message | Channel | Data | Notes |
|---------|---------|------|-------|
| Note On | 10 (0-index: 9) | note, velocity | Hit detected |
| CC | 10 | CC 16, position | Position 0-127 |
| Note Off | 10 | note, 0 | 30ms after Note On |

## Console Commands

Connect via serial at 115200 baud:

```
help                           → Show all commands
show                           → Display current configuration
set <param> <value>            → Adjust parameter (e.g., set edge_threshold 180)
test <module>                  → Test module (edge, decay, velocity, adaptive, all)
test hit [vel] [pos]           → Synthetic hit on demand (no hardware needed)
test auto [interval_ms]        → Periodic synthetic hits (default 500 ms)
test stop                      → Stop synthetic test, restore real ADC
test status                    → Synthetic generator state
save [name]                    → Save config to NVS
load [name]                    → Load config from NVS
reset                          → Restore default values
```

## Development Status

### ✅ Completed
- [x] ADC continuous with DMA (8kHz, 2 channels)
- [x] Dual-core pinning (Core 0=ADC DMA, Core 1=DSP)
- [x] 40-400Hz bandpass Butterworth IIR filter
- [x] Phase 3 rebound detection (edge, decay, velocity, adaptive)
- [x] Positional sensing (TDOA + Amplitude Ratio hybrid)
- [x] USB MIDI Note On/Off + CC for position
- [x] Real-time console parameter adjustment
- [x] Synthetic test mode (signal generator + console control, #19-#21)
- [x] RGB LED status indication
- [x] Rotary encoder and button input
- [x] NVS configuration persistence
- [x] Wokwi simulation support

### 🚧 In Progress
- [ ] Latency optimization to <5ms
- [ ] ADC calibration with eFuse
- [ ] Hardware-in-the-loop validation

### 📋 Planned
- [ ] Multi-pad crosstalk cancellation
- [ ] PCB design and physical assembly
- [ ] Factory reset and calibration
- [ ] MIDI mapping customization

## Documentation

- **[ARQUITECTURA.md](ARQUITECTURA.md)** — Full system architecture, pipeline, hardware specs
- **[TESTING-SYNTHETIC.md](docs/TESTING-SYNTHETIC.md)** — Synthetic test mode runbook (validate the pipeline without hardware)
- **[CLAUDE.md](CLAUDE.md)** — Development guide for AI coding assistants
- **[CONSOLE_COMMANDS.md](CONSOLE_COMMANDS.md)** — Serial command reference

## References

- [Original Edrumulus project](https://github.com/corrados/edrumulus) by Volker Fischer
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)
- [TinyUSB MIDI specification](https://www.usb.org/hid)

## License

This project follows the same GPLv2 license as the original Edrumulus project.
