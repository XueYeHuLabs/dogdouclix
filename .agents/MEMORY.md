# Project Memory: DogdouClix

This file records durable project-specific lessons, architectural invariants, and debugging tips for DogdouClix.

---

## 1. Core Invariants & Architecture Constraints

- **UTF-8 and Wide String Boundaries**:
  - Windows APIs (CreateProcessW, CredWriteW, GetCommandLineW) operate strictly on UTF-16 (std::wstring).
  - Internal serialization (JSON/INI files, Credential Blobs, error messages) uses UTF-8 (std::string).
  - Always use Utf8ToWide / WideToUtf8 in src/lib/common.cpp for boundaries.

- **Sensitive Credential Clearing**:
  - Any intermediate std::string or std::vector<char> holding plaintext passwords or credential blobs MUST be wiped with ::SecureZeroMemory(ptr, size) before leaving scope (src/lib/cred_manager.cpp).

- **Isolated Double-Null-Terminated Unicode Environment Blocks**:
  - BuildEnvironmentBlock produces a contiguous std::vector<wchar_t> formatted as KEY=VALUE\0KEY2=VALUE2\0\0.
  - Case-insensitive key comparison (CASE_INSENSITIVE_LESS) is required for Windows environment variables.
  - Never mutate the caller's parent environment; pass the block directly to CreateProcessW / CreateProcessAsUserW.

- **Standard I/O Handle Inheritance**:
  - DogdouClix uses STARTUPINFOEXW with PROC_THREAD_ATTRIBUTE_HANDLE_LIST.
  - Only valid standard I/O handles (STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE) are marked inheritable and placed in the attribute list.
  - Unrelated handles in the parent process are strictly excluded.

- **Console Signal Delegation**:
  - SetConsoleCtrlHandler intercepts Ctrl+C and Ctrl+Break and returns TRUE, allowing the target child process to receive the signal and manage its own termination without terminating the forwarder early.
  - Global child process handle is stored in std::atomic<HANDLE> with release/acquire semantics.

- **PATH Penetration Anti-Recursion**:
  - When resolving targets in transparent shim mode, PenetratePath normalizes all directory entries and strictly skips the directory where DogdouClix itself resides to prevent infinite recursive spawning.

---

## 2. Windows API & Toolchain Traps

- **Long Path Support**:
  - Dynamic buffer queries (calling API with size `0` to get required buffer capacity) must be used for `GetFullPathNameW` and `GetEnvironmentVariableW` instead of fixed `MAX_PATH` buffers.

- **Logon Types**:
  - Default logon type is `LOGON32_LOGON_INTERACTIVE`.
  - For service accounts or CI batch jobs lacking interactive logon permissions, `LOGON32_LOGON_BATCH` or `LOGON32_LOGON_SERVICE` must be specified via `--clix-logon-type` or `logon_type` config setting.

- **Exit Code Fidelity**:
  - Process exit codes (`DWORD`, 32-bit unsigned) must be cast to `int` via `std::memcpy` rather than `static_cast<int>` to safely preserve high-bit NTSTATUS error patterns (e.g. `0xC0000005`, `0xC0000409`) without signed overflow undefined behavior.

- **CreateProcessWithLogonW vs CreateProcessAsUserW (Code 1314)**:
  - `CreateProcessAsUserW` requires `SeAssignPrimaryTokenPrivilege` (`SE_ASSIGNPRIMARYTOKEN_NAME`), which standard non-service processes do not hold, causing `ERROR_PRIVILEGE_NOT_HELD` (code 1314).
  - For user credentials and Credential Manager profiles, `CreateProcessWithLogonW` (with `LOGON_WITH_PROFILE` and `STARTF_USESTDHANDLES`) must be used instead, delegating to the Secondary Logon service without requiring administrator privileges or UAC elevation.

- **Same-User Fastpath & Inter-Session Pipe Bridging**:
  - Console handles (`\Device\ConDrv`) cannot cross logon session boundaries. `CreateProcessWithLogonW` allocates a new console window by default unless standard handles are securable pipes.
  - When target user matches the current process identity (`IsCurrentUser`), DogdouClix uses `CreateProcessW` directly in the current console (zero popup, zero latency).
  - For cross-user logon, DogdouClix establishes an inter-session anonymous pipe bridge with `CREATE_NO_WINDOW` and asynchronous background I/O pumps to stream child output directly to the parent terminal.

- **Cross-User Console Encoding Translation (Unicode / OEM Mojibake Prevention)**:
  - When child stdout/stderr is piped into parent `dogdouclix`, modern CLI apps (like Go/Rust/C++ Unicode tools) output UTF-8, while Windows system utilities (like `cmd.exe`, `whoami`) output OEM/GBK.
  - If the parent destination is a Console (`GetConsoleMode` succeeds), raw `WriteFile` causes mojibake on Chinese characters if code pages mismatch.
  - DogdouClix uses a multi-stage smart decoder (`PumpStream`): it first validates UTF-8 decoding to `wchar_t` and writes via `WriteConsoleW`. If invalid UTF-8, it falls back to `GetConsoleOutputCP()` / `GetOEMCP()` -> `WriteConsoleW`, with multi-byte tail carryover buffering to prevent split character corruption. Raw byte `WriteFile` is preserved for file/pipe redirections.

- **PROC_THREAD_ATTRIBUTE_HANDLE_LIST Deduplication & Console Handle Exclusion (Code 87 / 0x80070057)**:
  - In Windows, `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` ONLY accepts genuine kernel object handles (e.g. `FILE_TYPE_DISK` or `FILE_TYPE_PIPE`).
  - Passing Console handles (`FILE_TYPE_CHAR`, e.g. `\Device\ConDrv` handles from interactive PowerShell/CMD) or passing duplicate handles causes `UpdateProcThreadAttribute` or `CreateProcessW` to fail with `ERROR_INVALID_PARAMETER` (code 87 / `0x80070057` / `2147942487`).
  - DogdouClix checks `GetFileType(hHandle)`: only `FILE_TYPE_DISK` and `FILE_TYPE_PIPE` are added to the attribute list. When the list is empty, standard `STARTUPINFOW` is used without `EXTENDED_STARTUPINFO_PRESENT`.
  - For `CreateProcessAsUserW`, if no `STARTUPINFOEXW` attribute list is initialized, `STARTUPINFOW` must be used without `EXTENDED_STARTUPINFO_PRESENT`, otherwise the API returns `ERROR_INVALID_PARAMETER` (code 87).

- **Cross-User Standard Input Streaming (`CancelSynchronousIo`)**:
  - In `CreateProcessWithLogonW` cross-user mode, input streaming from parent `hstdin` to `hpipeinwrite` runs on an asynchronous `PumpInputStream` background thread.
  - When the child process terminates, `CancelSynchronousIo(stdinpump.native_handle())` unblocks any pending synchronous `ReadFile` on parent stdin, allowing clean thread termination without deadlock.
