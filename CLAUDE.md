# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## ESP32 E-Drum Trigger System

This is a high-performance electronic drum trigger system based on ESP32-S3 with native USB MIDI support, featuring ported Edrumulus detection algorithms. The system provides <10ms latency with advanced 3-phase signal processing.

## Build and Development Commands

### Environment Setup (Windows PowerShell)
```powershell
# Navigate to ESP-IDF directory and setup environment
& "C:\path\to\esp-idf\export.ps1"

# Navigate to project directory
cd I:\esp32\esp32-drumpad
```

### Quick Build Scripts (Recommended)
```powershell
# Use provided PowerShell scripts for faster iteration:
.\build.ps1              # Build only
.\flash.ps1              # Flash only
.\build-flash-monitor.ps1 # Complete build, flash & monitor
```

### Core Build Commands
```powershell
# Set target to ESP32-S3
idf.py set-target esp32s3

# Build the project
idf.py build

# Flash to device (COM9 configured)
idf.py flash

# Monitor serial output (115200 baud)
idf.py monitor

# Clean build
idf.py fullclean

# Configuration menu
idf.py menuconfig
```

### Combined Commands
```powershell
# Build and flash in one command
idf.py build flash

# Build, flash and monitor
idf.py build flash monitor

# Note: Use Ctrl+] to exit monitor, not Ctrl+C
```

## System Architecture

### Core Components
- **edrumulus_core**: System initialization, task management, and coordination
- **edrumulus_detection**: 3-phase signal processing algorithms (8kHz sampling, filtering, detection)
- **edrumulus_midi**: Native USB MIDI interface via TinyUSB
- **edrumulus_console**: Serial command interface for real-time parameter adjustment
- **edrumulus_config**: Configuration management with NVS persistence
- **edrumulus_led**: RGB LED status indication
- **edrumulus_input**: Rotary encoder input handling

### Signal Processing Pipeline (3-Phase Detection)
1. **Phase 1**: Basic ADC sampling (8kHz) and peak detection
2. **Phase 2**: 40-400Hz Butterworth bandpass filtering for noise reduction
3. **Phase 3**: Advanced detection algorithms:
   - Edge detection with rise/fall rate analysis
   - Exponential decay analyzer for piezo signal validation
   - Velocity validator with minimum threshold (15)
   - Adaptive threshold based on SNR and noise floor
   - Mechanical rebound detector for false trigger rejection

### FreeRTOS Task Architecture
- **Main Task**: System coordination and initialization
- **Detection Task**: Real-time signal processing at 8kHz
- **MIDI Task**: USB MIDI message handling
- **Input Task**: Rotary encoder and button handling
- **Console Task**: Serial command processing

## Hardware Configuration (ESP32-S3)

### Pin Assignments
- **ADC Channels**: GPIO4-9 (6 analog inputs for piezo sensors)
- **Rotary Encoder**: GPIO1 (A), GPIO2 (B), GPIO3 (Button)
- **Boot Button**: GPIO0 (system functions)
- **Status LED**: GPIO48 (WS2812 addressable RGB)
- **USB Native**: GPIO19 (D-), GPIO20 (D+) - fixed pins for USB MIDI

### ESP32-S3 Specific Settings
- **CPU Frequency**: 240MHz for maximum performance
- **SPIRAM**: Enabled (OCT mode, 80MHz)
- **USB OTG**: Native USB support enabled
- **TinyUSB**: Configured for MIDI (1 device)

## Console Command System

### Access
- **Port**: COM9 (configured in sdkconfig)
- **Baud Rate**: 115200
- **Tools**: PuTTY, Arduino IDE Serial Monitor, or `idf.py monitor`

### Key Commands
```
help                           # Show all commands
show                           # Display current configuration
set <param> <value>            # Adjust parameters dynamically
test <module>                  # Run individual module tests
save/load [name]               # Store/retrieve configurations from NVS
reset                          # Restore default values
```

### Parameter Categories
- **Edge Detector**: edge_threshold, edge_sensitivity, rise_rate
- **Decay Analyzer**: tau_min, tau_max, r_squared
- **Velocity Validator**: linearity, repeatability, dynamic_min/max
- **Adaptive Threshold**: snr_target, adaptation_time, stability

## Development Workflow

### Component Management Rules
- **CRITICAL**: Never modify `managed_components/` - these are IDF-managed
- **Custom Components**: Only place custom code in `components/` directory
- **TinyUSB**: Use only `managed_components/espressif__tinyusb` (official)

### Debugging and Monitoring
- Use `idf.py monitor` for real-time log viewing
- Debug level set to DEBUG in sdkconfig.defaults
- Console commands provide real-time parameter adjustment without recompiling

### Configuration Persistence
- Settings stored in NVS (Non-Volatile Storage)
- Use `save` command to persist parameter changes
- Use `load` command to retrieve stored configurations
- Default configuration automatically loaded on startup

## Build Configuration

### ESP-IDF Configuration (sdkconfig.defaults)
- Target: ESP32-S3
- USB OTG: Enabled for native USB MIDI
- TinyUSB: 1 MIDI device configured
- Serial Port: COM9, 921600 baud for programming
- CPU: 240MHz with performance optimization
- ADC: Continuous mode, ISR safe for real-time processing

### Dependencies
- **TinyUSB**: Managed via IDF component system
- **FreeRTOS**: Included with ESP-IDF
- **ESP32-S3**: Specific hardware optimizations enabled

## Testing and Validation

### Phase 3 Testing
Use console commands for real-time testing:
```
test edge       # Test edge detection algorithm
test decay      # Test decay analyzer
test velocity   # Test velocity validation
test adaptive   # Test adaptive threshold
test all        # Run complete system test
```

### Performance Monitoring
- Latency measurement shows <10ms response time
- CPU usage remains <80% at full load
- Memory usage <300KB SRAM
- Real-time oscilloscope validation completed

## Important Notes

### USB Port Usage
- **Native USB Port**: Connect computer for MIDI functionality
- **Programming Port**: Use for flashing/monitoring via USB-to-serial bridge
- **Do not confuse** - ESP32-S3 has separate USB modes

### Common Issues
- If TinyUSB fails: Check USB OTG configuration and disable USB Serial JTAG
- If monitor doesn't respond: Use Ctrl+] to exit, not Ctrl+C
- If build fails: Run `idf.py fullclean` and rebuild
- If MIDI not detected: Verify native USB connection, not programming port

### Reference Documentation
- ESP-IDF v5.4.1 documentation: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/index.html
- TinyUSB MIDI implementation included in managed_components
- Console commands detailed in CONSOLE_COMMANDS.md