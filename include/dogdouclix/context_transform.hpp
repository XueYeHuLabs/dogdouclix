#pragma once

#include "dogdouclix/common.hpp"

namespace dogdouclix {

typedef enum _ENV_MUTATION_TYPE {
  EnvMutationSet,
  EnvMutationRemove
} ENV_MUTATION_TYPE;

typedef struct _ENV_MUTATION {
  std::wstring Key;
  std::wstring Value;
  ENV_MUTATION_TYPE Type{EnvMutationSet};
} ENV_MUTATION;

typedef struct _USER_CONTEXT_CONFIG {
  std::optional<std::wstring> Username;
  std::optional<std::wstring> Domain;
  std::optional<std::wstring> Password;
  std::optional<DWORD> LogonType;
  HANDLE ExistingToken{nullptr};
  bool LoadUserProfile{false};
} USER_CONTEXT_CONFIG;

typedef struct _CONTEXT_TRANSFORM_OPTIONS {
  std::vector<ENV_MUTATION> EnvMutations;
  std::optional<std::wstring> WorkingDirectory;
  std::optional<std::wstring> DesktopStation;
  std::optional<USER_CONTEXT_CONFIG> UserContext;
} CONTEXT_TRANSFORM_OPTIONS;

// Generates an isolated double-null-terminated Unicode environment block
std::vector<wchar_t> BuildEnvironmentBlock(
  const std::vector<ENV_MUTATION>& Mutations,
  HANDLE UserToken = nullptr
);

// Logs on a user security token based on USER_CONTEXT_CONFIG
bool AcquireUserToken(
  const USER_CONTEXT_CONFIG& UserConfig,
  HANDLE* OutToken,
  std::string* OutError = nullptr
);

} // namespace dogdouclix