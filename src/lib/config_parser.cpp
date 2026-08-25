#include "dogdouclix/config_parser.hpp"
#include "dogdouclix/cred_manager.hpp"
#include <fstream>
#include <sstream>
#include <cctype>

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
  } else if (Str[Pos] == '{') {
    ++Pos;
    int depth = 1;
    while (Pos < Str.size() && depth > 0) {
      if (Str[Pos] == '{') ++depth;
      else if (Str[Pos] == '}') --depth;
      ++Pos;
    }
  } else if (Str[Pos] == '[') {
    ++Pos;
    int depth = 1;
    while (Pos < Str.size() && depth > 0) {
      if (Str[Pos] == '[') ++depth;
      else if (Str[Pos] == ']') --depth;
      ++Pos;
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

  // Merge profile mutations before existing config mutations
  Config.EnvMutations.insert(
    Config.EnvMutations.begin(),
    prof->EnvMutations.begin(),
    prof->EnvMutations.end()
  );

  if (!Config.WorkingDirectory.has_value() && prof->WorkingDirectory.has_value()) {
    Config.WorkingDirectory = prof->WorkingDirectory;
  }

  if (!Config.DesktopStation.has_value() && prof->DesktopStation.has_value()) {
    Config.DesktopStation = prof->DesktopStation;
  }

  if (prof->Username.has_value() || prof->Password.has_value() || prof->Domain.has_value() || prof->LoadUserProfile) {
    if (!Config.UserContext.has_value()) {
      USER_CONTEXT_CONFIG ucfg{};
      ucfg.Username = prof->Username;
      ucfg.Domain = prof->Domain;
      ucfg.Password = prof->Password;
      ucfg.LoadUserProfile = prof->LoadUserProfile;
      Config.UserContext = ucfg;
    } else {
      if (!Config.UserContext->Username.has_value()) Config.UserContext->Username = prof->Username;
      if (!Config.UserContext->Domain.has_value()) Config.UserContext->Domain = prof->Domain;
      if (!Config.UserContext->Password.has_value()) Config.UserContext->Password = prof->Password;
      if (prof->LoadUserProfile) Config.UserContext->LoadUserProfile = true;
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
            if (*pkey == "name") {
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
          } else if (*ukey == "domain") {
            auto uval = ParseJsonString(JsonContent, pos);
            if (uval.has_value()) usercfg.Domain = Utf8ToWide(*uval);
          } else if (*ukey == "password") {
            auto uval = ParseJsonString(JsonContent, pos);
            if (uval.has_value()) usercfg.Password = Utf8ToWide(*uval);
          } else if (*ukey == "load_profile") {
            auto uval = ParseJsonBool(JsonContent, pos);
            if (uval.has_value()) usercfg.LoadUserProfile = *uval;
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
      } else if (key == "domain") {
        config.UserContext->Domain = Utf8ToWide(val);
      } else if (key == "password") {
        config.UserContext->Password = Utf8ToWide(val);
      } else if (key == "load_profile") {
        config.UserContext->LoadUserProfile = (val == "true" || val == "1" || val == "yes");
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

} // namespace dogdouclix