#pragma once

#include "dogdouclix/common.hpp"
#include "dogdouclix/context_transform.hpp"

namespace dogdouclix {

typedef struct _CLIX_COMPANION_CONFIG {
  std::optional<std::wstring> Target;
  std::optional<std::wstring> Profile;
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

  // Resolves profile references and merges profile secrets into configuration
  static void ResolveAndApplyProfile(CLIX_COMPANION_CONFIG& Config);

  // Generates a standard JSON companion configuration template
  static std::string GenerateTemplateJson();

  // Generates a standard INI companion configuration template
  static std::string GenerateTemplateIni();

  // Writes a generated template (JSON or INI) to the specified file path
  static bool WriteTemplateFile(
    const std::wstring& FilePath,
    std::string_view Format,
    std::string* ErrorMessage = nullptr
  );

public:
  ~ConfigParser() = default;
  ConfigParser() = default;
};

} // namespace dogdouclix