#include "dogdouclix/common.hpp"
#include "dogdouclix/context_transform.hpp"
#include "dogdouclix/config_parser.hpp"
#include "dogdouclix/target_resolver.hpp"
#include "dogdouclix/forwarder.hpp"
#include <shellapi.h>
#include <iostream>
#include <fstream>
#include <cstdlib>

#define TEST_ASSERT(Condition, Message) \
  do { \
    if (!(Condition)) { \
      std::cerr << "[FAIL] " << Message << " (" << #Condition << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
      std::exit(1); \
    } else { \
      std::cout << "[PASS] " << Message << "\n"; \
    } \
  } while (0)

static void TestStringConversions() {
  std::string utf8 = "Hello, DogdouClix ASCII and Unicode Test!";
  std::wstring wide = dogdouclix::Utf8ToWide(utf8);
  TEST_ASSERT(!wide.empty(), "Utf8ToWide non-empty");

  std::string roundtrip = dogdouclix::WideToUtf8(wide);
  TEST_ASSERT(roundtrip == utf8, "UTF-8 roundtrip preservation");
}

static void TestArgumentQuoting() {
  std::wstring simple = dogdouclix::Forwarder::QuoteArgument(L"hello");
  TEST_ASSERT(simple == L"hello", "Simple arg not quoted");

  std::wstring withspaces = dogdouclix::Forwarder::QuoteArgument(L"hello world");
  TEST_ASSERT(withspaces == L"\"hello world\"", "Arg with spaces quoted");

  std::wstring withquotes = dogdouclix::Forwarder::QuoteArgument(L"foo\"bar");
  TEST_ASSERT(withquotes == L"\"foo\\\"bar\"", "Quotes escaped with backslash");

  std::wstring trailingbs = dogdouclix::Forwarder::QuoteArgument(L"C:\\Program Files\\");
  TEST_ASSERT(trailingbs == L"\"C:\\Program Files\\\\\"", "Trailing backslashes doubled in quoted arg");

  std::vector<std::wstring> testinputs = {
    L"",
    L"simple",
    L"with spaces",
    L"with \"quotes\"",
    L"C:\\Program Files\\App\\",
    L"special \\\" complex \\\\\" string"
  };

  for (const auto& original : testinputs) {
    std::wstring quoted = dogdouclix::Forwarder::QuoteArgument(original);
    std::wstring dummycmd = L"app.exe " + quoted;
    int numargs = 0;
    LPWSTR* parsedargv = ::CommandLineToArgvW(dummycmd.c_str(), &numargs);
    TEST_ASSERT(parsedargv != nullptr && numargs == 2, "CommandLineToArgvW parsed token count == 2");
    if (parsedargv != nullptr) {
      std::wstring parsedarg = parsedargv[1];
      ::LocalFree(parsedargv);
      TEST_ASSERT(parsedarg == original, "CommandLineToArgvW roundtrip matches original argument");
    }
  }
}

static void TestConfigParserJson() {
  std::string json = R"({
    "target": "C:\\Program Files\\Git\\bin\\git.exe",
    "cwd": "D:\\workspace",
    "desktop": "winsta0\\default",
    "env_set": {
      "CUSTOM_SECRET": "VaultSecret999",
      "CLI_MODE": "transparent"
    },
    "env_remove": [
      "AWS_SECRET_KEY",
      "LEAKED_TOKEN"
    ],
    "user": {
      "username": "DeployBot",
      "domain": "CORP",
      "load_profile": true
    }
  })";

  auto cfg = dogdouclix::ConfigParser::ParseJson(json);
  TEST_ASSERT(cfg.has_value(), "JSON parse succeeded");
  TEST_ASSERT(cfg->Target.has_value() && *cfg->Target == L"C:\\Program Files\\Git\\bin\\git.exe", "Target matches");
  TEST_ASSERT(cfg->WorkingDirectory.has_value() && *cfg->WorkingDirectory == L"D:\\workspace", "CWD matches");
  TEST_ASSERT(cfg->DesktopStation.has_value() && *cfg->DesktopStation == L"winsta0\\default", "Desktop matches");
  TEST_ASSERT(cfg->EnvMutations.size() == 4, "4 env mutations (2 set, 2 remove)");
  TEST_ASSERT(cfg->UserContext.has_value(), "User context present");
  TEST_ASSERT(cfg->UserContext->Username == L"DeployBot", "Username matches");
  TEST_ASSERT(cfg->UserContext->Domain == L"CORP", "Domain matches");
  TEST_ASSERT(cfg->UserContext->LoadUserProfile == true, "LoadUserProfile is true");
}

