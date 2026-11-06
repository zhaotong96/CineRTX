# Security Policy

## Reporting a Vulnerability

Please report suspected vulnerabilities privately through GitHub Security Advisories:
https://github.com/zhaotong96/CineRTX/security/advisories/new

Please do not open public issues for security vulnerabilities.

## Scope & Operational Boundaries

This repository distributes the portable transcoding core, the CLI/GUI tools,
and the pure-C plugin ABI. It does not distribute NVIDIA proprietary SDK binaries;
issues originating in underlying driver or proprietary components should be reported to NVIDIA.

- **First-Party Runtime Scope**: Applies strictly to the portable C++ engine core, CLI, and native Win32 GUI code distributed within this repository.
- **External & Platform Boundaries**: Proprietary NVIDIA driver components and host operating system services (such as OS-level Windows Error Reporting / WER) execute outside the engine and remain governed by their respective vendor policies and local system configuration.

## Privacy & Local Diagnostics Disclosure

CineRTX is designed as a 100% self-contained, offline-first application:
- **Zero First-Party Telemetry & Zero Network Uploads**: The first-party engine runtime contains no telemetry, background network reporting, update checking, or tracking services. No media content, file paths, or diagnostic data are ever transmitted to any remote server by the application.
- **Local-Only Crash Dumps**: On Windows, when an unhandled fatal crash occurs, the system writes a standard `MiniDumpNormal` file locally to `%TEMP%` (`CineRTX_crash_<PID>_<TIMESTAMP>.dmp`) solely for local post-mortem debugging. First-party crash dumps remain strictly on the local machine and are never automatically uploaded by the application. (Subsequent OS-level crash reporting via Windows Error Reporting is controlled by host operating system group policy).
- **Diagnostic Output & User-Directed I/O**: Optional diagnostic logs and console probe outputs are generated only upon explicit operator request. File paths and output destinations (whether on local volumes or operator-configured network mounts/UNC shares) reflect explicit operator intent and are handled via standard OS filesystem APIs. Operators sharing logs or probe output in public issue trackers should review and redact directory paths as appropriate.
