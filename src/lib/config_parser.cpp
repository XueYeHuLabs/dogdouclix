#include "dogdouclix/config_parser.hpp"
#include "dogdouclix/cred_manager.hpp"
#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>

namespace dogdouclix {

namespace {

static std::string Trim(std::string_view Str) {
  size_t start = 0;
  while (start < Str.size() && (Str[start] == ' ' || Str[start] == '\t' || Str[start] == '\r' || Str[start] == '\n')) {
    ++start;
  }
  size_t end = Str.size();
  while (end > start && (Str[end - 1] == ' ' || Str[end - 1] == '\t' || Str[end - 1] == '\r' || Str[end - 1] == '\n')) {
    --end;
  }
  return std::string(Str.substr(start, end - start));
}

static void SkipJsonWhitespace(std::string_view Str, size_t& Pos) {
  while (Pos < Str.size() && (Str[Pos] == ' ' || Str[Pos] == '\t' || Str[Pos] == '\r' || Str[Pos] == '\n')) {
    ++Pos;
  }
}

static bool MatchChar(std::string_view Str, size_t& Pos, char Expected) {
  SkipJsonWhitespace(Str, Pos);
  if (Pos < Str.size() && Str[Pos] == Expected) {
    ++Pos;
    return true;
  }
  return false;
}

static std::optional<std::string> ParseJsonString(std::string_view Str, size_t& Pos) {
  SkipJsonWhitespace(Str, Pos);
  if (Pos >= Str.size() || Str[Pos] != '"') {
    return std::nullopt;
  }
  ++Pos;
  std::string result;
  while (Pos < Str.size()) {
    char ch = Str[Pos++];
    if (ch == '"') {
      return result;
    }
    if (ch == '\\' && Pos < Str.size()) {
      char esc = Str[Pos++];
      switch (esc) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case '/': result.push_back('/'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        default: result.push_back(esc); break;
      }
    } else {
      result.push_back(ch);
    }
  }
  return std::nullopt;
}

static std::optional<bool> ParseJsonBool(std::string_view Str, size_t& Pos) {
  SkipJsonWhitespace(Str, Pos);
  if (Str.substr(Pos, 4) == "true") {
    Pos += 4;
    return true;
  }
  if (Str.substr(Pos, 5) == "false") {
    Pos += 5;
    return false;
  }
  return std::nullopt;
}

static void SkipJsonValue(std::string_view Str, size_t& Pos) {
  SkipJsonWhitespace(Str, Pos);
  if (Pos >= Str.size()) return;
  if (Str[Pos] == '"') {
    ParseJsonString(Str, Pos);
  } else if (Str[Pos] == '{' || Str[Pos] == '[') {
    ++Pos;
    int depth = 1;
    while (Pos < Str.size() && depth > 0) {
      if (Str[Pos] == '"') {
        ParseJsonString(Str, Pos);
      } else if (Str[Pos] == '{' || Str[Pos] == '[') {
        ++depth;
        ++Pos;
      } else if (Str[Pos] == '}' || Str[Pos] == ']') {
        --depth;
        ++Pos;
      } else {
        ++Pos;
      }
    }
  } else {
    while (Pos < Str.size() && Str[Pos] != ',' && Str[Pos] != '}' && Str[Pos] != ']') {
      ++Pos;
    }
  }
}

} // namespace

void ConfigParser::ResolveAndApplyProfile(CLIX_COMPANION_CONFIG& Config) {
  if (!Config.Profile.has_value() || Config.Profile->empty()) {
    return;
  }

  std::string err;
  auto prof = CredManager::GetProfile(*Config.Profile, &err);
  if (!prof.has_value()) {
    return;
  }

  // Populate OS identity credentials from Credential Manager
  if (!Config.UserContext.has_value()) {
    USER_CONTEXT_CONFIG ucfg{};
    ucfg.Username = prof->Username;
    ucfg.Domain = prof->Domain;
    ucfg.Password = prof->Password;
    Config.UserContext = ucfg;
  } else {
    if (!Config.UserContext->Username.has_value() && prof->Username.has_value()) {
      Config.UserContext->Username = prof->Username;
    }
    if (!Config.UserContext->Domain.has_value() && prof->Domain.has_value()) {
      Config.UserContext->Domain = prof->Domain;
    }
    if (!Config.UserContext->Password.has_value() && prof->Password.has_value()) {
      Config.UserContext->Password = prof->Password;
    }
  }
}

std::optional<CLIX_COMPANION_CONFIG> ConfigParser::ParseJson(std::string_view JsonContent) {
  size_t pos = 0;
  if (!MatchChar(JsonContent, pos, '{')) {
    return std::nullopt;
  }

  CLIX_COMPANION_CONFIG config;

  while (pos < JsonContent.size()) {
    SkipJsonWhitespace(JsonContent, pos);
    if (MatchChar(JsonContent, pos, '}')) {
      break;
    }

    auto keyopt = ParseJsonString(JsonContent, pos);
    if (!keyopt.has_value()) {
      break;
    }

    if (!MatchChar(JsonContent, pos, ':')) {
      break;
    }

    std::string key = *keyopt;
    if (key == "target" || key == "executable") {
      auto val = ParseJsonString(JsonContent, pos);
      if (val.has_value()) {
        config.Target = Utf8ToWide(*val);
      }
    } else if (key == "profile") {
      auto val = ParseJsonString(JsonContent, pos);
      if (val.has_value()) {
        config.Profile = Utf8ToWide(*val);
      } else if (MatchChar(JsonContent, pos, '{')) {
        while (pos < JsonContent.size() && !MatchChar(JsonContent, pos, '}')) {
          auto pkey = ParseJsonString(JsonContent, pos);
          if (pkey.has_value() && MatchChar(JsonContent, pos, ':')) {
            if (*pkey == "name" || *pkey == "profile") {
              auto pval = ParseJsonString(JsonContent, pos);
              if (pval.has_value()) config.Profile = Utf8ToWide(*pval);
            } else {
              SkipJsonValue(JsonContent, pos);
            }
          }
          MatchChar(JsonContent, pos, ',');
        }
      }
    } else if (key == "cwd" || key == "working_directory") {
      auto val = ParseJsonString(JsonContent, pos);
      if (val.has_value()) {
        config.WorkingDirectory = Utf8ToWide(*val);
      }
    } else if (key == "desktop") {
      auto val = ParseJsonString(JsonContent, pos);
      if (val.has_value()) {
        config.DesktopStation = Utf8ToWide(*val);
      }
    } else if (key == "env_set") {
      if (MatchChar(JsonContent, pos, '{')) {
        while (pos < JsonContent.size()) {
          SkipJsonWhitespace(JsonContent, pos);
          if (MatchChar(JsonContent, pos, '}')) break;
          auto envk = ParseJsonString(JsonContent, pos);
          if (!envk.has_value() || !MatchChar(JsonContent, pos, ':')) break;
          auto envv = ParseJsonString(JsonContent, pos);
          if (envv.has_value()) {
            config.EnvMutations.push_back({
              Utf8ToWide(*envk),
              Utf8ToWide(*envv),
              EnvMutationSet
            });
          }
          MatchChar(JsonContent, pos, ',');
        }
      }
    } else if (key == "env_remove") {
      if (MatchChar(JsonContent, pos, '[')) {
        while (pos < JsonContent.size()) {
          SkipJsonWhitespace(JsonContent, pos);
          if (MatchChar(JsonContent, pos, ']')) break;
          auto envk = ParseJsonString(JsonContent, pos);
          if (envk.has_value()) {
            config.EnvMutations.push_back({
              Utf8ToWide(*envk),
              L"",
              EnvMutationRemove
            });
          }
          MatchChar(JsonContent, pos, ',');
        }
      }
    } else if (key == "user") {
      if (MatchChar(JsonContent, pos, '{')) {
        USER_CONTEXT_CONFIG usercfg;
        while (pos < JsonContent.size()) {
          SkipJsonWhitespace(JsonContent, pos);
          if (MatchChar(JsonContent, pos, '}')) break;
          auto ukey = ParseJsonString(JsonContent, pos);
          if (!ukey.has_value() || !MatchChar(JsonContent, pos, ':')) break;
          if (*ukey == "username" || *ukey == "user") {
            auto uval = ParseJsonString(JsonContent, pos);
            if (uval.has_value()) usercfg.Username = Utf8ToWide(*uval);
          } else if (*ukey == "profile" || *ukey == "cred_profile") {
            auto uval = ParseJsonString(JsonContent, pos);
            if (uval.has_value()) config.Profile = Utf8ToWide(*uval);
          } else if (*ukey == "domain") {
            auto uval = ParseJsonString(JsonContent, pos);
            if (uval.has_value()) usercfg.Domain = Utf8ToWide(*uval);
          } else if (*ukey == "password") {
            auto uval = ParseJsonString(JsonContent, pos);
            if (uval.has_value()) usercfg.Password = Utf8ToWide(*uval);
          } else if (*ukey == "load_profile") {
            auto uval = ParseJsonBool(JsonContent, pos);
            if (uval.has_value()) usercfg.LoadUserProfile = *uval;
          } else if (*ukey == "logon_type" || *ukey == "logontype") {
            auto uval = ParseJsonString(JsonContent, pos);
            if (uval.has_value()) {
              std::string lt = *uval;
              std::transform(lt.begin(), lt.end(), lt.begin(), [](unsigned char C) {
                return static_cast<char>(std::tolower(C));
              });
              if (lt == "interactive") usercfg.LogonType = LOGON32_LOGON_INTERACTIVE;
              else if (lt == "batch") usercfg.LogonType = LOGON32_LOGON_BATCH;
              else if (lt == "service") usercfg.LogonType = LOGON32_LOGON_SERVICE;
              else if (lt == "network") usercfg.LogonType = LOGON32_LOGON_NETWORK;
              else if (lt == "network_cleartext") usercfg.LogonType = LOGON32_LOGON_NETWORK_CLEARTEXT;
              else if (lt == "new_credentials") usercfg.LogonType = LOGON32_LOGON_NEW_CREDENTIALS;
              else {
                try {
                  usercfg.LogonType = static_cast<DWORD>(std::stoul(lt));
                } catch (...) {}
              }
            } else {
              SkipJsonWhitespace(JsonContent, pos);
              size_t numstart = pos;
              while (pos < JsonContent.size() && std::isdigit(static_cast<unsigned char>(JsonContent[pos]))) {
                ++pos;
              }
              if (pos > numstart) {
                try {
                  usercfg.LogonType = static_cast<DWORD>(std::stoul(std::string(JsonContent.substr(numstart, pos - numstart))));
                } catch (...) {}
              }
            }
          } else {
            SkipJsonValue(JsonContent, pos);
          }
          MatchChar(JsonContent, pos, ',');
        }
        config.UserContext = usercfg;
      }
    } else {
      SkipJsonValue(JsonContent, pos);
    }

    MatchChar(JsonContent, pos, ',');
  }

  return config;
}

std::optional<CLIX_COMPANION_CONFIG> ConfigParser::ParseIni(std::string_view IniContent) {
  CLIX_COMPANION_CONFIG config;
  std::string cursection = "";
  std::string inistr(IniContent);
  std::istringstream stream(inistr);
  std::string line;

  while (std::getline(stream, line)) {
    std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed.starts_with(";") || trimmed.starts_with("#")) {
      continue;
    }

    if (trimmed.starts_with("[") && trimmed.ends_with("]")) {
      cursection = Trim(trimmed.substr(1, trimmed.size() - 2));
      continue;
    }

    size_t eqpos = trimmed.find('=');
    if (eqpos == std::string::npos) {
      continue;
    }

    std::string key = Trim(trimmed.substr(0, eqpos));
    std::string val = Trim(trimmed.substr(eqpos + 1));

    if (cursection == "target" || cursection.empty()) {
      if (key == "target" || key == "executable") {
        config.Target = Utf8ToWide(val);
      } else if (key == "profile") {
        config.Profile = Utf8ToWide(val);
      } else if (key == "cwd" || key == "working_directory") {
        config.WorkingDirectory = Utf8ToWide(val);
      } else if (key == "desktop") {
        config.DesktopStation = Utf8ToWide(val);
      }
    } else if (cursection == "profile") {
      if (key == "name" || key == "profile") {
        config.Profile = Utf8ToWide(val);
      }
    } else if (cursection == "env.set" || cursection == "env_set") {
      config.EnvMutations.push_back({
        Utf8ToWide(key),
        Utf8ToWide(val),
        EnvMutationSet
      });
    } else if (cursection == "env.remove" || cursection == "env_remove") {
      config.EnvMutations.push_back({
        Utf8ToWide(key),
        L"",
        EnvMutationRemove
      });
    } else if (cursection == "user") {
      if (!config.UserContext.has_value()) {
        config.UserContext = USER_CONTEXT_CONFIG{};
      }
      if (key == "username" || key == "user") {
        config.UserContext->Username = Utf8ToWide(val);
      } else if (key == "profile" || key == "cred_profile") {
        config.Profile = Utf8ToWide(val);
      } else if (key == "domain") {
        config.UserContext->Domain = Utf8ToWide(val);
      } else if (key == "password") {
        config.UserContext->Password = Utf8ToWide(val);
      } else if (key == "load_profile") {
        config.UserContext->LoadUserProfile = (val == "true" || val == "1" || val == "yes");
      } else if (key == "logon_type" || key == "logontype") {
        std::string lt = val;
        std::transform(lt.begin(), lt.end(), lt.begin(), [](unsigned char C) {
          return static_cast<char>(std::tolower(C));
        });
        if (lt == "interactive") config.UserContext->LogonType = LOGON32_LOGON_INTERACTIVE;
        else if (lt == "batch") config.UserContext->LogonType = LOGON32_LOGON_BATCH;
        else if (lt == "service") config.UserContext->LogonType = LOGON32_LOGON_SERVICE;
        else if (lt == "network") config.UserContext->LogonType = LOGON32_LOGON_NETWORK;
        else if (lt == "network_cleartext") config.UserContext->LogonType = LOGON32_LOGON_NETWORK_CLEARTEXT;
        else if (lt == "new_credentials") config.UserContext->LogonType = LOGON32_LOGON_NEW_CREDENTIALS;
        else {
          try {
            config.UserContext->LogonType = static_cast<DWORD>(std::stoul(val));
          } catch (...) {}
        }
      }
    }
  }