static void TestConfigParserIni() {
  std::string ini = R"(
    [target]
    executable = C:\Tools\mytool.exe
    cwd = C:\Temp

    [env.set]
    MY_VAR = IniValue123
    ANOTHER_VAR = HelloIni

    [env.remove]
    REMOVE_ME = 1

    [user]
    username = Alice
    load_profile = true
  )";

  auto cfg = dogdouclix::ConfigParser::ParseIni(ini);
  TEST_ASSERT(cfg.has_value(), "INI parse succeeded");
  TEST_ASSERT(cfg->Target.has_value() && *cfg->Target == L"C:\\Tools\\mytool.exe", "Target matches in INI");
  TEST_ASSERT(cfg->WorkingDirectory.has_value() && *cfg->WorkingDirectory == L"C:\\Temp", "CWD matches in INI");
  TEST_ASSERT(cfg->EnvMutations.size() == 3, "3 env mutations in INI");
  TEST_ASSERT(cfg->UserContext.has_value() && cfg->UserContext->Username == L"Alice", "Username matches in INI");
}

static void TestTargetResolverSplitPath() {
  std::wstring dir;
  std::wstring filename;
  std::wstring basename;

  dogdouclix::TargetResolver::SplitPath(L"C:\\tools\\git.exe", dir, filename, basename);
  TEST_ASSERT(dir == L"C:\\tools", "Directory parsed as C:\\tools");
  TEST_ASSERT(filename == L"git.exe", "Filename parsed as git.exe");
  TEST_ASSERT(basename == L"git", "Basename parsed as git");
}

static void TestTargetResolverPathPenetration() {
  auto penetrated = dogdouclix::TargetResolver::PenetratePath(L"cmd.exe", L"C:\\fake_shim_dir");
  TEST_ASSERT(penetrated.has_value(), "PATH penetration found cmd.exe");
  TEST_ASSERT(penetrated->find(L"cmd.exe") != std::wstring::npos, "Penetrated path contains cmd.exe");
}

static void TestTargetResolverCompanionConfig() {
  wchar_t temppath[MAX_PATH] = {0};
  ::GetTempPathW(MAX_PATH, temppath);

  std::wstring dummyexe = std::wstring(temppath) + L"dummytool.exe";
  std::wstring dummycfg = std::wstring(temppath) + L"dummytool.clix.json";

  std::ofstream outcfg(dummycfg, std::ios::binary);
  outcfg << R"({ "target": "cmd.exe", "env_set": { "SHIM_FLAG": "Active" } })";
  outcfg.close();

  auto res = dogdouclix::TargetResolver::Resolve(dummyexe);
  TEST_ASSERT(res.has_value(), "TargetResolver resolved companion config");
  TEST_ASSERT(res->IsTransparentShim, "IsTransparentShim is true");
  TEST_ASSERT(res->TargetExecutable == L"cmd.exe", "Target executable is cmd.exe");
  TEST_ASSERT(res->LoadedConfig.has_value(), "Config loaded");
  TEST_ASSERT(res->LoadedConfig->EnvMutations.size() == 1, "Env mutation loaded");

  ::DeleteFileW(dummycfg.c_str());
}

static void TestForwarderTransparentShimMode() {
  wchar_t temppath[MAX_PATH] = {0};
  ::GetTempPathW(MAX_PATH, temppath);

  std::wstring dummyexe = std::wstring(temppath) + L"mycustomshim.exe";
  std::wstring dummycfg = std::wstring(temppath) + L"mycustomshim.clix.json";

  std::ofstream outcfg(dummycfg, std::ios::binary);
  outcfg << R"({
    "target": "cmd.exe",
    "env_set": {
      "SHIM_SECRET": "InjectedSecret777"
    },
    "env_remove": [
      "DOGDOUCLIX_LEAKED_SECRET"
    ]
  })";
  outcfg.close();

  ::SetEnvironmentVariableW(L"DOGDOUCLIX_LEAKED_SECRET", L"LeakedValue");

  const wchar_t* rawcmd = L"mycustomshim.exe /c if not defined DOGDOUCLIX_LEAKED_SECRET if \"%SHIM_SECRET%\"==\"InjectedSecret777\" exit 0";
  wchar_t a0[] = L"mycustomshim.exe";
  wchar_t a1[] = L"/c";
  wchar_t a2[] = L"if";
  wchar_t* argv[] = { a0, a1, a2 };

  auto options = dogdouclix::Forwarder::ParseCommandLine(3, argv, rawcmd, dummyexe);
  TEST_ASSERT(options.has_value(), "ParseCommandLine in transparent shim mode succeeded");
  TEST_ASSERT(options->IsTransparentMode, "IsTransparentMode is true");
  TEST_ASSERT(options->TargetExecutable == L"cmd.exe", "Target executable is cmd.exe");
  TEST_ASSERT(options->ContextOptions.EnvMutations.size() == 2, "2 env mutations loaded (1 set, 1 remove)");

  auto result = dogdouclix::Forwarder::Execute(*options);
  TEST_ASSERT(result.Succeeded, "Execution in transparent shim mode succeeded");
  TEST_ASSERT(result.ExitCode == 0, "Exit code is 0 (child verified secret injection and redaction)");

  wchar_t checkval[64] = {0};
  DWORD getok = ::GetEnvironmentVariableW(L"DOGDOUCLIX_LEAKED_SECRET", checkval, 64);
  TEST_ASSERT(getok > 0 && std::wstring(checkval) == L"LeakedValue", "Parent environment remained unmodified");

  ::SetEnvironmentVariableW(L"DOGDOUCLIX_LEAKED_SECRET", nullptr);
  ::DeleteFileW(dummycfg.c_str());
}

