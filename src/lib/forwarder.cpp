#include "dogdouclix/forwarder.hpp"
#include "dogdouclix/version.hpp"
#include <shellapi.h>
#include <iostream>

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
        // Even backslashes mean unescaped quotation mark
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
  const wchar_t* RawCommandLine
) {
  if (Argc < 2 || RawCommandLine == nullptr) {
    return std::nullopt;
  }

  FORWARDER_OPTIONS options;
  int targetargindex = 1;
  const wchar_t* rawcursor = RawCommandLine;

  // Skip argv[0] in raw command line
  rawcursor = SkipOneToken(rawcursor);

  while (targetargindex < Argc) {
    std::wstring_view arg = Argv[targetargindex];
    if (arg == L"--clix-version" || arg == L"-V") {
      std::wcout << L"dogdouclix version " << Utf8ToWide(VersionString) << std::endl;
      return std::nullopt;
    } else if (arg == L"--clix-help" || arg == L"-h") {
      std::wcout << L"Usage: dogdouclix [options] [--] <target.exe> [args...]\n\n"
                 << L"Options:\n"
                 << L"  --clix-env-set KEY=VAL     Insert or overwrite an environment variable\n"
                 << L"  --clix-env-remove KEY      Unset an environment variable in child\n"
                 << L"  --clix-cwd <DIR>           Set working directory for target process\n"
                 << L"  --clix-desktop <DESKTOP>   Set desktop station (e.g. winsta0\\default)\n"
                 << L"  --                         Stop option processing; next token is target executable\n"
                 << L"  --clix-version, -V         Show version\n"
                 << L"  --clix-help, -h            Show this help message\n";
      return std::nullopt;
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
    } else if (arg == L"--") {
      targetargindex += 1;
      rawcursor = SkipOneToken(rawcursor);
      break;
    } else {
      break;
    }
  }

  if (targetargindex >= Argc) {
    return std::nullopt;
  }

  options.TargetExecutable = Argv[targetargindex];
  rawcursor = SkipWhitespace(rawcursor);

  const wchar_t* rawtail = SkipOneToken(rawcursor);
  rawtail = SkipWhitespace(rawtail);

  options.FullCommandLine = BuildTargetCommandLine(options.TargetExecutable, rawtail);
  return options;
}

FORWARDING_RESULT Forwarder::Execute(const FORWARDER_OPTIONS& Options) {
  LAUNCH_CONFIG config;
  config.TargetExecutable = Options.TargetExecutable;
  config.FullCommandLine = Options.FullCommandLine;
  config.WorkingDirectory = Options.ContextOptions.WorkingDirectory;
  config.DesktopStation = Options.ContextOptions.DesktopStation;
  config.EnvironmentBlock = BuildEnvironmentBlock(
    Options.ContextOptions.EnvMutations,
    Options.ContextOptions.UserContext ? Options.ContextOptions.UserContext->ExistingToken : nullptr
  );
  if (Options.ContextOptions.UserContext) {
    config.UserToken = Options.ContextOptions.UserContext->ExistingToken;
  }

  return ProcessLauncher::LaunchAndForward(config);
}

} // namespace dogdouclix