  return config;
}

std::optional<CLIX_COMPANION_CONFIG> ConfigParser::ParseFile(const std::wstring& FilePath) {
  std::ifstream file(FilePath, std::ios::binary);
  if (!file.is_open()) {
    return std::nullopt;
  }

  std::ostringstream ss;
  ss << file.rdbuf();
  std::string content = ss.str();

  std::optional<CLIX_COMPANION_CONFIG> parsed;
  if (FilePath.ends_with(L".ini")) {
    parsed = ParseIni(content);
  } else {
    parsed = ParseJson(content);
    if (!parsed.has_value()) {
      parsed = ParseIni(content);
    }
  }

  if (parsed.has_value()) {
    ResolveAndApplyProfile(*parsed);
  }

  return parsed;
}

std::string ConfigParser::GenerateTemplateJson() {
  return "{\n"
         "  \"$schema\": \"https://raw.githubusercontent.com/XueYeHuLabs/dogdouclix/main/schemas/clix.schema.json\",\n"
         "  \"target\": \"C:\\\\Windows\\\\System32\\\\notepad.exe\",\n"
         "  \"cwd\": \"C:\\\\\",\n"
         "  \"desktop\": \"winsta0\\\\default\",\n"
         "  \"user\": {\n"
         "    \"profile\": \"DeployAdmin\",\n"
         "    \"username\": \"TargetUser\",\n"
         "    \"domain\": \"\",\n"
         "    \"password\": \"\",\n"
         "    \"logon_type\": \"interactive\",\n"
         "    \"load_profile\": true\n"
         "  },\n"
         "  \"env_set\": {\n"
         "    \"CUSTOM_ENV_KEY\": \"SampleValue\"\n"
         "  },\n"
         "  \"env_remove\": [\n"
         "    \"AWS_SECRET_ACCESS_KEY\",\n"
         "    \"CALLER_PRIVATE_TOKEN\"\n"
         "  ]\n"
         "}\n";
}

