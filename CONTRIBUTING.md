# Contributing to RFID-M0

Thank you for your interest in contributing to RFID-M0.

RFID-M0 is an open-source scientific project designed to monitor the activity of small animal species using RFID identification, infrared sensors and timestamped data acquisition. The project combines embedded software, electronics and hardware documentation.

This document describes how to report issues, propose changes and contribute to the firmware.

## 1. Ways to contribute

Contributions are welcome in several areas:

- **Bug reports:** identify and document unexpected behavior.
- **Firmware improvements:** fix bugs, improve reliability or add features.
- **Hardware support:** improve sensor integration, hardware compatibility or power management.
- **Testing:** reproduce issues, validate fixes and test firmware on actual devices.
- **Documentation:** clarify configuration, installation, deployment or troubleshooting procedures.
- **Scientific reproducibility:** improve data integrity, timestamps, logging and acquisition reliability.

For significant changes, open an issue before starting implementation to discuss the proposed approach.

## 2. Before contributing

Before opening an issue or submitting a pull request:

1. Check the existing issues and pull requests for related work.
2. Read the project README and relevant technical documentation.
3. Confirm which hardware revision and firmware version are affected.
4. For a bug report, try to reproduce the problem and collect relevant logs.

If the issue concerns a specific device, include its hardware configuration and the conditions under which the problem occurs.

## 3. Development environment

The firmware is developed in C++ using PlatformIO and the Arduino framework.

The primary target is the Adafruit Feather M0 based on the ATSAMD21G18 microcontroller (ARM Cortex-M0+).

### Prerequisites

- Git
- Python and PlatformIO Core, or Visual Studio Code with the PlatformIO IDE extension
- The dependencies declared in the repository's PlatformIO configuration
- Compatible hardware for tests that require physical devices

### Clone the repository

```bash
git clone https://github.com/Julien-768/rfid-m0-code.git
cd rfid-m0-code
```

Open the project in Visual Studio Code with PlatformIO, or use the PlatformIO command-line interface.

### Build the firmware

From the repository root, run:

```bash
pio run
```

This builds the environments configured in `platformio.ini`.

If the project defines multiple environments, consult that file and select the appropriate target:

```bash
pio run -e <environment>
```

A contribution should not introduce compilation errors or unexplained warnings for the affected target.

## 4. Coding guidelines

Contributions should follow the existing coding style and architecture.

### General principles

- Keep changes focused and avoid unrelated refactoring.
- Prefer clear, descriptive names for functions, variables and constants.
- Use comments to explain non-obvious behavior and design decisions.
- Avoid duplicating functionality that already exists in the project.
- Keep hardware-specific behavior and configuration consistent with the existing abstractions.
- Avoid introducing dependencies unless they provide a clear benefit.
- Do not commit generated build artifacts, temporary files or local IDE settings unless explicitly required.

### Embedded software considerations

Firmware changes must take into account the constraints of a low-power embedded system.

In particular:

- Avoid unnecessary dynamic memory allocation.
- Keep interrupt service routines short and predictable.
- Use appropriate synchronization when sharing data between interrupt handlers and the main execution flow.
- Avoid blocking operations in acquisition and deployment loops unless explicitly justified.
- Consider sleep modes, wake-up sources and peripheral power states.
- Handle sensor, RTC, SD card and communication failures explicitly where applicable.
- Preserve timestamp integrity and the consistency of recorded data.
- Avoid introducing excessive logging or unnecessary power consumption in deployment mode.

Changes affecting power management, interrupt handling, timing, sensor acquisition or file writing should include an explanation of their potential impact.

## 5. Testing and validation

Testing requirements depend on the type and risk of the change.

### Build validation

At a minimum, compile the affected PlatformIO environment:

```bash
pio run
```

If only one environment is relevant, document which environment was tested.

### Hardware validation

Changes affecting firmware behavior should be tested on compatible hardware whenever practical.

Depending on the change, relevant checks may include:

- Startup and initialization.
- Sensor acquisition and data logging.
- RTC timekeeping and scheduled wake-ups.
- Infrared event detection and debounce behavior.
- RFID operation and activation modes.
- SD card access and daily file management.
- Battery monitoring and power management.
- Sleep, wake-up and recovery behavior.

Do not claim that hardware behavior has been tested if only compilation or code inspection was performed.

### Reporting test results

In the pull request description, state:

- Which build or tests were performed.
- Which hardware was used, if applicable.
- Which scenarios were verified.
- Any known limitations or tests that remain outstanding.

## 6. Commit messages

Write concise commit messages that describe the purpose of the change.

Examples:

```text
Fix RTC wake-up handling
Improve SD card error reporting
Document RFID activation modes
Add validation for sensor configuration
```

Keep commits focused so that changes are easy to review and trace.

## 7. Submitting a pull request

1. Create a branch for your change.
2. Implement the change and update the documentation where necessary.
3. Build the affected firmware target and perform relevant tests.
4. Commit your changes.
5. Push your branch to GitHub.
6. Open a pull request against the appropriate target branch.

The pull request should explain:

- **Purpose:** what problem is being addressed?
- **Changes:** what has been modified?
- **Validation:** what builds and tests were performed?
- **Impact:** are hardware, configuration, power consumption, data formats or existing deployments affected?

Link related issues when applicable.

Keep pull requests focused and reasonably small. Changes to persistent data formats, hardware interfaces or deployment behavior should receive particular attention during review.

## 8. Hardware and data integrity

RFID-M0 is intended for scientific data acquisition. Changes must preserve the reliability and interpretability of recorded observations.

Contributors should:

- Avoid silently changing existing data formats.
- Document changes to timestamps, field names, units or logging conventions.
- Preserve compatibility where practical, or explain migration requirements.
- Identify failure modes that could cause data loss or incorrect observations.
- Avoid publishing sensitive field-site information or identifiable research data in issues and logs.

When a change may affect previously collected data or deployed devices, describe the consequences clearly.

## 9. Dependencies and third-party code

Before adding a dependency or incorporating external code:

- Check its license and compatibility with the project license.
- Prefer established libraries suitable for the target platform.
- Document new dependencies and their purpose.
- Preserve required copyright and license notices.
- Avoid committing credentials, access tokens, private keys or other secrets.

Updates to hardware libraries and embedded dependencies should be validated against the relevant target.

## 10. Documentation

Update the relevant documentation when a contribution changes:

- Configuration parameters or defaults.
- Hardware connections or supported components.
- Build, flashing or deployment procedures.
- Firmware behavior or operating modes.
- Data formats, logging or troubleshooting procedures.

Documentation should distinguish confirmed behavior from assumptions and hardware-dependent limitations.

## 11. Issues and bug reports

When reporting a bug, provide as much relevant information as possible:

- Firmware version or commit identifier.
- Hardware revision and relevant peripherals.
- Build environment and toolchain, when relevant.
- Steps to reproduce the issue.
- Expected and observed behavior.
- Relevant logs or error messages.
- Whether the problem is reproducible.

Remove sensitive information from logs before publishing them.

For feature requests, explain the scientific or technical need, the expected behavior and any relevant constraints.

## 12. Review and acceptance

Pull requests are reviewed for correctness, maintainability, compatibility with the target hardware and consistency with the project's scientific objectives.

A contribution may require changes before it can be accepted. Reviewers may request additional tests, clarification, documentation or a different implementation.

Submitting a pull request does not guarantee acceptance or inclusion in a particular release.

## 13. License

By submitting a contribution, you agree that it may be distributed under the license applicable to the files you modify.

Please ensure that you have the right to contribute the submitted code, documentation and other materials.

Consult the repository's `LICENSE` file for the applicable terms.

## 14. Questions

For questions about development or proposed changes, use the GitHub Issues section:

<https://github.com/Julien-768/rfid-m0-code/issues>

Thank you for helping improve RFID-M0 and the reliability of its scientific data acquisition.