static void TestEnvironmentBlockMutations() {
  std::vector<dogdouclix::ENV_MUTATION> mutations = {
    { L"DOGDOUCLIX_TEST_KEY", L"SpecialSecretValue123", dogdouclix::EnvMutationSet },
    { L"DOGDOUCLIX_REMOVE_KEY", L"", dogdouclix::EnvMutationRemove }
  };

  auto block = dogdouclix::BuildEnvironmentBlock(mutations);
  TEST_ASSERT(!block.empty(), "Environment block generation non-empty");

  bool foundsecret = false;
  const wchar_t* curr = block.data();
  while (*curr != L'\0') {
    std::wstring entry(curr);
    if (entry == L"DOGDOUCLIX_TEST_KEY=SpecialSecretValue123") {
      foundsecret = true;
    }
    curr += entry.size() + 1;
  }
  TEST_ASSERT(foundsecret, "Environment block contains inserted test key");
}

static void TestCommandLineParsing() {
  const wchar_t* rawcmd = L"dogdouclix.exe --clix-env-set FOO=BAR cmd.exe /c echo hello \"world with spaces\"";
  wchar_t arg0[] = L"dogdouclix.exe";
  wchar_t arg1[] = L"--clix-env-set";
  wchar_t arg2[] = L"FOO=BAR";
  wchar_t arg3[] = L"cmd.exe";
  wchar_t arg4[] = L"/c";
  wchar_t arg5[] = L"echo";
  wchar_t arg6[] = L"hello";
  wchar_t arg7[] = L"world with spaces";
  wchar_t* argv[] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7 };

  auto options = dogdouclix::Forwarder::ParseCommandLine(8, argv, rawcmd);
  TEST_ASSERT(options.has_value(), "ParseCommandLine successful");
  TEST_ASSERT(options->TargetExecutable == L"cmd.exe", "Target executable is cmd.exe");
  TEST_ASSERT(options->ContextOptions.EnvMutations.size() == 1, "One env mutation parsed");
  TEST_ASSERT(options->ContextOptions.EnvMutations[0].Key == L"FOO", "Env key is FOO");
  TEST_ASSERT(options->ContextOptions.EnvMutations[0].Value == L"BAR", "Env value is BAR");
  TEST_ASSERT(options->FullCommandLine.find(L"\"world with spaces\"") != std::wstring::npos, "Quotes preserved in full command line");

  const wchar_t* rawcmddash = L"dogdouclix.exe --clix-cwd L:\\temp -- git.exe log -n 5";
  wchar_t d0[] = L"dogdouclix.exe";
  wchar_t d1[] = L"--clix-cwd";
  wchar_t d2[] = L"L:\\temp";
  wchar_t d3[] = L"--";
  wchar_t d4[] = L"git.exe";
  wchar_t d5[] = L"log";
  wchar_t d6[] = L"-n";
  wchar_t d7[] = L"5";
  wchar_t* dashargv[] = { d0, d1, d2, d3, d4, d5, d6, d7 };

  auto dashoptions = dogdouclix::Forwarder::ParseCommandLine(8, dashargv, rawcmddash);
  TEST_ASSERT(dashoptions.has_value(), "ParseCommandLine with -- delimiter successful");
  TEST_ASSERT(dashoptions->TargetExecutable == L"git.exe", "Target executable after -- is git.exe");
  TEST_ASSERT(dashoptions->ContextOptions.WorkingDirectory == L"L:\\temp", "Working directory parsed correctly");
  TEST_ASSERT(dashoptions->FullCommandLine == L"git.exe log -n 5", "Full command line matches tail arguments");
}

static void TestProcessExecutionAndExitCodes() {
  std::vector<DWORD> testcodes = { 0, 1, 42, 100, 255 };
  for (DWORD code : testcodes) {
    dogdouclix::FORWARDER_OPTIONS options;
    options.TargetExecutable = L"cmd.exe";
    options.FullCommandLine = L"cmd.exe /c exit " + std::to_wstring(code);

    auto result = dogdouclix::Forwarder::Execute(options);
    TEST_ASSERT(result.Succeeded, "Process execution succeeded for exit code test");
    TEST_ASSERT(result.ExitCode == code, "Exit code accurately propagated (" + std::to_string(code) + ")");
  }
}