std::string ConfigParser::GenerateTemplateIni() {
  return "; DogdouClix Companion Configuration Template (.clix.ini)\n"
         "\n"
         "[target]\n"
         "executable = C:\\Windows\\System32\\notepad.exe\n"
         "cwd = C:\\\n"
         "desktop = winsta0\\default\n"
         "\n"
         "[user]\n"
         "profile = DeployAdmin\n"
         "username = TargetUser\n"
         "domain = \n"
         "password = \n"
         "logon_type = interactive\n"
         "load_profile = true\n"
         "\n"
         "[env.set]\n"
         "CUSTOM_ENV_KEY = SampleValue\n"
         "\n"
         "[env.remove]\n"
         "AWS_SECRET_ACCESS_KEY = 1\n"
         "CALLER_PRIVATE_TOKEN = 1\n";
}

bool ConfigParser::WriteTemplateFile(
  const std::wstring& FilePath,
  std::string_view Format,
  std::string* ErrorMessage
) {
  std::string content;
  std::string formatlower(Format);
  std::transform(formatlower.begin(), formatlower.end(), formatlower.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  if (formatlower == "json") {
    content = GenerateTemplateJson();
  } else if (formatlower == "ini") {
    content = GenerateTemplateIni();
  } else {
    if (ErrorMessage != nullptr) {
      *ErrorMessage = "Unsupported configuration format: '" + std::string(Format) + "'. Supported formats are 'json' and 'ini'.";
    }
    return false;
  }

  std::ofstream out(FilePath, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    if (ErrorMessage != nullptr) {
      *ErrorMessage = "Failed to create or open file for writing: " + WideToUtf8(FilePath);
    }
    return false;
  }

  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!out.good()) {
    if (ErrorMessage != nullptr) {
      *ErrorMessage = "Failed to write content to file: " + WideToUtf8(FilePath);
    }
    return false;
  }

  return true;
}

} // namespace dogdouclix