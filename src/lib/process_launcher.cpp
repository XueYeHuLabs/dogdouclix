#include "dogdouclix/process_launcher.hpp"
#include <atomic>
#include <thread>
#include <algorithm>
#include <vector>

namespace dogdouclix {

namespace {

static std::atomic<HANDLE> _ChildProcessHandle{nullptr};

static BOOL WINAPI ConsoleCtrlHandler(DWORD CtrlType) {
  switch (CtrlType) {
  case CTRL_C_EVENT:
  case CTRL_BREAK_EVENT:
  case CTRL_CLOSE_EVENT:
  case CTRL_LOGOFF_EVENT:
  case CTRL_SHUTDOWN_EVENT:
    // Delegate signal lifecycle to the child process without terminating the forwarder early
    return TRUE;
  default:
    return FALSE;
  }
}

static bool IsCurrentUser(const USER_CONTEXT_CONFIG& UserConfig) {
  if (UserConfig.ExistingToken != nullptr && UserConfig.ExistingToken != INVALID_HANDLE_VALUE) {
    return false;
  }
  if (!UserConfig.Username.has_value() || UserConfig.Username->empty()) {
    return true;
  }

  wchar_t currentuser[256] = {0};
  DWORD usersize = 256;
  if (!::GetUserNameW(currentuser, &usersize)) {
    return false;
  }

  std::wstring targetuser = *UserConfig.Username;
  std::wstring targetdomain;
  if (UserConfig.Domain.has_value() && !UserConfig.Domain->empty()) {
    targetdomain = *UserConfig.Domain;
  }

  size_t slashpos = targetuser.find(L'\\');
  if (slashpos != std::wstring::npos) {
    targetdomain = targetuser.substr(0, slashpos);
    targetuser = targetuser.substr(slashpos + 1);
  }

  if (::_wcsicmp(targetuser.c_str(), currentuser) != 0) {
    return false;
  }

  if (!targetdomain.empty()) {
    wchar_t compname[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD compsize = MAX_COMPUTERNAME_LENGTH + 1;
    if (::GetComputerNameW(compname, &compsize)) {
      if (::_wcsicmp(targetdomain.c_str(), compname) != 0 &&
          targetdomain != L"." &&
          targetdomain != L"localhost") {
        return false;
      }
    }
  }

  return true;
}

static void PumpStream(HANDLE HSourcePipe, HANDLE HTargetDest) {
  if (HSourcePipe == nullptr || HTargetDest == nullptr || HTargetDest == INVALID_HANDLE_VALUE) {
    return;
  }

  DWORD consolemode = 0;
  bool isconsole = (::GetConsoleMode(HTargetDest, &consolemode) != FALSE);

  char rawbuffer[4096];
  DWORD bytesread = 0;
  DWORD byteswritten = 0;
  std::vector<char> carryover;

  while (::ReadFile(HSourcePipe, rawbuffer, sizeof(rawbuffer), &bytesread, nullptr) && bytesread > 0) {
    if (!isconsole) {
      // Raw verbatim pass-through for file and pipe destinations
      ::WriteFile(HTargetDest, rawbuffer, bytesread, &byteswritten, nullptr);
      continue;
    }

    // Console output path: Decode to UTF-16 and output via WriteConsoleW to preserve Unicode fidelity
    std::vector<char> data;
    if (!carryover.empty()) {
      data.insert(data.end(), carryover.begin(), carryover.end());
      carryover.clear();
    }
    data.insert(data.end(), rawbuffer, rawbuffer + bytesread);

    // 1. Try UTF-8 decoding
    int neededwide = ::MultiByteToWideChar(
      CP_UTF8,
      MB_ERR_INVALID_CHARS,
      data.data(),
      static_cast<int>(data.size()),
      nullptr,
      0
    );

    if (neededwide > 0) {
      std::vector<wchar_t> wbuf(neededwide);
      ::MultiByteToWideChar(
        CP_UTF8,
        0,
        data.data(),
        static_cast<int>(data.size()),
        wbuf.data(),
        neededwide
      );
      DWORD charswritten = 0;
      ::WriteConsoleW(HTargetDest, wbuf.data(), static_cast<DWORD>(wbuf.size()), &charswritten, nullptr);
    } else {
      // Check if failure is due to a multi-byte sequence split across chunk boundary
      DWORD err = ::GetLastError();
      if (err == ERROR_NO_UNICODE_TRANSLATION && data.size() >= 1) {
        bool resolvedtail = false;
        for (size_t trim = 1; trim <= 3 && trim < data.size(); ++trim) {
          int prefixwide = ::MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            data.data(),
            static_cast<int>(data.size() - trim),
            nullptr,
            0
          );
          if (prefixwide > 0) {
            std::vector<wchar_t> wbuf(prefixwide);
            ::MultiByteToWideChar(
              CP_UTF8,
              0,
              data.data(),
              static_cast<int>(data.size() - trim),
              wbuf.data(),
              prefixwide
            );
            DWORD charswritten = 0;
            ::WriteConsoleW(HTargetDest, wbuf.data(), static_cast<DWORD>(wbuf.size()), &charswritten, nullptr);
            carryover.assign(data.end() - trim, data.end());
            resolvedtail = true;
            break;
          }
        }
        if (resolvedtail) {
          continue;
        }
      }

      // 2. Fallback to active Console Output Code Page or OEM Code Page for native Windows utilities (e.g. GBK/CP936)
      UINT codepage = ::GetConsoleOutputCP();
      if (codepage == 0) {
        codepage = ::GetOEMCP();
      }
      int oemwide = ::MultiByteToWideChar(
        codepage,
        0,
        data.data(),
        static_cast<int>(data.size()),
        nullptr,
        0
      );
      if (oemwide > 0) {
        std::vector<wchar_t> wbuf(oemwide);
        ::MultiByteToWideChar(
          codepage,
          0,
          data.data(),
          static_cast<int>(data.size()),
          wbuf.data(),
          oemwide
        );
        DWORD charswritten = 0;
        ::WriteConsoleW(HTargetDest, wbuf.data(), static_cast<DWORD>(wbuf.size()), &charswritten, nullptr);
      } else {
        // Raw byte write fallback
        ::WriteFile(HTargetDest, data.data(), static_cast<DWORD>(data.size()), &byteswritten, nullptr);
      }
    }
  }

  // Flush any remaining carryover bytes
  if (isconsole && !carryover.empty()) {
    UINT codepage = ::GetConsoleOutputCP();
    if (codepage == 0) codepage = ::GetOEMCP();
    int oemwide = ::MultiByteToWideChar(codepage, 0, carryover.data(), static_cast<int>(carryover.size()), nullptr, 0);
    if (oemwide > 0) {
      std::vector<wchar_t> wbuf(oemwide);
      ::MultiByteToWideChar(codepage, 0, carryover.data(), static_cast<int>(carryover.size()), wbuf.data(), oemwide);
      DWORD charswritten = 0;
      ::WriteConsoleW(HTargetDest, wbuf.data(), static_cast<DWORD>(wbuf.size()), &charswritten, nullptr);
    }
  }
}

} // namespace

FORWARDING_RESULT ProcessLauncher::LaunchAndForward(const LAUNCH_CONFIG& Config) {
  FORWARDING_RESULT result;

  if (Config.FullCommandLine.empty()) {
    result.Succeeded = false;
    result.ErrorMessage = "Command line cannot be empty.";
    result.ExitCode = 1;
    return result;
  }

  HANDLE hstdin = ::GetStdHandle(STD_INPUT_HANDLE);
  HANDLE hstdout = ::GetStdHandle(STD_OUTPUT_HANDLE);
  HANDLE hstderr = ::GetStdHandle(STD_ERROR_HANDLE);

  std::vector<wchar_t> cmdlinebuffer(Config.FullCommandLine.begin(), Config.FullCommandLine.end());
  cmdlinebuffer.push_back(L'\0');

  LPCWSTR workdir = Config.WorkingDirectory.has_value() ? Config.WorkingDirectory->c_str() : nullptr;
  LPVOID envblock = Config.EnvironmentBlock.empty() ? nullptr : const_cast<wchar_t*>(Config.EnvironmentBlock.data());

  std::wstring desktopstr;
  if (Config.DesktopStation.has_value()) {
    desktopstr = *Config.DesktopStation;
  }

  // Determine whether we should use cross-user CreateProcessWithLogonW
  bool needscrosslogon = false;
  if (Config.UserContext.has_value() &&
      Config.UserContext->Username.has_value() &&
      !Config.UserContext->Username->empty() &&
      Config.UserContext->Password.has_value() &&
      !IsCurrentUser(*Config.UserContext)) {
    needscrosslogon = true;
  }

  PROCESS_INFORMATION pi{};
  BOOL created = FALSE;
  DWORD creationerror = 0;

  // Branch A: Cross-User Execution via CreateProcessWithLogonW + Inter-Session Pipe Bridge
  if (needscrosslogon) {
    LPCWSTR username = Config.UserContext->Username->c_str();
    LPCWSTR domain = (Config.UserContext->Domain.has_value() && !Config.UserContext->Domain->empty())
                     ? Config.UserContext->Domain->c_str() : nullptr;
    LPCWSTR password = Config.UserContext->Password->c_str();

    DWORD logonflags = Config.UserContext->LoadUserProfile ? LOGON_WITH_PROFILE : 0;
    DWORD creationflags = CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW;

    // Create permissive security descriptor for inter-session anonymous pipes
    SECURITY_DESCRIPTOR sd{};
    ::InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    ::SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE);

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = &sd;

    HANDLE hpipeoutread = nullptr;
    HANDLE hpipeoutwrite = nullptr;
    HANDLE hpipeerrread = nullptr;
    HANDLE hpipeerrwrite = nullptr;
    HANDLE hpipeinread = nullptr;
    HANDLE hpipeinwrite = nullptr;

    ::CreatePipe(&hpipeoutread, &hpipeoutwrite, &sa, 0);
    ::CreatePipe(&hpipeerrread, &hpipeerrwrite, &sa, 0);
    ::CreatePipe(&hpipeinread, &hpipeinwrite, &sa, 0);

    // Parent ends should not be inherited by child
    if (hpipeoutread != nullptr) ::SetHandleInformation(hpipeoutread, HANDLE_FLAG_INHERIT, 0);
    if (hpipeerrread != nullptr) ::SetHandleInformation(hpipeerrread, HANDLE_FLAG_INHERIT, 0);
    if (hpipeinwrite != nullptr) ::SetHandleInformation(hpipeinwrite, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(STARTUPINFOW);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = hpipeinread;
    si.hStdOutput = hpipeoutwrite;
    si.hStdError = hpipeerrwrite;

    if (!desktopstr.empty()) {
      si.lpDesktop = desktopstr.data();
    }

    created = ::CreateProcessWithLogonW(
      username,
      domain,
      password,
      logonflags,
      nullptr,
      cmdlinebuffer.data(),
      creationflags,
      envblock,
      workdir,
      &si,
      &pi
    );

    // Close child write/read ends in parent immediately so pipe pump reads reach EOF on process exit
    if (hpipeoutwrite != nullptr) ::CloseHandle(hpipeoutwrite);
    if (hpipeerrwrite != nullptr) ::CloseHandle(hpipeerrwrite);
    if (hpipeinread != nullptr) ::CloseHandle(hpipeinread);
    if (hpipeinwrite != nullptr) ::CloseHandle(hpipeinwrite);

    if (!created) {
      creationerror = ::GetLastError();
      if (hpipeoutread != nullptr) ::CloseHandle(hpipeoutread);
      if (hpipeerrread != nullptr) ::CloseHandle(hpipeerrread);
    } else {
      _ChildProcessHandle.store(pi.hProcess, std::memory_order_release);
      ::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

      // Start asynchronous I/O pump threads with smart Unicode / OEM encoding translation
      std::thread stdoutpump(PumpStream, hpipeoutread, hstdout);
      std::thread stderrpump(PumpStream, hpipeerrread, hstderr);

      ::WaitForSingleObject(pi.hProcess, INFINITE);

      if (stdoutpump.joinable()) stdoutpump.join();
      if (stderrpump.joinable()) stderrpump.join();

      if (hpipeoutread != nullptr) ::CloseHandle(hpipeoutread);
      if (hpipeerrread != nullptr) ::CloseHandle(hpipeerrread);
    }
  }
  // Branch B: User token handle provided -> Try CreateProcessWithTokenW then CreateProcessAsUserW
  else if (Config.UserToken != nullptr) {
    DWORD logonflags = (Config.UserContext.has_value() && Config.UserContext->LoadUserProfile) ? LOGON_WITH_PROFILE : 0;
    DWORD creationflags = CREATE_UNICODE_ENVIRONMENT;

    STARTUPINFOW si{};
    si.cb = sizeof(STARTUPINFOW);
    if (Config.DirectHandleInheritance) {
      si.dwFlags |= STARTF_USESTDHANDLES;
      si.hStdInput = hstdin;
      si.hStdOutput = hstdout;
      si.hStdError = hstderr;
    }
    if (!desktopstr.empty()) {
      si.lpDesktop = desktopstr.data();
    }

    created = ::CreateProcessWithTokenW(
      Config.UserToken,
      logonflags,
      nullptr,
      cmdlinebuffer.data(),
      creationflags,
      envblock,
      workdir,
      &si,
      &pi
    );

    if (!created) {
      STARTUPINFOEXW siex{};
      siex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
      if (Config.DirectHandleInheritance) {
        siex.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
        siex.StartupInfo.hStdInput = hstdin;
        siex.StartupInfo.hStdOutput = hstdout;
        siex.StartupInfo.hStdError = hstderr;
      }
      if (!desktopstr.empty()) {
        siex.StartupInfo.lpDesktop = desktopstr.data();
      }

      BOOL inherithandles = Config.DirectHandleInheritance ? TRUE : FALSE;
      DWORD asuserflags = CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
      created = ::CreateProcessAsUserW(
        Config.UserToken,
        nullptr,
        cmdlinebuffer.data(),
        nullptr,
        nullptr,
        inherithandles,
        asuserflags,
        envblock,
        workdir,
        &siex.StartupInfo,
        &pi
      );
      if (!created) {
        creationerror = ::GetLastError();
      }
    }

    if (created) {
      _ChildProcessHandle.store(pi.hProcess, std::memory_order_release);
      ::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
      ::WaitForSingleObject(pi.hProcess, INFINITE);
    }
  }
  // Branch C: Direct Execution in Current Session (Same-User Fastpath & Standard Forwarding)
  else {
    std::vector<HANDLE> inheritablehandles;
    if (Config.DirectHandleInheritance) {
      auto markinheritable = [&inheritablehandles](HANDLE Handle) {
        if (Handle != nullptr && Handle != INVALID_HANDLE_VALUE) {
          DWORD filetype = ::GetFileType(Handle);
          // Only actual kernel file/pipe/disk objects can be marked inheritable and placed in PROC_THREAD_ATTRIBUTE_HANDLE_LIST.
          // Console handles (FILE_TYPE_CHAR) are not kernel file objects and will cause UpdateProcThreadAttribute to return ERROR_INVALID_PARAMETER (87).
          if (filetype == FILE_TYPE_DISK || filetype == FILE_TYPE_PIPE) {
            ::SetHandleInformation(Handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
            if (std::find(inheritablehandles.begin(), inheritablehandles.end(), Handle) == inheritablehandles.end()) {
              inheritablehandles.push_back(Handle);
            }
          }
        }
      };

      markinheritable(hstdin);
      markinheritable(hstdout);
      markinheritable(hstderr);
    }

    if (!inheritablehandles.empty()) {
      STARTUPINFOEXW siex{};
      siex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
      if (Config.DirectHandleInheritance) {
        siex.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
        siex.StartupInfo.hStdInput = hstdin;
        siex.StartupInfo.hStdOutput = hstdout;
        siex.StartupInfo.hStdError = hstderr;
      }
      if (!desktopstr.empty()) {
        siex.StartupInfo.lpDesktop = desktopstr.data();
      }

      SIZE_T attributelistsize = 0;
      ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attributelistsize);
      std::vector<BYTE> attributelistbuffer(attributelistsize);
      siex.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributelistbuffer.data());

      if (::InitializeProcThreadAttributeList(siex.lpAttributeList, 1, 0, &attributelistsize)) {
        if (::UpdateProcThreadAttribute(
            siex.lpAttributeList,
            0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inheritablehandles.data(),
            inheritablehandles.size() * sizeof(HANDLE),
            nullptr,
            nullptr)) {
          DWORD creationflags = CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
          BOOL inherithandles = TRUE;
          created = ::CreateProcessW(
            nullptr,
            cmdlinebuffer.data(),
            nullptr,
            nullptr,
            inherithandles,
            creationflags,
            envblock,
            workdir,
            &siex.StartupInfo,
            &pi
          );
          if (!created) {
            creationerror = ::GetLastError();
          }
        }
        ::DeleteProcThreadAttributeList(siex.lpAttributeList);
      }
    }

    // Fallback or standard path when no explicit kernel file/pipe handles need isolation list
    if (!created && creationerror == 0) {
      STARTUPINFOW si{};
      si.cb = sizeof(STARTUPINFOW);
      if (Config.DirectHandleInheritance) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = hstdin;
        si.hStdOutput = hstdout;
        si.hStdError = hstderr;
      }
      if (!desktopstr.empty()) {
        si.lpDesktop = desktopstr.data();
      }

      DWORD creationflags = CREATE_UNICODE_ENVIRONMENT;
      BOOL inherithandles = Config.DirectHandleInheritance ? TRUE : FALSE;
      created = ::CreateProcessW(
        nullptr,
        cmdlinebuffer.data(),
        nullptr,
        nullptr,
        inherithandles,
        creationflags,
        envblock,
        workdir,
        &si,
        &pi
      );
      if (!created) {
        creationerror = ::GetLastError();
      }
    }

    if (created) {
      _ChildProcessHandle.store(pi.hProcess, std::memory_order_release);
      ::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
      ::WaitForSingleObject(pi.hProcess, INFINITE);
    }
  }

  if (!created) {
    result.Succeeded = false;
    result.ErrorMessage = "CreateProcess failed: " + GetLastErrorMessage(creationerror);
    result.ExitCode = (creationerror != 0) ? creationerror : 1;
    return result;
  }

  DWORD exitcode = 0;
  if (::GetExitCodeProcess(pi.hProcess, &exitcode)) {
    result.ExitCode = exitcode;
    result.Succeeded = true;
  } else {
    result.ExitCode = ::GetLastError();
    result.Succeeded = false;
    result.ErrorMessage = "Failed to get child process exit code: " + GetLastErrorMessage(result.ExitCode);
  }

  ::SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
  _ChildProcessHandle.store(nullptr, std::memory_order_release);

  ::CloseHandle(pi.hThread);
  ::CloseHandle(pi.hProcess);

  return result;
}

} // namespace dogdouclix