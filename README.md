# ESP32 E-Drum Trigger System

A high-performance electronic drum trigger system based on ESP32-S3 with native USB MIDI support, featuring ported Edrumulus detection algorithms.

## Features

### Core Functionality
- **Native USB MIDI**: Direct USB MIDI communication using `esp_tinyusb`
- **Advanced Detection**: Ported Edrumulus algorithms for accurate trigger detection
- **Low Latency**: <10ms response time from pad hit to MIDI output
- **Multi-Pad Support**: Up to 8 drum pads with individual configuration
- **Real-time Processing**: 8kHz ADC sampling with FreeRTOS task architecture

### Detection Capabilities
- **Advanced Peak Detection**: Multi-phase detection with velocity sensing
- **Phase 2 Filtering**: 40-400Hz bandpass filter for noise reduction
- **Phase 3 Algorithms**: Edge detection, decay analysis, and rebound rejection
- **Velocity Validation**: Intelligent velocity filtering (minimum 15)
- **Adaptive Thresholding**: Dynamic threshold based on SNR and noise level
- **Rebound Detection**: Mechanical bounce elimination with rise/fall rate analysis
- **Crosstalk Cancellation**: Eliminates false triggers between pads
- **Multiple Piezoelectrics**: Support for multiple sensors per pad

### Hardware Interface
- **Target**: ESP32-S3 development board
- **ADC Channels**: 6 analog inputs for piezoelectric sensors
- **User Interface**: Rotary encoder with push-button + boot button
- **Status Display**: Integrated addressable RGB LED
- **Connectivity**: USB-C for MIDI and power

## Project Structure

```
esp32-drumpad/
├── main/                           # Main application
│   ├── esp32-edrumulus.c          # Application entry point
│   └── CMakeLists.txt             # Main component build config
├── components/                     # Modular components
│   ├── edrumulus_core/            # Core system management
│   ├── edrumulus_midi/            # USB MIDI interface
│   ├── edrumulus_detection/       # Signal processing & detection
│   ├── edrumulus_config/          # Configuration management
│   ├── edrumulus_led/             # LED status control
│   └── edrumulus_input/           # User input handling
├── managed_components/             # IDF-managed components
│   ├── espressif__esp_tinyusb/    # Official TinyUSB component
│   └── espressif__tinyusb/        # TinyUSB library
├── sdkconfig.defaults             # ESP-IDF configuration
├── CMakeLists.txt                 # Project build configuration
└── README.md                      # This file
```

## Technical Specifications

### Hardware Requirements
- **MCU**: ESP32-S3 (Xtensa LX7, 240MHz, 512KB SRAM)
- **USB**: Native USB 1.1 Full-Speed (12 Mbps)
- **ADC**: 12-bit, up to 6 channels
- **GPIO**: Rotary encoder (3 pins) + boot button
- **LED**: WS2812 addressable RGB LED

### Software Stack
- **Framework**: ESP-IDF v5.4.1
- **RTOS**: FreeRTOS (included in ESP-IDF)
- **USB MIDI**: esp_tinyusb v1.7.6+
- **Build System**: CMake
- **Language**: C99

### Performance Targets
- **Latency**: <10ms (pad hit to MIDI output)
- **Sample Rate**: 8kHz ADC sampling
- **CPU Usage**: <80% at full load
- **Memory**: <300KB SRAM usage

### Detection Algorithm Phases

#### Phase 1: Basic Signal Processing
- 8kHz ADC sampling with DMA
- Real-time signal buffering
- Basic peak detection

#### Phase 2: Bandpass Filtering
- **Filter Type**: 2nd order Butterworth bandpass
- **Frequency Range**: 40-400Hz
- **Purpose**: Eliminates low-frequency noise and high-frequency artifacts
- **Implementation**: Real-time IIR filter with optimized coefficients

#### Phase 3: Advanced Detection Algorithms
- **Edge Detector**: Rise/fall rate analysis to distinguish real hits from mechanical bounces
- **Decay Analyzer**: Exponential decay model validation for authentic piezo signals
- **Velocity Validator**: Intelligent filtering with minimum threshold of 15
- **Adaptive Threshold**: Dynamic threshold adjustment based on SNR and noise floor
- **Rebound Detector**: Multi-criteria rejection of mechanical bounces and false triggers

**Current Status**: All phases implemented and validated with oscilloscope testing

## Development Status

### ✅ Completed
- [x] Project structure and build system
- [x] Core system initialization
- [x] USB MIDI framework integration
- [x] Modular component architecture
- [x] Basic configuration management
- [x] ESP32-S3 target configuration
- [x] **Phase 1**: Basic ADC sampling and signal processing
- [x] **Phase 2**: 40-400Hz bandpass filter implementation
- [x] **Phase 3**: Advanced detection algorithms
  - [x] Edge detection with rise/fall rate analysis
  - [x] Exponential decay analyzer
  - [x] Velocity validator (minimum threshold 15)
  - [x] Adaptive threshold system
  - [x] Mechanical rebound detector
- [x] Real-time signal processing at 8kHz
- [x] USB MIDI message handling
- [x] LED status indication
- [x] **Console Command System**: Real-time parameter adjustment
  - [x] Serial command interface (115200 baud)
  - [x] Dynamic parameter modification (set/show/reset)
  - [x] Individual module testing (edge/decay/velocity/adaptive)
  - [x] Configuration persistence (save/load to NVS)
  - [x] Interactive help system

