# CLAUDE.md — ESP32 E-Drum Trigger System

## Build Commands

### Environment Setup (Windows PowerShell)
```powershell
# Activate ESP-IDF environment
& "i:\esp32\v5.5.1\esp-idf\export.ps1"
cd I:\esp32\esp32-drumpad
```

### Build & Flash
```powershell
idf.py build                    # Build firmware
idf.py flash                    # Flash to device (COM9)
idf.py monitor                  # Serial monitor (115200 baud, Ctrl+] to exit)
idf.py build flash monitor      # All-in-one

# Quick scripts (no export needed):
.\build.ps1
.\flash.ps1
.\build-flash-monitor.ps1
.\build_wokwi.ps1               # Build for Wokwi (PSRAM disabled)
```

### Clean Build
```powershell
idf.py fullclean
```

## Project Architecture

### Dual-Core Design
- **Core 0**: ADC DMA ISR → ring buffer (producer)
- **Core 1**: `dsp_task` → ring buffer → detection pipeline (consumer)
- **Main loop**: Event-driven, receives hit events → MIDI

### Component Map

| Component | Files | Purpose |
|-----------|-------|---------|
| `edrumulus_detection` | `edrumulus_detection.c/.h` | ADC continuous, ring buffer, DSP pipeline, TDOA, rebound detection, pad registry |
| `edrumulus_midi` | `edrumulus_midi.c/.h` | USB MIDI via TinyUSB (Note On/Off, CC) |
| `edrumulus_core` | `edrumulus_core.c/.h` | System init, NVS, status |
| `edrumulus_config` | `edrumulus_config.c/.h` | Pad config (dual-piezo), system config, NVS persistence |
| `edrumulus_console` | `edrumulus_console.c/.h` | Serial command interface |
| `edrumulus_led` | `edrumulus_led.c/.h` | WS2812 RGB LED |
| `edrumulus_input` | `edrumulus_input.c/.h` | Rotary encoder + buttons |

### Key Data Structures

```c
// Hit event with full positional data
typedef struct {
    uint8_t  channel;        // ADC channel (primary piezo)
    uint8_t  pad_id;         // Pad ID
    uint8_t  velocity;       // 0-127
    uint8_t  position;       // 0-127 (TDOA + Amp ratio)
    uint8_t  cc_position;    // MIDI CC for position (0=disabled)
    uint8_t  note;           // MIDI note
    uint32_t timestamp;      // µs
    bool     is_rimshot;
    uint16_t piezo1_raw;     // Peak from piezo1
    uint16_t piezo2_raw;     // Peak from piezo2
} edrumulus_hit_event_t;

// Pad configuration (dual-piezo)
typedef struct {
    uint8_t threshold;
    uint8_t sensitivity;
    uint8_t midi_note;
    uint8_t midi_note_rim;
    uint8_t midi_cc_position; // MIDI CC for position
    uint8_t curve;
    uint8_t piezo_ch_1;       // ADC channel for piezo1
    uint8_t piezo_ch_2;       // ADC channel for piezo2
    bool enable_rimshot;
    bool enable_crosstalk_cancel;
} edrumulus_pad_config_t;
```

## Detection Pipeline (in order)

1. `edrumulus_detection_get_sample()` — pop from ring buffer (non-blocking)
2. Normalize: `raw / 4095.0f` → `[0.0, 1.0]`
3. `edrumulus_detection_filter_process()` — 40-400Hz bandpass IIR
4. `edrumulus_rebound_detector_process()` — edge/decay/velocity analysis
5. TDOA: `Δt = t_peak2 - t_peak1` → position (0-127)
6. Amplitude ratio: `p2/(p1+p2)` → position fallback
7. Hybrid: TDOA 70% + Amp 30% (or Amp 100% when Δt < 50µs)
8. Low-pass EMA filter on position (α = 0.35)
9. Send `edrumulus_hit_event_t` to detection queue

## Positional Sensing Algorithm

```
Δt = peak_piezo2_time - peak_piezo1_time  (µs)
Δt ∈ [-3000µs, +3000µs] → position ∈ [0, 127]
  Δt > 0 → hit closer to piezo1
  Δt < 0 → hit closer to piezo2
  Δt ≈ 0 → center (position 64)

Hybrid:
  if |Δt| >= 50µs:  pos = TDOA * 0.7 + AmpRatio * 0.3
  if |Δt| < 50µs:   pos = AmpRatio * 1.0

EMA filter: filtered += 0.35 * (raw - filtered)
```

## Console Commands

```
help           Show all commands
show           Display configuration
set <p> <v>    Set parameter (edge_threshold, tau_min, linearity, etc.)
test <m>       Run module test (edge, decay, velocity, adaptive, all)
save [name]    Save to NVS
load [name]    Load from NVS
reset          Factory defaults
```

## Hardware (ESP32-S3 DevKitC-1)

| Signal | GPIO | ADC |
|--------|------|-----|
| Piezo 1 | 4 | ADC1_CH4 |
| Piezo 2 | 5 | ADC1_CH5 |
| LED | 21 | — |
| Encoder A | 1 | — |
| Encoder B | 2 | — |
| Encoder Btn | 3 | — |
| Boot | 0 | — |
| USB D- | 19 | Fixed |
| USB D+ | 20 | Fixed |

### Analog Frontend (per piezo)
- Bias: resistive divider to 1.65V (3.3V midpoint)
- Anti-alias: RC low-pass 10kΩ + 100nF
- Decoupling: 100nF at ADC input

## Testing

### Compile check
```bash
idf.py build    # Must pass 1117/1117 steps
```

### Wokwi simulation
```powershell
.\build_wokwi.ps1    # Build without PSRAM
# Upload build/esp32-edrumulus.bin to https://wokwi.com/ (ESP32-S3)
```

### Runtime validation
The `edrumulus_detection_adc_continuous_validate(500)` function checks:
- Sample rate ≈ 16k samples/s (2 channels × 8kHz)
- No ring buffer overflows
- Both channels (CH4=piezo1, CH5=piezo2) producing data

### HITL (hardware required)
1. Connect piezos to GPIO4 (piezo1) and GPIO5 (piezo2)
2. Flash firmware: `idf.py flash`
3. Monitor: `idf.py monitor`
4. Hit pad at different positions → verify position value in log

## Git Workflow

```bash
git checkout master
git pull origin master
git checkout -b issue-XX-descripcion
# ... implement ...
git add -A && git commit -m "feat(#XX): description"
git push origin issue-XX-descripcion
# Create PR via `gh pr create` or GitHub web
# PR merged with `gh pr merge --squash --delete-branch`
```

## Completed Issues (PRs)

| # | Branch | Description | PR |
|---|--------|-------------|----|
| 1 | `issue-01-migrar-adc-continuous` | ADC continuous with DMA | #12 |
| 3 | `issue-03-dual-core` | Dual-core pinning (Core 0=ADC, Core 1=DSP) | #13 |
| 4 | `issue-04-modelo-dual-piezo` | 2-piezo pad model + position field | #14 |
| 5 | `issue-05-tdoa` | TDOA positional sensing | #15 |
| 8 | `issue-08-midi-cc` | MIDI CC for hit position | #16 |
| 11 | `issue-11-docs` | Documentation update | #17 |
