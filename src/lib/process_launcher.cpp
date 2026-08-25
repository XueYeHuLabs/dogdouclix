#include "dogdouclix/process_launcher.hpp"
#include <atomic>

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

  std::vector<HANDLE> inheritablehandles;
  if (Config.DirectHandleInheritance) {
    auto markinheritable = [&inheritablehandles](HANDLE Handle) {
      if (Handle != nullptr && Handle != INVALID_HANDLE_VALUE) {
        ::SetHandleInformation(Handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        inheritablehandles.push_back(Handle);
      }
    };

    markinheritable(hstdin);
    markinheritable(hstdout);
    markinheritable(hstderr);
  }

  STARTUPINFOEXW siex{};
  siex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
  if (Config.DirectHandleInheritance) {
    siex.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    siex.StartupInfo.hStdInput = hstdin;
    siex.StartupInfo.hStdOutput = hstdout;
    siex.StartupInfo.hStdError = hstderr;
  }

  std::wstring desktopstr;
  if (Config.DesktopStation.has_value()) {
    desktopstr = *Config.DesktopStation;
    siex.StartupInfo.lpDesktop = desktopstr.data();
  }

  SIZE_T attributelistsize = 0;
  ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attributelistsize);
  std::vector<BYTE> attributelistbuffer(attributelistsize);
  siex.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributelistbuffer.data());

  if (!::InitializeProcThreadAttributeList(siex.lpAttributeList, 1, 0, &attributelistsize)) {
    result.Succeeded = false;
    result.ErrorMessage = "Failed to initialize process attribute list: " + GetLastErrorMessage();
    result.ExitCode = 1;
    return result;
  }

  if (!inheritablehandles.empty()) {
    if (!::UpdateProcThreadAttribute(
        siex.lpAttributeList,
        0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inheritablehandles.data(),
        inheritablehandles.size() * sizeof(HANDLE),
        nullptr,
        nullptr)) {
      ::DeleteProcThreadAttributeList(siex.lpAttributeList);
      result.Succeeded = false;
      result.ErrorMessage = "Failed to set handle inheritance attribute: " + GetLastErrorMessage();
      result.ExitCode = 1;
      return result;
    }
  }

  std::vector<wchar_t> cmdlinebuffer(Config.FullCommandLine.begin(), Config.FullCommandLine.end());
  cmdlinebuffer.push_back(L'\0');

  LPCWSTR workdir = Config.WorkingDirectory.has_value() ? Config.WorkingDirectory->c_str() : nullptr;
  LPVOID envblock = Config.EnvironmentBlock.empty() ? nullptr : const_cast<wchar_t*>(Config.EnvironmentBlock.data());

  DWORD creationflags = CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
  PROCESS_INFORMATION pi{};

  BOOL inherithandles = Config.DirectHandleInheritance ? TRUE : FALSE;
  BOOL created = FALSE;
  if (Config.UserToken != nullptr) {
    created = ::CreateProcessAsUserW(
      Config.UserToken,
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
  } else {
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
  }

  ::DeleteProcThreadAttributeList(siex.lpAttributeList);

  if (!created) {
    DWORD err = ::GetLastError();
    result.Succeeded = false;
    result.ErrorMessage = "CreateProcess failed: " + GetLastErrorMessage(err);
    result.ExitCode = (err != 0) ? err : 1;
    return result;
  }

  _ChildProcessHandle.store(pi.hProcess, std::memory_order_release);
  ::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

  ::WaitForSingleObject(pi.hProcess, INFINITE);

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