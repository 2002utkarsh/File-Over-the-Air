# FOTA (Firmware Over-The-Air) Update System

A production-grade embedded firmware update system with MCUboot A/B partition swapping, AWS IoT Core integration, and cryptographic image verification.

## System Architecture

```
AWS IoT Core                    Embedded Linux Gateway              Embedded Device
┌──────────────────┐           ┌─────────────────────┐           ┌──────────────────┐
│  IoT Jobs        │──MQTT────▶│  Python Gateway     │──UART────▶│  nRF52840 DK     │
│  S3 (firmware)   │           │  SHA-256 verify     │  (shell)  │  Zephyr + MCUboot│
│  manifest.json   │           │  SemVer check       │           │  A/B image swap  │
└──────────────────┘           └─────────────────────┘           └──────────────────┘
```

## Features

- **MCUboot A/B Swap**: Atomic firmware updates with automatic rollback on power loss
- **AWS IoT Core Gateway**: MQTT-based update notifications via IoT Jobs
- **SHA-256 Verification**: Cryptographic integrity check before flashing
- **SemVer Dependencies**: Prevents incompatible firmware deployments
- **UART Shell**: Interactive command interface (`version`, `status`, `update`)
- **LED Heartbeat**: Visual status indicator
- **CI/CD Pipeline**: GitHub Actions builds and deploys on version tags

## Project Structure

```
file_update_system/
├── CMakeLists.txt                   # Zephyr build configuration
├── prj.conf                         # MCUboot + Shell + GPIO + UART config
├── boards/
│   ├── arduino_nano_33_ble.overlay  # A/B flash partition layout
│   ├── native_sim.conf              # Linux simulation config
│   └── native_sim.overlay           # Native sim device tree
├── gateway/
│   ├── main.py                      # IoT Core MQTT gateway + UART bridge
│   └── requirements.txt             # Python dependencies
├── scripts/
│   └── deploy_firmware.py           # S3 upload with SHA-256 + SemVer
└── src/
    └── main.cpp                     # Device firmware (MCUboot + Shell)
```

## Flash Partition Layout (nRF52840 — 1 MB)

| Partition | Address | Size | Purpose |
|-----------|---------|------|---------|
| `mcuboot` | `0x00000` | 48 KB | MCUboot bootloader |
| `image-0` | `0x0C000` | 232 KB | Primary application slot |
| `image-1` | `0x46000` | 232 KB | Secondary slot (staging area) |
| `scratch`  | `0x80000` | 16 KB | Swap scratch (power-loss recovery) |
| `storage`  | `0x84000` | 496 KB | LittleFS / NVS |

## Getting Started

### Prerequisites

- [Zephyr RTOS](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) with West
- Arduino Nano 33 BLE or nRF52840 DK
- Python 3.10+
- AWS account (for IoT Core + S3)

### Build Firmware

```powershell
cd zephyrproject
west build -b arduino_nano_33_ble ../file_update_system -p
west flash
```

### Run Gateway

```bash
# Set environment variables
export AWS_IOT_ENDPOINT="your-endpoint.iot.us-east-1.amazonaws.com"
export AWS_IOT_THING_NAME="fota-device"
export UART_PORT="/dev/ttyUSB0"  # or COM3 on Windows

# Install dependencies
pip install -r file_update_system/gateway/requirements.txt

# Run (auto-selects IoT Core or S3 fallback)
python file_update_system/gateway/main.py
```

### Deploy New Firmware

```bash
python file_update_system/scripts/deploy_firmware.py \
    --file build/zephyr/zephyr.bin \
    --bucket my-fota-bucket \
    --version 1.1.0 \
    --min-version 1.0.0

# Test without uploading
python file_update_system/scripts/deploy_firmware.py \
    --file build/zephyr/zephyr.bin \
    --bucket test \
    --version 1.1.0 \
    --dry-run
```

### Shell Commands (UART)

| Command | Description |
|---------|-------------|
| `version` | Print firmware version (`VERSION:x.y.z`) |
| `status` | Device status, MCUboot confirmation, uptime |
| `update confirm` | Confirm current image (prevent rollback) |
| `update reboot` | Reboot to apply pending update |

## Update Flow

1. **Deploy**: CI/CD or `deploy_firmware.py` uploads firmware + manifest (with SHA-256) to S3
2. **Notify**: AWS IoT Core sends MQTT job notification to gateway
3. **Download**: Gateway fetches binary from S3
4. **Verify**: SHA-256 hash check + SemVer compatibility validation
5. **Stage**: Binary written to device's secondary slot (`image-1`)
6. **Swap**: Device reboots → MCUboot atomically swaps `image-0` ↔ `image-1`
7. **Confirm**: New firmware calls `boot_write_img_confirmed()` on successful boot
8. **Rollback**: If confirmation fails, MCUboot auto-reverts on next reboot

## Build Statistics

- **Flash Usage**: ~31 KB / 232 KB primary slot
- **RAM Usage**: ~7.5 KB / 256 KB
- **Binary Size**: ~30.4 KB

## License

MIT License

## Author

**Utkarsh Gupta**
- GitHub: [@2002utkarsh](https://github.com/2002utkarsh)
- Repository: [File-Over-the-Air](https://github.com/2002utkarsh/File-Over-the-Air)
