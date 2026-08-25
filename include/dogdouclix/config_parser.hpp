#pragma once

#include "dogdouclix/common.hpp"
#include "dogdouclix/context_transform.hpp"

namespace dogdouclix {

typedef struct _CLIX_COMPANION_CONFIG {
  std::optional<std::wstring> Target;
  std::vector<ENV_MUTATION> EnvMutations;
  std::optional<std::wstring> WorkingDirectory;
  std::optional<std::wstring> DesktopStation;
  std::optional<USER_CONTEXT_CONFIG> UserContext;
  bool PassthroughAllArgs{true};
} CLIX_COMPANION_CONFIG;

class ConfigParser {
public:
  // Parses a companion JSON configuration string
  static std::optional<CLIX_COMPANION_CONFIG> ParseJson(std::string_view JsonContent);

  // Parses a companion INI configuration string
  static std::optional<CLIX_COMPANION_CONFIG> ParseIni(std::string_view IniContent);

  // Parses a configuration file (auto-detecting JSON or INI by extension)
  static std::optional<CLIX_COMPANION_CONFIG> ParseFile(const std::wstring& FilePath);

public:
  ~ConfigParser() = default;
  ConfigParser() = default;
};

} // namespace dogdouclix