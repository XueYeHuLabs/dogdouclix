#include "dogdouclix/config_parser.hpp"
#include <fstream>
#include <sstream>
#include <cctype>

namespace dogdouclix {

namespace {

static void SkipWhitespace(std::string_view Str, size_t& Pos) {
  while (Pos < Str.size() && (Str[Pos] == ' ' || Str[Pos] == '\t' || Str[Pos] == '\r' || Str[Pos] == '\n')) {
    ++Pos;
  }
}

static bool MatchChar(std::string_view Str, size_t& Pos, char Expected) {
  SkipWhitespace(Str, Pos);
  if (Pos < Str.size() && Str[Pos] == Expected) {
    ++Pos;
    return true;
  }
  return false;
}

static std::optional<std::string> ParseJsonString(std::string_view Str, size_t& Pos) {
  SkipWhitespace(Str, Pos);
  if (Pos >= Str.size() || Str[Pos] != '"') {
    return std::nullopt;
  }
  ++Pos;
  std::string result;
  while (Pos < Str.size() && Str[Pos] != '"') {
    if (Str[Pos] == '\\' && Pos + 1 < Str.size()) {
      ++Pos;
      switch (Str[Pos]) {
      case '"': result.push_back('"'); break;
      case '\\': result.push_back('\\'); break;
      case '/': result.push_back('/'); break;
      case 'b': result.push_back('\b'); break;
      case 'f': result.push_back('\f'); break;
      case 'n': result.push_back('\n'); break;
      case 'r': result.push_back('\r'); break;
      case 't': result.push_back('\t'); break;
      default: result.push_back(Str[Pos]); break;
      }
    } else {
      result.push_back(Str[Pos]);
    }
    ++Pos;
  }
  if (Pos < Str.size() && Str[Pos] == '"') {
    ++Pos;
    return result;
  }
  return std::nullopt;
}

static bool ParseJsonBool(std::string_view Str, size_t& Pos, bool& OutVal) {
  SkipWhitespace(Str, Pos);
  if (Str.substr(Pos, 4) == "true") {
    Pos += 4;
    OutVal = true;
    return true;
  }
  if (Str.substr(Pos, 5) == "false") {
    Pos += 5;
    OutVal = false;
    return true;
  }
  return false;
}

static void SkipJsonValue(std::string_view Str, size_t& Pos) {
  SkipWhitespace(Str, Pos);
  if (Pos >= Str.size()) {
    return;
  }
  if (Str[Pos] == '"') {
    ParseJsonString(Str, Pos);
  } else if (Str[Pos] == '{') {
    ++Pos;
    int depth = 1;
    while (Pos < Str.size() && depth > 0) {
      if (Str[Pos] == '{') ++depth;
      else if (Str[Pos] == '}') --depth;
      else if (Str[Pos] == '"') {
        ParseJsonString(Str, Pos);
        continue;
      }
      ++Pos;
    }
  } else if (Str[Pos] == '[') {
    ++Pos;
    int depth = 1;
    while (Pos < Str.size() && depth > 0) {
      if (Str[Pos] == '[') ++depth;
      else if (Str[Pos] == ']') --depth;
      else if (Str[Pos] == '"') {
        ParseJsonString(Str, Pos);
        continue;
      }
      ++Pos;
    }
  } else {
    while (Pos < Str.size() && Str[Pos] != ',' && Str[Pos] != '}' && Str[Pos] != ']') {
      ++Pos;
    }
  }
}

} // namespace

std::optional<CLIX_COMPANION_CONFIG> ConfigParser::ParseJson(std::string_view JsonContent) {
  size_t pos = 0;
  if (!MatchChar(JsonContent, pos, '{')) {
    return std::nullopt;
  }

  CLIX_COMPANION_CONFIG config;

  while (pos < JsonContent.size()) {
    SkipWhitespace(JsonContent, pos);
    if (pos < JsonContent.size() && JsonContent[pos] == '}') {
      ++pos;
      return config;
    }

    auto keyopt = ParseJsonString(JsonContent, pos);
    if (!keyopt.has_value() || !MatchChar(JsonContent, pos, ':')) {
      return std::nullopt;
    }

    std::string key = *keyopt;
    SkipWhitespace(JsonContent, pos);

    if (key == "target") {
      auto val = ParseJsonString(JsonContent, pos);
      if (val.has_value()) {
        config.Target = Utf8ToWide(*val);
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
          SkipWhitespace(JsonContent, pos);
          if (pos < JsonContent.size() && JsonContent[pos] == '}') {
            ++pos;
            break;
          }
          auto envkey = ParseJsonString(JsonContent, pos);
          if (envkey.has_value() && MatchChar(JsonContent, pos, ':')) {
            auto envval = ParseJsonString(JsonContent, pos);
            if (envval.has_value()) {
              config.EnvMutations.push_back({
                Utf8ToWide(*envkey),
                Utf8ToWide(*envval),
                EnvMutationSet
              });
            }
          }
          MatchChar(JsonContent, pos, ',');
        }
      } else {
        SkipJsonValue(JsonContent, pos);
      }
    } else if (key == "env_remove") {
      if (MatchChar(JsonContent, pos, '[')) {
        while (pos < JsonContent.size()) {
          SkipWhitespace(JsonContent, pos);
          if (pos < JsonContent.size() && JsonContent[pos] == ']') {
            ++pos;
            break;
          }
          auto envkey = ParseJsonString(JsonContent, pos);
          if (envkey.has_value()) {
            config.EnvMutations.push_back({
              Utf8ToWide(*envkey),
              L"",
              EnvMutationRemove
            });
          }
          MatchChar(JsonContent, pos, ',');
        }
      } else {
        SkipJsonValue(JsonContent, pos);
      }
    } else if (key == "user") {
      if (MatchChar(JsonContent, pos, '{')) {
        USER_CONTEXT_CONFIG usercfg;
        while (pos < JsonContent.size()) {
          SkipWhitespace(JsonContent, pos);
          if (pos < JsonContent.size() && JsonContent[pos] == '}') {
            ++pos;
            break;
          }
          auto ukey = ParseJsonString(JsonContent, pos);
          if (ukey.has_value() && MatchChar(JsonContent, pos, ':')) {
            if (*ukey == "username" || *ukey == "user") {
              auto uval = ParseJsonString(JsonContent, pos);
              if (uval.has_value()) usercfg.Username = Utf8ToWide(*uval);
            } else if (*ukey == "domain") {
              auto dval = ParseJsonString(JsonContent, pos);
              if (dval.has_value()) usercfg.Domain = Utf8ToWide(*dval);
            } else if (*ukey == "password") {
              auto pval = ParseJsonString(JsonContent, pos);
              if (pval.has_value()) usercfg.Password = Utf8ToWide(*pval);
            } else if (*ukey == "load_profile") {
              bool lp = false;
              if (ParseJsonBool(JsonContent, pos, lp)) {
                usercfg.LoadUserProfile = lp;
              }
            } else {
              SkipJsonValue(JsonContent, pos);
            }
          }
          MatchChar(JsonContent, pos, ',');
        }
        config.UserContext = usercfg;
      } else {
        SkipJsonValue(JsonContent, pos);
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
  std::istringstream stream((std::string(IniContent)));
  std::string line;
  std::string currentsection;

  auto trim = [](std::string_view S) -> std::string_view {
    size_t start = 0;
    while (start < S.size() && (S[start] == ' ' || S[start] == '\t' || S[start] == '\r' || S[start] == '\n')) {
      ++start;
    }
    size_t end = S.size();
    while (end > start && (S[end - 1] == ' ' || S[end - 1] == '\t' || S[end - 1] == '\r' || S[end - 1] == '\n')) {
      --end;
    }
    return S.substr(start, end - start);
  };

  while (std::getline(stream, line)) {
    std::string_view trimmed = trim(line);
    if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#') {
      continue;
    }

    if (trimmed.front() == '[' && trimmed.back() == ']') {
      currentsection = std::string(trimmed.substr(1, trimmed.size() - 2));
      continue;
    }

    size_t eqpos = trimmed.find('=');
    std::string key;
    std::string val;
    if (eqpos != std::string_view::npos) {
      key = std::string(trim(trimmed.substr(0, eqpos)));
      val = std::string(trim(trimmed.substr(eqpos + 1)));
    } else {
      key = std::string(trimmed);
    }

    if (currentsection == "target" || currentsection.empty()) {
      if (key == "target" || key == "executable") {
        config.Target = Utf8ToWide(val);
      } else if (key == "cwd" || key == "working_directory") {
        config.WorkingDirectory = Utf8ToWide(val);
      } else if (key == "desktop") {
        config.DesktopStation = Utf8ToWide(val);
      }
    } else if (currentsection == "env.set" || currentsection == "env_set") {
      config.EnvMutations.push_back({
        Utf8ToWide(key),
        Utf8ToWide(val),
        EnvMutationSet
      });
    } else if (currentsection == "env.remove" || currentsection == "env_remove") {
      config.EnvMutations.push_back({
        Utf8ToWide(key),
        L"",
        EnvMutationRemove
      });
    } else if (currentsection == "user") {
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
        config.UserContext->LoadUserProfile = (val == "true" || val == "1");
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

  if (FilePath.ends_with(L".json")) {
    return ParseJson(content);
  } else if (FilePath.ends_with(L".ini")) {
    return ParseIni(content);
  }

  auto jsonopt = ParseJson(content);
  if (jsonopt.has_value()) {
    return jsonopt;
  }
  return ParseIni(content);
}

} // namespace dogdouclix