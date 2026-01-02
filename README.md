# FOTA (File Over-The-Air) Update System

A clean embedded firmware designed for Firmware Over-The-Air (FOTA) updates with AWS cloud integration.

## System Architecture

```
AWS Cloud (S3)
    ↓ HTTPS
Embedded Linux Gateway (Raspberry Pi, etc.)
    ↓ UART/SPI
Embedded Device (nrf72840 dk)
```

## Features

- **Version Tracking**: Simple versioning
- **UART Command Interface**: Query version remotely
- **LED Heartbeat**: Visual status indicator
- **Cross-Platform**: Builds for Arduino & native_sim
- **Minimal Footprint**: Only 30.4 KB (6.55% flash, 2.95% RAM)

## Project Structure

```
file_update_system/
├── CMakeLists.txt                   # Build configuration
├── prj.conf                         # Zephyr project config
├── boards/
│   ├── arduino_nano_33_ble.overlay  # Arduino flash partitions
│   └── native_sim.conf              # Linux testing config
└── src/
    └── main.cpp                     # FOTA firmware
```

### Prerequisites

- [Zephyr RTOS](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) installed
- Arduino Nano 33 BLE or compatible board
- West build tool

### Build for Arduino

```powershell
# Navigate to Zephyr workspace
cd <zephyr_workspace>

# Build firmware
west build -b arduino_nano_33_ble file_update_system -p

# Flash to device
west flash
```

### Build for Linux (native_sim)

```bash
# Requires Linux host
west build -b native_sim file_update_system -p

# Run executable
./build/zephyr/zephyr.exe
```

## Build Statistics

- **Flash Usage**: 31,132 bytes / 464 KB (6.55%)
- **RAM Usage**: 7,552 bytes / 256 KB (2.95%)
- **Binary Size**: 30.4 KB

## Roadmap
- Basic firmware with version tracking
- UART command interface
- LED heartbeat
- AWS S3 integration for firmware storage
- Embedded Linux gateway application
- OTA update mechanism
- Firmware signature verification
- Delta/incremental updates

## License

MIT License

## Author

**Utkarsh Gupta**
- GitHub: [@2002utkarsh](https://github.com/2002utkarsh)
- Repository: [File-Over-the-Air](https://github.com/2002utkarsh/File-Over-the-Air)