### 🚧 In Progress
- [ ] Rotary encoder input processing
- [ ] Advanced configuration interface
- [ ] Multi-pad calibration system

### 📋 Planned
- [ ] Advanced detection features (rimshots, positional sensing)
- [ ] Crosstalk cancellation algorithms
- [ ] Multi-pad configuration system
- [ ] Performance optimization
- [ ] Factory reset and calibration
- [ ] MIDI mapping customization

## Build Instructions

### Prerequisites
- ESP-IDF v5.4.1 or later
- CMake 3.16 or later
- Python 3.8 or later

### Setup
1. **Install ESP-IDF**: Follow the [ESP-IDF Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/)

2. **Set up environment**:
   ```bash
   # Windows PowerShell
   & "path\to\esp-idf\export.ps1"
   
   # Linux/macOS
   . path/to/esp-idf/export.sh
   ```

3. **Configure project**:
   ```bash
   cd esp32-drumpad
   idf.py set-target esp32s3
   idf.py menuconfig  # Optional: customize configuration
   ```

4. **Build and flash**:
   ```bash
   idf.py build
   idf.py flash monitor
   ```
   
   **Note**: To exit the monitor, use `Ctrl+T` followed by `Ctrl+X` (not `Ctrl+C`).

## Configuration

### Default Settings
- **MIDI Channel**: 10 (drum channel)
- **Sample Rate**: 8000 Hz
- **USB VID/PID**: Espressif default
- **Log Level**: Debug (development)

### GPIO Pin Assignment (ESP32-S3)
- **ADC Channels**: GPIO4-9 (6 channels)
- **Rotary Encoder**: GPIO1 (A), GPIO2 (B), GPIO3 (Button)
- **Boot Button**: GPIO0 (dedicated)
- **Status LED**: GPIO48 (WS2812)

## Usage

### Basic Operation
1. Connect ESP32-S3 to computer via USB
2. System automatically appears as USB MIDI device
3. Connect piezoelectric sensors to ADC inputs (GPIO4-9)
4. Configure sensitivity using rotary encoder
5. Play drums - MIDI notes sent in real-time with <10ms latency

### Current Functionality
- **Real-time Detection**: Advanced 3-phase detection algorithm
- **Noise Filtering**: 40-400Hz bandpass filter eliminates unwanted frequencies
- **Rebound Rejection**: Mechanical bounce detection prevents false triggers
- **Velocity Sensitivity**: Accurate velocity mapping with minimum threshold
- **Status Indication**: RGB LED shows system status and signal levels

### Configuration Mode
1. Hold boot button during startup
2. Use rotary encoder to navigate settings
3. Press encoder button to select/confirm
4. Settings saved to NVS flash

### Console Command System
A comprehensive command-line interface is available for real-time parameter adjustment:

- **Serial Access**: Connect via COM port at 115200 baud
- **Dynamic Adjustment**: Modify detection parameters without reflashing firmware
- **Real-time Testing**: Execute individual module tests instantly
- **Configuration Persistence**: Save/load multiple configuration profiles
- **Interactive Help**: Built-in command documentation

**Key Commands**:
- `help` - Show all available commands
- `show` - Display current configuration
- `set <param> <value>` - Adjust parameters (e.g., `set edge_threshold 180`)
- `test <module>` - Run individual tests (edge, decay, velocity, adaptive, all)
- `save/load [name]` - Persist configurations to NVS
- `reset` - Restore default values

**See [CONSOLE_COMMANDS.md](CONSOLE_COMMANDS.md) for complete documentation.**

### Validation and Testing
- **Oscilloscope Validation**: Signal processing verified with oscilloscope captures
- **Real-time Monitoring**: Debug logs show detection decisions and rejected signals
- **Performance Metrics**: Consistent <10ms latency, accurate velocity detection

## Project Rules and Guidelines

### Component Management
- **⚠️ CRITICAL**: Never modify components in `managed_components/` directory
- **IDF-Managed Components**: Always use `managed_components/` when available (e.g., `espressif__tinyusb`)
- **Custom Components**: Place only in `components/` directory
- **Compatibility**: Prefer IDF-managed components for future compatibility and automatic updates
- **Conflicts**: Remove any duplicate components from `components/` if they exist in `managed_components/`

### Development Best Practices
- **Environment Setup**: Always run `export.ps1` before building
- **Port Configuration**: Use COM9 for ESP32-S3 DevKit
- **Monitor Exit**: Use `Ctrl+]` to exit monitor (if fails, close terminal completely)
- **Build Clean**: Use `idf.py fullclean` when switching configurations
- **Component Dependencies**: Let ESP-IDF manage component versions automatically

### USB Configuration
- **USB Ports**: Distinguish between programming port (UART) and native USB port (MIDI)
- **MIDI Device**: Connect to native USB port for MIDI functionality
- **Serial Debug**: Use programming port for console output and debugging
- **TinyUSB**: Use only the official `managed_components/espressif__tinyusb`

## Contributing

This project is based on the excellent [Edrumulus](https://github.com/corrados/edrumulus) project by Volker Fischer. The detection algorithms are being ported from Arduino to ESP-IDF while maintaining compatibility and performance.

## License

This project follows the same license as the original Edrumulus project. Please refer to the original project for licensing details.

## Acknowledgments

- **Volker Fischer** - Original Edrumulus project and algorithms
- **Espressif Systems** - ESP32-C3 and ESP-IDF framework
- **TinyUSB Project** - USB MIDI implementation