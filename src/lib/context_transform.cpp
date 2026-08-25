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

} // namespace dogdouclix