#pragma once

#include "dogdouclix/common.hpp"
#include "dogdouclix/config_parser.hpp"

namespace dogdouclix {

typedef struct _TARGET_RESOLUTION {
  std::wstring TargetExecutable;
  bool IsTransparentShim{false};
  std::optional<CLIX_COMPANION_CONFIG> LoadedConfig;
} TARGET_RESOLUTION;

class TargetResolver {
public:
  // Resolves self executable location and companion configuration
  static std::optional<TARGET_RESOLUTION> Resolve(
    const std::wstring& CustomExePath = L""
  );

  // Performs PATH penetration to find the next executable with given name skipping self directory
  static std::optional<std::wstring> PenetratePath(
    const std::wstring& ExeName,
    const std::wstring& SkipDir
  );

  // Gets the full normalized path of the current running executable
  static std::wstring GetCurrentExecutablePath();

  // Splits a file path into directory and file name
  static void SplitPath(
    const std::wstring& FullPath,
    std::wstring& OutDir,
    std::wstring& OutFilename,
    std::wstring& OutBasename
  );

  ~TargetResolver() = default;
  TargetResolver() = default;
};

} // namespace dogdouclix