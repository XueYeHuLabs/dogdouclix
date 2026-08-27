#include "dogdouclix/context_transform.hpp"
#include <algorithm>

namespace dogdouclix {

namespace {

typedef struct _CASE_INSENSITIVE_LESS {
  bool operator()(const std::wstring& Lhs, const std::wstring& Rhs) const noexcept {
    return ::_wcsicmp(Lhs.c_str(), Rhs.c_str()) < 0;
  }
} CASE_INSENSITIVE_LESS;

} // namespace

std::vector<wchar_t> BuildEnvironmentBlock(
  const std::vector<ENV_MUTATION>& Mutations,
  HANDLE UserToken
) {
  std::map<std::wstring, std::wstring, CASE_INSENSITIVE_LESS> envmap;

  if (UserToken != nullptr) {
    LPVOID userenvblock = nullptr;
    if (::CreateEnvironmentBlock(&userenvblock, UserToken, FALSE)) {
      const wchar_t* curr = static_cast<const wchar_t*>(userenvblock);
      while (*curr != L'\0') {
        std::wstring entry(curr);
        size_t eqpos = entry.find(L'=');
        if (eqpos != std::wstring::npos && eqpos > 0) {
          envmap[entry.substr(0, eqpos)] = entry.substr(eqpos + 1);
        }
        curr += entry.size() + 1;
      }
      ::DestroyEnvironmentBlock(userenvblock);
    }
  } else {
    LPWCH currentenv = ::GetEnvironmentStringsW();
    if (currentenv != nullptr) {
      const wchar_t* curr = currentenv;
      while (*curr != L'\0') {
        std::wstring entry(curr);
        if (!entry.empty() && entry[0] != L'=') {
          size_t eqpos = entry.find(L'=');
          if (eqpos != std::wstring::npos && eqpos > 0) {
            envmap[entry.substr(0, eqpos)] = entry.substr(eqpos + 1);
          }
        }
        curr += entry.size() + 1;
      }
      ::FreeEnvironmentStringsW(currentenv);
    }
  }

  for (const auto& mut : Mutations) {
    if (mut.Key.empty()) {
      continue;
    }
    if (mut.Type == EnvMutationRemove) {
      envmap.erase(mut.Key);
    } else if (mut.Type == EnvMutationSet) {
      envmap[mut.Key] = mut.Value;
    }
  }

  std::vector<wchar_t> block;
  for (const auto& [k, v] : envmap) {
    std::wstring line = k + L"=" + v;
    block.insert(block.end(), line.begin(), line.end());
    block.push_back(L'\0');
  }
  block.push_back(L'\0');

  return block;
}

bool AcquireUserToken(
  const USER_CONTEXT_CONFIG& UserConfig,
  HANDLE* OutToken,
  std::string* OutError
) {
  if (OutToken == nullptr) {
    if (OutError != nullptr) {
      *OutError = "Invalid output token pointer.";
    }
    return false;
  }

  if (UserConfig.ExistingToken != nullptr && UserConfig.ExistingToken != INVALID_HANDLE_VALUE) {
    HANDLE duptoken = nullptr;
    if (::DuplicateTokenEx(
        UserConfig.ExistingToken,
        TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY,
        nullptr,
        SecurityImpersonation,
        TokenPrimary,
        &duptoken)) {
      *OutToken = duptoken;
      return true;
    }
    if (OutError != nullptr) {
      *OutError = "DuplicateTokenEx failed: " + GetLastErrorMessage();
    }
    return false;
  }

  if (UserConfig.Username.has_value() && !UserConfig.Username->empty()) {
    LPCWSTR user = UserConfig.Username->c_str();
    LPCWSTR domain = UserConfig.Domain.has_value() ? UserConfig.Domain->c_str() : nullptr;
    LPCWSTR pass = UserConfig.Password.has_value() ? UserConfig.Password->c_str() : L"";
    DWORD logontype = UserConfig.LogonType.value_or(LOGON32_LOGON_INTERACTIVE);

    HANDLE logontoken = nullptr;
    BOOL logonok = ::LogonUserW(
      user,
      domain,
      pass,
      logontype,
      LOGON32_PROVIDER_DEFAULT,
      &logontoken
    );

    if (logonok) {
      *OutToken = logontoken;
      return true;
    }

    if (OutError != nullptr) {
      *OutError = "LogonUserW failed for user '" + WideToUtf8(*UserConfig.Username) + "': " + GetLastErrorMessage();
    }
    return false;
  }

  if (OutError != nullptr) {
    *OutError = "No valid user credentials or existing token supplied.";
  }
  return false;
}

} // namespace dogdouclix