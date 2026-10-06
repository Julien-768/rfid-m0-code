# RFID-M0

Firmware for the **RFID-M0** autonomous monitoring device developed by the MIBE technical platform at IPHC.

This repository contains the embedded software, drivers, hardware abstraction, tests, and build configuration required to develop and program the RFID-M0 device.

## Repository

The **GitLab repository is the primary project repository**. The GitHub repository is maintained as a mirror.

- **GitLab:** <https://gitlab.in2p3.fr/rfid-m0/rfid.m0.code>
- **GitHub:** <https://github.com/Julien-768/rfid-m0-code>

## Overview

RFID-M0 is an autonomous embedded system designed for long-term monitoring and data acquisition.

The firmware manages the device's sensors, RFID reader, infrared detection, real-time clock, SD card storage, power management, configuration, and low-power operating modes.

## Hardware

The firmware targets the **Adafruit Feather M0** platform:

- MCU: ATSAMD21G18
- CPU: ARM Cortex-M0+
- Clock: 48 MHz
- Logic level: 3.3 V
- Framework: Arduino
- Build system: PlatformIO

Additional hardware includes sensors, RFID, infrared detection, RTC, SD storage, and power-management circuitry.

## Repository structure

```text
rfid.m0.code/
├── include/          # Header files
├── lib/              # Local libraries
├── src/              # Application and firmware source code
├── test/             # Unit tests
├── scripts/          # Build and utility scripts
├── sd/               # SD-card related files
├── platformio.ini    # PlatformIO configuration
├── Doxyfile          # Doxygen configuration
└── LICENSE.txt       # License
```

## Requirements

The main development tools are:

- [Visual Studio Code](https://code.visualstudio.com/)
- [PlatformIO](https://platformio.org/)
- Git

PlatformIO can be used either through the Visual Studio Code extension or through PlatformIO Core.

## Build

Clone the repository and build the firmware with PlatformIO:

```bash
git clone https://gitlab.in2p3.fr/rfid-m0/rfid.m0.code.git
cd rfid.m0.code
pio run
```

Specific PlatformIO environments can be selected with:

```bash
pio run -e <environment>
```

## Tests

Unit tests are implemented using the PlatformIO Unity test framework.

Run the tests with:

```bash
pio test
```

## Static analysis

Static analysis configurations are provided through PlatformIO.

For example:

```bash
pio check
```

The repository includes configurations for tools such as **clang-tidy** and **Cppcheck**.

## Documentation

The complete RFID-M0 documentation is maintained in a separate repository.

- **GitLab:** <https://gitlab.in2p3.fr/rfid-m0/rfid.m0.wiki>
- **GitHub:** <https://github.com/Julien-768/rfid-m0-wiki>
- **Online documentation:** <https://julien-768.github.io/rfid-m0-wiki/>

The documentation covers:

- device operation;
- configuration;
- hardware architecture;
- electronics;
- antennas;
- assembly;
- programming;
- manufacturing;
- testing and maintenance.

## Development

The GitLab repository is the primary development repository.

Changes to the firmware should be tested before being merged.

When a firmware change modifies documented behaviour, the corresponding documentation should also be updated in the `rfid.m0.wiki` repository.

## License

This project is distributed under the **GNU General Public License v3.0**.

See [`LICENSE.txt`](LICENSE.txt) for the complete license text.

## Project

**RFID-M0**
MIBE Technical Platform — IPHC