static void TestPipedStreamPassThrough() {
  HANDLE hreadpipe = nullptr;
  HANDLE hwritepipe = nullptr;
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  BOOL pipecreated = ::CreatePipe(&hreadpipe, &hwritepipe, &sa, 0);
  TEST_ASSERT(pipecreated, "Anonymous pipe created successfully");

  HANDLE holdstdout = ::GetStdHandle(STD_OUTPUT_HANDLE);
  ::SetStdHandle(STD_OUTPUT_HANDLE, hwritepipe);

  dogdouclix::FORWARDER_OPTIONS options;
  options.TargetExecutable = L"cmd.exe";
  options.FullCommandLine = L"cmd.exe /c echo PipedOutputVerificationString";

  auto result = dogdouclix::Forwarder::Execute(options);

  ::SetStdHandle(STD_OUTPUT_HANDLE, holdstdout);
  ::CloseHandle(hwritepipe);

  TEST_ASSERT(result.Succeeded, "Forwarder execution with piped stdout succeeded");
  TEST_ASSERT(result.ExitCode == 0, "Exit code is 0");

  char readbuffer[256] = {0};
  DWORD bytesread = 0;
  BOOL readok = ::ReadFile(hreadpipe, readbuffer, sizeof(readbuffer) - 1, &bytesread, nullptr);
  ::CloseHandle(hreadpipe);

  TEST_ASSERT(readok, "ReadFile from anonymous pipe succeeded");
  std::string captured(readbuffer, bytesread);
  TEST_ASSERT(captured.find("PipedOutputVerificationString") != std::string::npos, "Piped stream captured expected child output");
}

static void TestWorkingDirectorySwitching() {
  wchar_t temppath[MAX_PATH] = {0};
  ::GetTempPathW(MAX_PATH, temppath);

  dogdouclix::FORWARDER_OPTIONS options;
  options.TargetExecutable = L"cmd.exe";
  options.ContextOptions.WorkingDirectory = temppath;
  options.FullCommandLine = L"cmd.exe /c exit 0";

  auto result = dogdouclix::Forwarder::Execute(options);
  TEST_ASSERT(result.Succeeded, "Execution with working directory switch succeeded");
  TEST_ASSERT(result.ExitCode == 0, "Exit code is 0");
}

static void TestEnvironmentIsolationAndMutation() {
  ::SetEnvironmentVariableW(L"DOGDOUCLIX_PARENT_SECRET", L"ParentValueShouldBeRedacted");

  dogdouclix::FORWARDER_OPTIONS options;
  options.TargetExecutable = L"cmd.exe";
  options.ContextOptions.EnvMutations.push_back({
    L"DOGDOUCLIX_PARENT_SECRET",
    L"",
    dogdouclix::EnvMutationRemove
  });
  options.ContextOptions.EnvMutations.push_back({
    L"DOGDOUCLIX_INJECTED_KEY",
    L"InjectedValue987",
    dogdouclix::EnvMutationSet
  });
  options.FullCommandLine = L"cmd.exe /c if not defined DOGDOUCLIX_PARENT_SECRET if \"%DOGDOUCLIX_INJECTED_KEY%\"==\"InjectedValue987\" exit 0";

  auto result = dogdouclix::Forwarder::Execute(options);
  TEST_ASSERT(result.Succeeded, "Execution with env mutation succeeded");
  TEST_ASSERT(result.ExitCode == 0, "Child verified parent secret was redacted and injected key was present");

  wchar_t parentval[64] = {0};
  DWORD getok = ::GetEnvironmentVariableW(L"DOGDOUCLIX_PARENT_SECRET", parentval, 64);
  TEST_ASSERT(getok > 0 && std::wstring(parentval) == L"ParentValueShouldBeRedacted", "Parent environment remained untouched");

  ::SetEnvironmentVariableW(L"DOGDOUCLIX_PARENT_SECRET", nullptr);
}

int main() {
  std::cout << "=== Running DogdouClix Core Test Suite ===\n";
  TestStringConversions();
  TestArgumentQuoting();
  TestConfigParserJson();
  TestConfigParserIni();
  TestTargetResolverSplitPath();
  TestTargetResolverPathPenetration();
  TestTargetResolverCompanionConfig();
  TestForwarderTransparentShimMode();
  TestEnvironmentBlockMutations();
  TestCommandLineParsing();
  TestProcessExecutionAndExitCodes();
  TestPipedStreamPassThrough();
  TestWorkingDirectorySwitching();
  TestEnvironmentIsolationAndMutation();
  std::cout << "=== All Tests Passed Successfully! ===\n";
  return 0;
}