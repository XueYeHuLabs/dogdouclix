#include "dogdouclix/cred_manager.hpp"
#include "dogdouclix/config_parser.hpp"
#include <sstream>

namespace dogdouclix {

namespace {

constexpr const wchar_t* ProfilePrefix = L"dogdouclix:profile:";
constexpr size_t ProfilePrefixLen = 19; // wcslen(L"dogdouclix:profile:")

static std::string EscapeJsonString(std::string_view Str) {
  std::string result;
  for (char ch : Str) {
    if (ch == '\\') {
      result.append("\\\\");
    } else if (ch == '"') {
      result.append("\\\"");
    } else if (ch == '\n') {
      result.append("\\n");
    } else if (ch == '\r') {
      result.append("\\r");
    } else if (ch == '\t') {
      result.append("\\t");
    } else {
      result.push_back(ch);
    }
  }
  return result;
}

static std::string SerializeProfilePayload(const CRED_PROFILE& Profile) {
  std::ostringstream ss;
  ss << "{\n";
  if (Profile.WorkingDirectory.has_value()) {
    ss << "  \"cwd\": \"" << EscapeJsonString(WideToUtf8(*Profile.WorkingDirectory)) << "\",\n";
  }
  if (Profile.DesktopStation.has_value()) {
    ss << "  \"desktop\": \"" << EscapeJsonString(WideToUtf8(*Profile.DesktopStation)) << "\",\n";
  }

  if (Profile.Username.has_value() || Profile.Domain.has_value() || Profile.Password.has_value() || Profile.LoadUserProfile) {
    ss << "  \"user\": {\n";
    bool firstuser = true;
    if (Profile.Username.has_value()) {
      ss << "    \"username\": \"" << EscapeJsonString(WideToUtf8(*Profile.Username)) << "\"";
      firstuser = false;
    }
    if (Profile.Domain.has_value()) {
      if (!firstuser) ss << ",\n";
      ss << "    \"domain\": \"" << EscapeJsonString(WideToUtf8(*Profile.Domain)) << "\"";
      firstuser = false;
    }
    if (Profile.Password.has_value()) {
      if (!firstuser) ss << ",\n";
      ss << "    \"password\": \"" << EscapeJsonString(WideToUtf8(*Profile.Password)) << "\"";
      firstuser = false;
    }
    if (Profile.LoadUserProfile) {
      if (!firstuser) ss << ",\n";
      ss << "    \"load_profile\": true";
      firstuser = false;
    }
    ss << "\n  },\n";
  }

  ss << "  \"env_set\": {\n";
  bool firstset = true;
  for (const auto& mut : Profile.EnvMutations) {
    if (mut.Type == EnvMutationSet) {
      if (!firstset) ss << ",\n";
      ss << "    \"" << EscapeJsonString(WideToUtf8(mut.Key)) << "\": \"" << EscapeJsonString(WideToUtf8(mut.Value)) << "\"";
      firstset = false;
    }
  }
  ss << "\n  },\n";

  ss << "  \"env_remove\": [\n";
  bool firstrem = true;
  for (const auto& mut : Profile.EnvMutations) {
    if (mut.Type == EnvMutationRemove) {
      if (!firstrem) ss << ",\n";
      ss << "    \"" << EscapeJsonString(WideToUtf8(mut.Key)) << "\"";
      firstrem = false;
    }
  }
  ss << "\n  ]\n";
  ss << "}\n";
  return ss.str();
}

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
  std::string payload = SerializeProfilePayload(Profile);

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
  cred.CredentialBlobSize = static_cast<DWORD>(payload.size());
  cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(payload.data()));
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

  std::string payload;
  if (cred->CredentialBlob != nullptr && cred->CredentialBlobSize > 0) {
    payload.assign(reinterpret_cast<const char*>(cred->CredentialBlob), cred->CredentialBlobSize);
  }

  CRED_PROFILE profile;
  profile.Name = ProfileName;

  if (!payload.empty()) {
    auto parsed = ConfigParser::ParseJson(payload);
    if (parsed.has_value()) {
      profile.EnvMutations = parsed->EnvMutations;
      profile.WorkingDirectory = parsed->WorkingDirectory;
      profile.DesktopStation = parsed->DesktopStation;
      if (parsed->UserContext.has_value()) {
        profile.Username = parsed->UserContext->Username;
        profile.Domain = parsed->UserContext->Domain;
        profile.Password = parsed->UserContext->Password;
        profile.LoadUserProfile = parsed->UserContext->LoadUserProfile;
      }
    }
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