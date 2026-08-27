# DogdouClix

A high-performance, lightweight (<150 KB) native Windows CLI interception, forwarding, and context-governance engine written in C++20.

DogdouClix acts as an execution proxy between callers and target executables, ensuring verbatim argument pass-through, real-time unbuffered I/O stream streaming, exit code fidelity, session/desktop inheritance, environment isolation/mutations, DPAPI-secured OS credential management, and cross-user context transitions.

---

## Key Features

- **Dual Operating Modes**:
  - **Explicit Forwarder**: Invoke target binaries directly with execution governance flags (`dogdouclix.exe --clix-cwd ... -- target.exe`).
  - **Transparent Proxy / Drop-in Shim**: Deploy as a drop-in binary replacement (via rename, hardlink, symlink, or higher-priority `PATH`), automatically stripping `argv[0]` and discovering downstream targets via PATH penetration.
- **Zero Parent Pollution**: All environment mutations (`--clix-env-set`, `--clix-env-remove`) are applied exclusively to the newly spawned child process via isolated double-null-terminated Unicode environment blocks.
- **Verbatim Argument Fidelity**: Full Windows command-line quoting roundtrip preservation conforming to `CommandLineToArgvW` rules.
- **Windows Credential Manager Integration**: Securely store and retrieve OS user credentials using `CRED_TYPE_GENERIC` backed by Windows DPAPI encryption with automatic in-memory buffer clearing (`SecureZeroMemory`).
- **Companion Configuration Scaffolding**: Built-in support for companion configurations in JSON and INI formats, including auto-scaffolding commands (`--clix-init`, `--clix-template`).
- **Stream Transparency & Signal Delegation**: Proper Win32 standard I/O handle inheritance (`STARTUPINFOEXW` handle lists) and graceful console signal delegation (`Ctrl+C`, `Ctrl+Break`).

---

## Quick Start

### 1. Explicit Execution

```powershell
# Forward command with working directory and injected environment variable
dogdouclix.exe --clix-cwd "D:\Workspace" --clix-env-set API_KEY=secret_123 -- git.exe status

# Run target executable under a registered Windows Credential Manager profile
dogdouclix.exe --clix-profile DeployAdmin python.exe deploy.py
```

### 2. Scaffold Companion Configuration

```powershell
# Generate a companion JSON configuration template (clix.json)
dogdouclix.exe --clix-init json

# Output companion INI template to stdout for piping
dogdouclix.exe --clix-template ini > app.clix.ini
```

### 3. Register Secure OS Credentials

```powershell
# Save an OS user credential profile in Windows Credential Manager (DPAPI encrypted)
dogdouclix.exe --clix-profile-set DeployAdmin `
  --clix-user DeployBot `
  --clix-domain CORP `
  --clix-password "SecurePassword123!"

# List registered profiles
dogdouclix.exe --clix-profile-list
```

---

## Building and Testing

### Prerequisites
- Windows 10/11 or Windows Server 2019/2022/2025
- Visual Studio 2022 (or Build Tools) with C++20 support
- CMake 3.25+

### Build & Run Test Suite

An automated root build script is provided:

```cmd
.\build.cmd
```

The script automatically detects the MSVC x64 toolchain, configures CMake, builds Release binaries (`dogdouclix_core.lib`, `dogdouclix.exe`, `dogdouclix_tests.exe`), and runs the comprehensive test suite with 100% verification.

---

## Documentation

- [User Guide & Architecture Reference](documents/USAGE_GUIDE.md): Complete CLI reference, companion configuration schema, target resolution, and security details.
- [Agent Guidelines](AGENTS.md): Repository development rules, code quality standards, and collaboration workflows.
