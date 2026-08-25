#pragma once

#include "dogdouclix/common.hpp"
#include "dogdouclix/context_transform.hpp"

namespace dogdouclix {

typedef struct _LAUNCH_CONFIG {
  std::wstring TargetExecutable;
  std::wstring FullCommandLine;
  std::optional<std::wstring> WorkingDirectory;
  std::optional<std::wstring> DesktopStation;
  std::vector<wchar_t> EnvironmentBlock;
  std::optional<USER_CONTEXT_CONFIG> UserContext;
  HANDLE UserToken{nullptr};
  bool DirectHandleInheritance{true};
} LAUNCH_CONFIG;

class ProcessLauncher {
public:
  // Launches target process with WinStation/Desktop inheritance and I/O handle pass-through
  static FORWARDING_RESULT LaunchAndForward(const LAUNCH_CONFIG& Config);

public:
  ~ProcessLauncher() = default;
  ProcessLauncher() = default;
};

} // namespace dogdouclix