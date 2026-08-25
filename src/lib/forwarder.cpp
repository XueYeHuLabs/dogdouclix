#include "dogdouclix/forwarder.hpp"
#include "dogdouclix/version.hpp"
#include "dogdouclix/cred_manager.hpp"
#include <shellapi.h>
#include <iostream>
#include <algorithm>

namespace dogdouclix {

namespace {

static const wchar_t* SkipWhitespace(const wchar_t* Str) {
  while (*Str != L'\0' && (*Str == L' ' || *Str == L'\t')) {
    ++Str;
  }
  return Str;
}

static const wchar_t* SkipOneToken(const wchar_t* Str) {
  Str = SkipWhitespace(Str);
  if (*Str == L'\0') {
    return Str;
  }

  bool inquotes = false;
  size_t backslashes = 0;

  while (*Str != L'\0') {
    if (*Str == L'\\') {
      ++backslashes;
    } else if (*Str == L'"') {
      if ((backslashes % 2) == 0) {
        inquotes = !inquotes;
      }
      backslashes = 0;
    } else {
      if (!inquotes && (*Str == L' ' || *Str == L'\t')) {
        break;
      }
      backslashes = 0;
    }
    ++Str;
  }
  return Str;
}

} // namespace

std::wstring Forwarder::QuoteArgument(std::wstring_view Arg) {
  if (Arg.empty()) {
    return L"\"\"";
  }
  if (Arg.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
    return std::wstring(Arg);
  }

  std::wstring result;
  result.push_back(L'"');
  size_t backslashes = 0;

  for (wchar_t ch : Arg) {
    if (ch == L'\\') {
      ++backslashes;
    } else if (ch == L'"') {
      result.append(backslashes * 2 + 1, L'\\');
      result.push_back(L'"');
      backslashes = 0;
    } else {
      result.append(backslashes, L'\\');
      result.push_back(ch);
      backslashes = 0;
    }
  }

  result.append(backslashes * 2, L'\\');
  result.push_back(L'"');
  return result;
}

std::wstring Forwarder::BuildTargetCommandLine(
  std::wstring_view TargetExe,
  std::wstring_view TailArgs
) {
  std::wstring cmd;
  if (TargetExe.find(L' ') != std::wstring_view::npos && !TargetExe.starts_with(L"\"")) {
    cmd = L"\"" + std::wstring(TargetExe) + L"\"";
  } else {
    cmd = std::wstring(TargetExe);
  }

  if (!TailArgs.empty()) {
    cmd += L" ";
    cmd += TailArgs;
  }
  return cmd;
}

std::optional<FORWARDER_OPTIONS> Forwarder::ParseCommandLine(
  int Argc,
  wchar_t* Argv[],
  const wchar_t* RawCommandLine,
  const std::wstring& CustomSelfExe
) {
  if (RawCommandLine == nullptr) {
    return std::nullopt;
  }

  // 1. Check for transparent shim mode
  FORWARDER_OPTIONS options;
  auto resolved = TargetResolver::Resolve(CustomSelfExe);
  if (resolved.has_value() && resolved->IsTransparentShim) {
    if (resolved->TargetExecutable.empty()) {
      std::wcerr << L"dogdouclix: Transparent shim target not found.\n";
      return std::nullopt;
    }

    options.IsTransparentMode = true;
    options.TargetExecutable = resolved->TargetExecutable;

    if (resolved->LoadedConfig.has_value()) {
      options.ContextOptions.EnvMutations = resolved->LoadedConfig->EnvMutations;
      options.ContextOptions.WorkingDirectory = resolved->LoadedConfig->WorkingDirectory;
      options.ContextOptions.DesktopStation = resolved->LoadedConfig->DesktopStation;
      options.ContextOptions.UserContext = resolved->LoadedConfig->UserContext;
    }
  } else {
    // Explicit Forwarder Mode requires at least 2 arguments (dogdouclix <target.exe>)
    if (Argc < 2) {
      return std::nullopt;
    }
  }

  int targetargindex = 1;
  const wchar_t* rawcursor = RawCommandLine;

  rawcursor = SkipOneToken(rawcursor);

  while (targetargindex < Argc) {
    std::wstring_view arg = Argv[targetargindex];
    if (arg == L"--clix-version" || (!options.IsTransparentMode && arg == L"-V")) {
      std::wcout << L"dogdouclix version " << Utf8ToWide(VersionString) << std::endl;
      return std::nullopt;
    } else if (arg == L"--clix-help" || (!options.IsTransparentMode && arg == L"-h")) {
      std::wcout << L"Usage: dogdouclix [options] [--] <target.exe> [args...]\n\n"
                 << L"Execution Options:\n"
                 << L"  --clix-profile <NAME>      Load OS user credentials from Windows Credential Manager\n"
                 << L"  --clix-config <FILE>       Load companion configuration file (.json / .ini)\n"
                 << L"  --clix-env-set KEY=VAL     Insert or overwrite an environment variable\n"
                 << L"  --clix-env-remove KEY      Unset an environment variable in child\n"
                 << L"  --clix-cwd <DIR>           Set working directory for target process\n"
                 << L"  --clix-desktop <DESKTOP>   Set desktop station (e.g. winsta0\\default)\n"
                 << L"  --clix-user <USER>         Execute target under specified username\n"
                 << L"  --clix-domain <DOMAIN>     Domain for user credentials\n"
                 << L"  --clix-password <PWD>      Password for user credentials\n"
                 << L"  --clix-logon-type <TYPE>   Logon type (interactive/batch/service/network/new_credentials)\n"
                 << L"  --clix-load-profile        Load user profile when switching user\n"
                 << L"  --                         Stop option processing; next token is target executable\n\n"
                 << L"Scaffolding Commands:\n"
                 << L"  --clix-init <json|ini> [path]   Generate template configuration file (default: clix.json / clix.ini)\n"
                 << L"  --clix-template <json|ini>      Print configuration template directly to stdout\n\n"
                 << L"Credential Management Commands:\n"
                 << L"  --clix-profile-set <NAME> [opts]   Save/update OS credentials in Credential Manager\n"
                 << L"  --clix-profile-get <NAME>          Display credential metadata from Credential Manager\n"
                 << L"  --clix-profile-delete <NAME>       Delete credential profile from Credential Manager\n"
                 << L"  --clix-profile-list                List all registered credential profiles\n";
      return std::nullopt;
    } else if (arg == L"--clix-template" && targetargindex + 1 < Argc) {
      std::wstring formatarg = Argv[targetargindex + 1];
      std::string formatutf8 = WideToUtf8(formatarg);
      std::string formatlower = formatutf8;
      std::transform(formatlower.begin(), formatlower.end(), formatlower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      if (formatlower == "json") {
        std::cout << ConfigParser::GenerateTemplateJson();
      } else if (formatlower == "ini") {
        std::cout << ConfigParser::GenerateTemplateIni();
      } else {
        std::wcerr << L"dogdouclix error: Unsupported template format '" << formatarg << L"'. Supported formats: 'json', 'ini'.\n";
      }
      return std::nullopt;
    } else if (arg == L"--clix-template") {
      std::wcerr << L"dogdouclix error: Missing template format for --clix-template. Usage: --clix-template <json|ini>\n";
      return std::nullopt;
    } else if (arg == L"--clix-init" && targetargindex + 1 < Argc) {
      std::wstring formatarg = Argv[targetargindex + 1];
      std::string formatutf8 = WideToUtf8(formatarg);
      std::string formatlower = formatutf8;
      std::transform(formatlower.begin(), formatlower.end(), formatlower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });

      if (formatlower != "json" && formatlower != "ini") {
        std::wcerr << L"dogdouclix error: Unsupported template format '" << formatarg << L"'. Supported formats: 'json', 'ini'.\n";
        return std::nullopt;
      }

      std::wstring outpath;
      if (targetargindex + 2 < Argc && !std::wstring_view(Argv[targetargindex + 2]).starts_with(L"--")) {
        outpath = Argv[targetargindex + 2];
      } else {
        outpath = (formatlower == "json") ? L"clix.json" : L"clix.ini";
      }

      if (outpath == L"-" || outpath == L"stdout") {
        if (formatlower == "json") {
          std::cout << ConfigParser::GenerateTemplateJson();
        } else {
          std::cout << ConfigParser::GenerateTemplateIni();
        }
      } else {
        std::string err;
        if (ConfigParser::WriteTemplateFile(outpath, formatlower, &err)) {
          std::wcout << L"dogdouclix: Successfully generated template configuration at '" << outpath << L"'.\n";
        } else {
          std::wcerr << L"dogdouclix error: " << Utf8ToWide(err) << L"\n";
        }
      }
      return std::nullopt;
    } else if (arg == L"--clix-init") {
      std::wcerr << L"dogdouclix error: Missing template format for --clix-init. Usage: --clix-init <json|ini> [path]\n";
      return std::nullopt;
    } else if (arg == L"--clix-profile-list") {
      std::string err;
      auto list = CredManager::ListProfiles(&err);
      std::wcout << L"Registered DogdouClix Profiles in Windows Credential Manager:\n";
      if (list.empty()) {
        std::wcout << L"  (No profiles registered)\n";
      } else {
        for (const auto& p : list) {
          std::wcout << L"  - " << p << L"\n";
        }
      }
      return std::nullopt;
    } else if (arg == L"--clix-profile-get" && targetargindex + 1 < Argc) {
      std::wstring profname = Argv[targetargindex + 1];
      std::string err;
      auto prof = CredManager::GetProfile(profname, &err);
      if (!prof.has_value()) {
        std::wcerr << L"dogdouclix error: " << Utf8ToWide(err) << L"\n";
      } else {
        std::wcout << L"Profile: " << prof->Name << L"\n";
        if (prof->Username.has_value()) std::wcout << L"  Username: " << *prof->Username << L"\n";
        if (prof->Domain.has_value()) std::wcout << L"  Domain: " << *prof->Domain << L"\n";
        if (prof->Password.has_value()) std::wcout << L"  Password: [PROTECTED (" << prof->Password->size() << L" chars)]\n";
      }
      return std::nullopt;
    } else if (arg == L"--clix-profile-delete" && targetargindex + 1 < Argc) {
      std::wstring profname = Argv[targetargindex + 1];
      std::string err;
      if (CredManager::DeleteProfile(profname, &err)) {
        std::wcout << L"dogdouclix: Profile '" << profname << L"' deleted successfully from Windows Credential Manager.\n";
      } else {
        std::wcerr << L"dogdouclix error: " << Utf8ToWide(err) << L"\n";
      }
      return std::nullopt;
    } else if (arg == L"--clix-profile-set" && targetargindex + 1 < Argc) {
      CRED_PROFILE newprof;
      newprof.Name = Argv[targetargindex + 1];
      targetargindex += 2;
      while (targetargindex < Argc) {
        std::wstring_view subarg = Argv[targetargindex];
        if (subarg == L"--clix-user" && targetargindex + 1 < Argc) {
          newprof.Username = Argv[targetargindex + 1];
          targetargindex += 2;
        } else if (subarg == L"--clix-domain" && targetargindex + 1 < Argc) {
          newprof.Domain = Argv[targetargindex + 1];
          targetargindex += 2;
        } else if (subarg == L"--clix-password" && targetargindex + 1 < Argc) {
          newprof.Password = Argv[targetargindex + 1];
          targetargindex += 2;
        } else {
          break;
        }
      }
      std::string err;
      if (CredManager::SaveProfile(newprof, &err)) {
        std::wcout << L"dogdouclix: Profile '" << newprof.Name << L"' successfully saved to Windows Credential Manager.\n";
      } else {
        std::wcerr << L"dogdouclix error: " << Utf8ToWide(err) << L"\n";
      }
      return std::nullopt;
    } else if (arg == L"--clix-profile" && targetargindex + 1 < Argc) {
      std::wstring profname = Argv[targetargindex + 1];
      std::string err;
      auto prof = CredManager::GetProfile(profname, &err);
      if (prof.has_value()) {
        if (!options.ContextOptions.UserContext.has_value()) {
          options.ContextOptions.UserContext = USER_CONTEXT_CONFIG{};
        }
        if (prof->Username.has_value()) options.ContextOptions.UserContext->Username = prof->Username;
        if (prof->Domain.has_value()) options.ContextOptions.UserContext->Domain = prof->Domain;
        if (prof->Password.has_value()) options.ContextOptions.UserContext->Password = prof->Password;
      } else {
        std::wcerr << L"dogdouclix warning: " << Utf8ToWide(err) << L"\n";
      }
      targetargindex += 2;
      rawcursor = SkipOneToken(rawcursor);
      rawcursor = SkipOneToken(rawcursor);
    } else if (arg == L"--clix-config" && targetargindex + 1 < Argc) {
      auto cfg = ConfigParser::ParseFile(Argv[targetargindex + 1]);
      if (cfg.has_value()) {
        options.ContextOptions.EnvMutations.insert(
          options.ContextOptions.EnvMutations.end(),
          cfg->EnvMutations.begin(),
          cfg->EnvMutations.end()
        );
        if (cfg->WorkingDirectory.has_value()) options.ContextOptions.WorkingDirectory = cfg->WorkingDirectory;
        if (cfg->DesktopStation.has_value()) options.ContextOptions.DesktopStation = cfg->DesktopStation;
        if (cfg->UserContext.has_value()) options.ContextOptions.UserContext = cfg->UserContext;
        if (cfg->Target.has_value()) options.TargetExecutable = *cfg->Target;
      }
      targetargindex += 2;
      rawcursor = SkipOneToken(rawcursor);
      rawcursor = SkipOneToken(rawcursor);
    } else if (arg == L"--clix-env-set" && targetargindex + 1 < Argc) {
      std::wstring_view kv = Argv[targetargindex + 1];
      size_t eqpos = kv.find(L'=');
      if (eqpos != std::wstring_view::npos) {
        options.ContextOptions.EnvMutations.push_back({
          std::wstring(kv.substr(0, eqpos)),
          std::wstring(kv.substr(eqpos + 1)),
          EnvMutationSet
        });
      }
      targetargindex += 2;
      rawcursor = SkipOneToken(rawcursor);
      rawcursor = SkipOneToken(rawcursor);
    } else if (arg == L"--clix-env-remove" && targetargindex + 1 < Argc) {
      options.ContextOptions.EnvMutations.push_back({
        std::wstring(Argv[targetargindex + 1]),
        L"",
        EnvMutationRemove
      });
      targetargindex += 2;
      rawcursor = SkipOneToken(rawcursor);
      rawcursor = SkipOneToken(rawcursor);
    } else if (arg == L"--clix-cwd" && targetargindex + 1 < Argc) {
      options.ContextOptions.WorkingDirectory = Argv[targetargindex + 1];
      targetargindex += 2;
      rawcursor = SkipOneToken(rawcursor);
      rawcursor = SkipOneToken(rawcursor);
    } else if (arg == L"--clix-desktop" && targetargindex + 1 < Argc) {
      options.ContextOptions.DesktopStation = Argv[targetargindex + 1];
      targetargindex += 2;
      rawcursor = SkipOneToken(rawcursor);
      rawcursor = SkipOneToken(rawcursor);
    } else if (arg == L"--clix-user" && targetargindex + 1 < Argc) {
      if (!options.ContextOptions.UserContext.has_value()) {
        options.ContextOptions.UserContext = USER_CONTEXT_CONFIG{};
      }
      options.ContextOptions.UserContext->Username = Argv[targetargindex + 1];
      targetargindex += 2;
      rawcursor = SkipOneToken(rawcursor);
      rawcursor = SkipOneToken(rawcursor);
    } else if (arg == L"--clix-domain" && targetargindex + 1 < Argc) {
      if (!options.ContextOptions.UserContext.has_value()) {
        options.ContextOptions.UserContext = USER_CONTEXT_CONFIG{};
      }
      options.ContextOptions.UserContext->Domain = Argv[targetargindex + 1];
      targetargindex += 2;
      rawcursor = SkipOneToken(rawcursor);
      rawcursor = SkipOneToken(rawcursor);
    } else if (arg == L"--clix-password" && targetargindex + 1 < Argc) {
      if (!options.ContextOptions.UserContext.has_value()) {
        options.ContextOptions.UserContext = USER_CONTEXT_CONFIG{};
      }
      options.ContextOptions.UserContext->Password = Argv[targetargindex + 1];
      targetargindex += 2;
      rawcursor = SkipOneToken(rawcursor);
      rawcursor = SkipOneToken(rawcursor);
    } else if (arg == L"--clix-logon-type" && targetargindex + 1 < Argc) {
      if (!options.ContextOptions.UserContext.has_value()) {
        options.ContextOptions.UserContext = USER_CONTEXT_CONFIG{};
      }
      std::wstring ltstr = Argv[targetargindex + 1];
      std::transform(ltstr.begin(), ltstr.end(), ltstr.begin(), ::towlower);
      if (ltstr == L"interactive") options.ContextOptions.UserContext->LogonType = LOGON32_LOGON_INTERACTIVE;
      else if (ltstr == L"batch") options.ContextOptions.UserContext->LogonType = LOGON32_LOGON_BATCH;
      else if (ltstr == L"service") options.ContextOptions.UserContext->LogonType = LOGON32_LOGON_SERVICE;
      else if (ltstr == L"network") options.ContextOptions.UserContext->LogonType = LOGON32_LOGON_NETWORK;
      else if (ltstr == L"network_cleartext") options.ContextOptions.UserContext->LogonType = LOGON32_LOGON_NETWORK_CLEARTEXT;
      else if (ltstr == L"new_credentials") options.ContextOptions.UserContext->LogonType = LOGON32_LOGON_NEW_CREDENTIALS;
      else {
        try {
          options.ContextOptions.UserContext->LogonType = static_cast<DWORD>(std::stoul(ltstr));
        } catch (...) {}
      }
      targetargindex += 2;
      rawcursor = SkipOneToken(rawcursor);
      rawcursor = SkipOneToken(rawcursor);
    } else if (arg == L"--clix-load-profile") {
      if (!options.ContextOptions.UserContext.has_value()) {
        options.ContextOptions.UserContext = USER_CONTEXT_CONFIG{};
      }
      options.ContextOptions.UserContext->LoadUserProfile = true;
      targetargindex += 1;
      rawcursor = SkipOneToken(rawcursor);
    } else if (arg == L"--") {
      targetargindex += 1;
      rawcursor = SkipOneToken(rawcursor);
      break;
    } else {
      break;
    }
  }

  if (options.IsTransparentMode) {
    rawcursor = SkipWhitespace(rawcursor);
    options.FullCommandLine = BuildTargetCommandLine(options.TargetExecutable, rawcursor);
    return options;
  }

  if (targetargindex < Argc) {
    options.TargetExecutable = Argv[targetargindex];
    rawcursor = SkipWhitespace(rawcursor);

    const wchar_t* rawtail = SkipOneToken(rawcursor);
    rawtail = SkipWhitespace(rawtail);

    options.FullCommandLine = BuildTargetCommandLine(options.TargetExecutable, rawtail);
    return options;
  }

  if (!options.TargetExecutable.empty()) {
    rawcursor = SkipWhitespace(rawcursor);
    options.FullCommandLine = BuildTargetCommandLine(options.TargetExecutable, rawcursor);
    return options;
  }

  return std::nullopt;
}

FORWARDING_RESULT Forwarder::Execute(const FORWARDER_OPTIONS& Options) {
  HANDLE usertoken = nullptr;
  if (Options.ContextOptions.UserContext.has_value()) {
    std::string error;
    if (!AcquireUserToken(*Options.ContextOptions.UserContext, &usertoken, &error)) {
      FORWARDING_RESULT errres;
      errres.Succeeded = false;
      errres.ErrorMessage = error;
      errres.ExitCode = 1;
      return errres;
    }
  }

  LAUNCH_CONFIG config;
  config.TargetExecutable = Options.TargetExecutable;
  config.FullCommandLine = Options.FullCommandLine;
  config.WorkingDirectory = Options.ContextOptions.WorkingDirectory;
  config.DesktopStation = Options.ContextOptions.DesktopStation;
  config.EnvironmentBlock = BuildEnvironmentBlock(
    Options.ContextOptions.EnvMutations,
    usertoken
  );
  config.UserToken = usertoken;

  auto result = ProcessLauncher::LaunchAndForward(config);

  if (usertoken != nullptr) {
    ::CloseHandle(usertoken);
  }

  return result;
}

} // namespace dogdouclix