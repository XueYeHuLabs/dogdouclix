#include "dogdouclix/cred_manager.hpp"

namespace dogdouclix {

namespace {

constexpr const wchar_t* ProfilePrefix = L"dogdouclix:profile:";
constexpr size_t ProfilePrefixLen = 19; // wcslen(L"dogdouclix:profile:")

} // namespace

std::wstring CredManager::FormatTargetName(const std::wstring& ProfileName) {
  return std::wstring(ProfilePrefix) + ProfileName;
}

bool CredManager::SaveProfile(
  const CRED_PROFILE& Profile,
  std::string* OutError
) {
  if (Profile.Name.empty()) {
    if (OutError != nullptr) {
      *OutError = "Profile name cannot be empty.";
    }
    return false;
  }

  std::wstring targetname = FormatTargetName(Profile.Name);
  std::string passwordutf8;
  if (Profile.Password.has_value()) {
    passwordutf8 = WideToUtf8(*Profile.Password);
  }

  std::wstring usernamefull;
  if (Profile.Username.has_value()) {
    if (Profile.Domain.has_value() && !Profile.Domain->empty()) {
      usernamefull = *Profile.Domain + L"\\" + *Profile.Username;
    } else {
      usernamefull = *Profile.Username;
    }
  }

  CREDENTIALW cred{};
  cred.Type = CRED_TYPE_GENERIC;
  cred.TargetName = const_cast<LPWSTR>(targetname.c_str());
  cred.CredentialBlobSize = static_cast<DWORD>(passwordutf8.size());
  cred.CredentialBlob = passwordutf8.empty() ? nullptr : reinterpret_cast<LPBYTE>(const_cast<char*>(passwordutf8.data()));
  cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
  cred.UserName = usernamefull.empty() ? nullptr : const_cast<LPWSTR>(usernamefull.c_str());

  if (!::CredWriteW(&cred, 0)) {
    DWORD err = ::GetLastError();
    if (OutError != nullptr) {
      *OutError = "CredWriteW failed: " + GetLastErrorMessage(err);
    }
    return false;
  }

  return true;
}

std::optional<CRED_PROFILE> CredManager::GetProfile(
  const std::wstring& ProfileName,
  std::string* OutError
) {
  if (ProfileName.empty()) {
    if (OutError != nullptr) {
      *OutError = "Profile name cannot be empty.";
    }
    return std::nullopt;
  }

  std::wstring targetname = FormatTargetName(ProfileName);
  PCREDENTIALW cred = nullptr;

  if (!::CredReadW(targetname.c_str(), CRED_TYPE_GENERIC, 0, &cred) || cred == nullptr) {
    DWORD err = ::GetLastError();
    if (OutError != nullptr) {
      *OutError = "Profile '" + WideToUtf8(ProfileName) + "' not found in Credential Manager (" + GetLastErrorMessage(err) + ")";
    }
    return std::nullopt;
  }

  CRED_PROFILE profile;
  profile.Name = ProfileName;

  if (cred->UserName != nullptr && wcslen(cred->UserName) > 0) {
    std::wstring userstr(cred->UserName);
    size_t slashpos = userstr.find(L'\\');
    if (slashpos != std::wstring::npos) {
      profile.Domain = userstr.substr(0, slashpos);
      profile.Username = userstr.substr(slashpos + 1);
    } else {
      profile.Username = userstr;
    }
  }

  if (cred->CredentialBlob != nullptr && cred->CredentialBlobSize > 0) {
    std::string passraw(reinterpret_cast<const char*>(cred->CredentialBlob), cred->CredentialBlobSize);
    profile.Password = Utf8ToWide(passraw);
  }

  ::CredFree(cred);
  return profile;
}

bool CredManager::DeleteProfile(
  const std::wstring& ProfileName,
  std::string* OutError
) {
  if (ProfileName.empty()) {
    if (OutError != nullptr) {
      *OutError = "Profile name cannot be empty.";
    }
    return false;
  }

  std::wstring targetname = FormatTargetName(ProfileName);
  if (!::CredDeleteW(targetname.c_str(), CRED_TYPE_GENERIC, 0)) {
    DWORD err = ::GetLastError();
    if (OutError != nullptr) {
      *OutError = "CredDeleteW failed: " + GetLastErrorMessage(err);
    }
    return false;
  }

  return true;
}

std::vector<std::wstring> CredManager::ListProfiles(std::string* OutError) {
  std::vector<std::wstring> profiles;
  DWORD count = 0;
  PCREDENTIALW* creds = nullptr;

  std::wstring filter = std::wstring(ProfilePrefix) + L"*";
  if (!::CredEnumerateW(filter.c_str(), 0, &count, &creds) || creds == nullptr) {
    DWORD err = ::GetLastError();
    if (err == ERROR_NOT_FOUND) {
      return profiles;
    }
    if (OutError != nullptr) {
      *OutError = "CredEnumerateW failed: " + GetLastErrorMessage(err);
    }
    return profiles;
  }

  for (DWORD i = 0; i < count; ++i) {
    if (creds[i] != nullptr && creds[i]->TargetName != nullptr) {
      std::wstring target(creds[i]->TargetName);
      if (target.starts_with(ProfilePrefix)) {
        profiles.push_back(target.substr(ProfilePrefixLen));
      }
    }
  }

  ::CredFree(creds);
  return profiles;
}

} // namespace dogdouclix