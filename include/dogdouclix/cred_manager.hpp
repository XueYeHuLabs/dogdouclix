#pragma once

#include "dogdouclix/common.hpp"
#include "dogdouclix/context_transform.hpp"
#include <wincred.h>

namespace dogdouclix {

typedef struct _CRED_PROFILE {
  std::wstring Name;
  std::optional<std::wstring> Username;
  std::optional<std::wstring> Domain;
  std::optional<std::wstring> Password;
  std::vector<ENV_MUTATION> EnvMutations;
  std::optional<std::wstring> WorkingDirectory;
  std::optional<std::wstring> DesktopStation;
  bool LoadUserProfile{false};
} CRED_PROFILE;

class CredManager {
public:
  // Saves or updates a profile in Windows Credential Manager
  static bool SaveProfile(
    const CRED_PROFILE& Profile,
    std::string* OutError = nullptr
  );

  // Reads a profile from Windows Credential Manager
  static std::optional<CRED_PROFILE> GetProfile(
    const std::wstring& ProfileName,
    std::string* OutError = nullptr
  );

  // Deletes a profile from Windows Credential Manager
  static bool DeleteProfile(
    const std::wstring& ProfileName,
    std::string* OutError = nullptr
  );

  // Lists all dogdouclix profile names stored in Windows Credential Manager
  static std::vector<std::wstring> ListProfiles(
    std::string* OutError = nullptr
  );

  // Formats canonical target name for Windows Credential Manager
  static std::wstring FormatTargetName(const std::wstring& ProfileName);

public:
  ~CredManager() = default;
  CredManager() = default;
};

} // namespace dogdouclix