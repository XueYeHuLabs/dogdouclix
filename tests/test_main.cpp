#include "dogdouclix/common.hpp"
#include "dogdouclix/context_transform.hpp"
#include "dogdouclix/config_parser.hpp"
#include "dogdouclix/target_resolver.hpp"
#include "dogdouclix/cred_manager.hpp"
#include "dogdouclix/forwarder.hpp"
#include <shellapi.h>
#include <iostream>
#include <fstream>
#include <sstream>
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

static void TestCredManagerCrud() {
  dogdouclix::CRED_PROFILE prof;
  prof.Name = L"TEST_UNIT_PROFILE_1";
  prof.Username = L"VaultAdmin";
  prof.Domain = L"LOCALDOM";
  prof.Password = L"SuperSecretPass999!";

  std::string error;
  bool saved = dogdouclix::CredManager::SaveProfile(prof, &error);
  TEST_ASSERT(saved, "CredManager::SaveProfile succeeded: " + error);

  auto loaded = dogdouclix::CredManager::GetProfile(L"TEST_UNIT_PROFILE_1", &error);
  TEST_ASSERT(loaded.has_value(), "CredManager::GetProfile succeeded: " + error);
  TEST_ASSERT(loaded->Name == L"TEST_UNIT_PROFILE_1", "Profile name matches");
  TEST_ASSERT(loaded->Username.has_value() && *loaded->Username == L"VaultAdmin", "Username matches");
  TEST_ASSERT(loaded->Domain.has_value() && *loaded->Domain == L"LOCALDOM", "Domain matches");
  TEST_ASSERT(loaded->Password.has_value() && *loaded->Password == L"SuperSecretPass999!", "Password matches");

  auto list = dogdouclix::CredManager::ListProfiles(&error);
  bool foundinlist = false;
  for (const auto& item : list) {
    if (item == L"TEST_UNIT_PROFILE_1") {
      foundinlist = true;
      break;
    }
  }
  TEST_ASSERT(foundinlist, "Profile found in ListProfiles");

  bool deleted = dogdouclix::CredManager::DeleteProfile(L"TEST_UNIT_PROFILE_1", &error);
  TEST_ASSERT(deleted, "CredManager::DeleteProfile succeeded: " + error);

  auto afterdel = dogdouclix::CredManager::GetProfile(L"TEST_UNIT_PROFILE_1");
  TEST_ASSERT(!afterdel.has_value(), "Profile successfully deleted from Credential Manager");
}

static void TestProfileConfigIntegration() {
  dogdouclix::CRED_PROFILE prof;
  prof.Name = L"TEST_CONFIG_INT_PROFILE";
  prof.Username = L"TestAdminUser";
  prof.Domain = L"CORPDOM";
  prof.Password = L"VaultTokenLive555";

  std::string error;
  bool saved = dogdouclix::CredManager::SaveProfile(prof, &error);
  TEST_ASSERT(saved, "SaveProfile for integration test succeeded");

  wchar_t temppath[MAX_PATH] = {0};
  ::GetTempPathW(MAX_PATH, temppath);

  std::wstring cfgpath = std::wstring(temppath) + L"test_profile_int.clix.json";
  std::ofstream outcfg(cfgpath, std::ios::binary);
  outcfg << R"({
    "target": "cmd.exe",
    "cwd": "C:\\Temp",
    "user": {
      "profile": "TEST_CONFIG_INT_PROFILE"
    },
    "env_set": {
      "MY_CONFIG_VAR": "ConfigValue123"
    }
  })";
  outcfg.close();

  auto cfg = dogdouclix::ConfigParser::ParseFile(cfgpath);
  TEST_ASSERT(cfg.has_value(), "ParseFile with profile reference succeeded");
  TEST_ASSERT(cfg->UserContext.has_value(), "UserContext resolved");
  TEST_ASSERT(cfg->UserContext->Username.has_value() && *cfg->UserContext->Username == L"TestAdminUser", "Username injected from profile");
  TEST_ASSERT(cfg->UserContext->Domain.has_value() && *cfg->UserContext->Domain == L"CORPDOM", "Domain injected from profile");
  TEST_ASSERT(cfg->UserContext->Password.has_value() && *cfg->UserContext->Password == L"VaultTokenLive555", "Password injected from profile");
  TEST_ASSERT(cfg->WorkingDirectory.has_value() && *cfg->WorkingDirectory == L"C:\\Temp", "CWD comes from file");
  TEST_ASSERT(cfg->EnvMutations.size() == 1, "Env mutations come from file");

  dogdouclix::CredManager::DeleteProfile(L"TEST_CONFIG_INT_PROFILE");
  ::DeleteFileW(cfgpath.c_str());
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

static void TestJsonNestedStringBrackets() {
  std::string json = R"({
    "unknown_object": {
      "nested_key": "{nested_bracket_string}",
      "another_key": "[nested_array_string]"
    },
    "target": "C:\\Tools\\app.exe",
    "user": {
      "username": "TestUser",
      "logon_type": "batch"
    }
  })";

  auto cfg = dogdouclix::ConfigParser::ParseJson(json);
  TEST_ASSERT(cfg.has_value(), "JSON parse with nested string brackets succeeded");
  TEST_ASSERT(cfg->Target.has_value() && *cfg->Target == L"C:\\Tools\\app.exe", "Target parsed after nested brackets skipped");
  TEST_ASSERT(cfg->UserContext.has_value() && cfg->UserContext->Username == L"TestUser", "UserContext parsed after nested brackets");
  TEST_ASSERT(cfg->UserContext->LogonType.has_value() && *cfg->UserContext->LogonType == LOGON32_LOGON_BATCH, "LogonType batch parsed correctly");
}

static void TestTransparentShimCliOverride() {
  wchar_t temppath[MAX_PATH] = {0};
  ::GetTempPathW(MAX_PATH, temppath);

  std::wstring dummyexe = std::wstring(temppath) + L"shim_cli_test.exe";
  std::wstring dummycfg = std::wstring(temppath) + L"shim_cli_test.clix.json";

  std::ofstream outcfg(dummycfg, std::ios::binary);
  outcfg << R"({
    "target": "cmd.exe",
    "env_set": {
      "DEFAULT_FLAG": "InitVal"
    }
  })";
  outcfg.close();

  const wchar_t* rawcmd = L"shim_cli_test.exe --clix-env-set OVERRIDE_FLAG=InjectedViaCli /c exit 0";
  wchar_t a0[] = L"shim_cli_test.exe";
  wchar_t a1[] = L"--clix-env-set";
  wchar_t a2[] = L"OVERRIDE_FLAG=InjectedViaCli";
  wchar_t a3[] = L"/c";
  wchar_t a4[] = L"exit";
  wchar_t a5[] = L"0";
  wchar_t* argv[] = { a0, a1, a2, a3, a4, a5 };

  auto options = dogdouclix::Forwarder::ParseCommandLine(6, argv, rawcmd, dummyexe);
  TEST_ASSERT(options.has_value(), "ParseCommandLine transparent mode with CLI override succeeded");
  TEST_ASSERT(options->IsTransparentMode, "IsTransparentMode is true");
  TEST_ASSERT(options->TargetExecutable == L"cmd.exe", "Target executable is cmd.exe");
  TEST_ASSERT(options->ContextOptions.EnvMutations.size() == 2, "2 env mutations loaded (1 from config, 1 from CLI)");
  TEST_ASSERT(options->FullCommandLine.find(L"/c exit 0") != std::wstring::npos, "Child command line contains tail args");

  ::DeleteFileW(dummycfg.c_str());
}

static void TestTransparentShimShortFlagsPassthrough() {
  wchar_t temppath[MAX_PATH] = {0};
  ::GetTempPathW(MAX_PATH, temppath);

  std::wstring dummyexe = std::wstring(temppath) + L"shim_flag_test.exe";
  std::wstring dummycfg = std::wstring(temppath) + L"shim_flag_test.clix.json";

  std::ofstream outcfg(dummycfg, std::ios::binary);
  outcfg << R"({ "target": "cmd.exe" })";
  outcfg.close();

  // Test -h in transparent shim mode is NOT intercepted and passed through as tail argument
  {
    const wchar_t* rawcmd = L"shim_flag_test.exe -h";
    wchar_t a0[] = L"shim_flag_test.exe";
    wchar_t a1[] = L"-h";
    wchar_t* argv[] = { a0, a1 };

    auto options = dogdouclix::Forwarder::ParseCommandLine(2, argv, rawcmd, dummyexe);
    TEST_ASSERT(options.has_value(), "ParseCommandLine transparent mode with -h returns options (not intercepted)");
    TEST_ASSERT(options->IsTransparentMode, "IsTransparentMode is true");
    TEST_ASSERT(options->FullCommandLine.find(L"-h") != std::wstring::npos, "Full command line contains -h for target");
  }

  // Test -V in transparent shim mode is NOT intercepted and passed through as tail argument
  {
    const wchar_t* rawcmd = L"shim_flag_test.exe -V";
    wchar_t a0[] = L"shim_flag_test.exe";
    wchar_t a1[] = L"-V";
    wchar_t* argv[] = { a0, a1 };

    auto options = dogdouclix::Forwarder::ParseCommandLine(2, argv, rawcmd, dummyexe);
    TEST_ASSERT(options.has_value(), "ParseCommandLine transparent mode with -V returns options (not intercepted)");
    TEST_ASSERT(options->IsTransparentMode, "IsTransparentMode is true");
    TEST_ASSERT(options->FullCommandLine.find(L"-V") != std::wstring::npos, "Full command line contains -V for target");
  }

  // Test --clix-help in transparent shim mode IS intercepted
  {
    const wchar_t* rawcmd = L"shim_flag_test.exe --clix-help";
    wchar_t a0[] = L"shim_flag_test.exe";
    wchar_t a1[] = L"--clix-help";
    wchar_t* argv[] = { a0, a1 };

    auto options = dogdouclix::Forwarder::ParseCommandLine(2, argv, rawcmd, dummyexe);
    TEST_ASSERT(!options.has_value(), "ParseCommandLine transparent mode with --clix-help is intercepted");
  }

  ::DeleteFileW(dummycfg.c_str());
}

static void TestTemplateGeneration() {
  std::string json = dogdouclix::ConfigParser::GenerateTemplateJson();
  TEST_ASSERT(!json.empty(), "GenerateTemplateJson produces non-empty string");
  TEST_ASSERT(json.find("$schema") != std::string::npos, "Template JSON includes $schema");

  auto parsedjson = dogdouclix::ConfigParser::ParseJson(json);
  TEST_ASSERT(parsedjson.has_value(), "Template JSON parses cleanly");
  TEST_ASSERT(parsedjson->Target.has_value() && parsedjson->Target == L"C:\\Windows\\System32\\notepad.exe", "Template JSON target is notepad.exe");
  TEST_ASSERT(parsedjson->WorkingDirectory.has_value() && parsedjson->WorkingDirectory == L"C:\\", "Template JSON CWD is C:\\");
  TEST_ASSERT(parsedjson->DesktopStation.has_value() && parsedjson->DesktopStation == L"winsta0\\default", "Template JSON desktop is winsta0\\default");
  TEST_ASSERT(parsedjson->Profile.has_value() && parsedjson->Profile == L"DeployAdmin", "Template JSON profile is DeployAdmin");
  TEST_ASSERT(parsedjson->UserContext.has_value() && parsedjson->UserContext->Username == L"TargetUser", "Template JSON username is TargetUser");
  TEST_ASSERT(parsedjson->EnvMutations.size() == 3, "Template JSON contains 3 env mutations (1 set, 2 remove)");

  std::string ini = dogdouclix::ConfigParser::GenerateTemplateIni();
  TEST_ASSERT(!ini.empty(), "GenerateTemplateIni produces non-empty string");
  TEST_ASSERT(ini.find("[target]") != std::string::npos, "Template INI includes [target] section");

  auto parsedini = dogdouclix::ConfigParser::ParseIni(ini);
  TEST_ASSERT(parsedini.has_value(), "Template INI parses cleanly");
  TEST_ASSERT(parsedini->Target.has_value() && parsedini->Target == L"C:\\Windows\\System32\\notepad.exe", "Template INI target is notepad.exe");
  TEST_ASSERT(parsedini->WorkingDirectory.has_value() && parsedini->WorkingDirectory == L"C:\\", "Template INI CWD is C:\\");
  TEST_ASSERT(parsedini->DesktopStation.has_value() && parsedini->DesktopStation == L"winsta0\\default", "Template INI desktop is winsta0\\default");
  TEST_ASSERT(parsedini->Profile.has_value() && parsedini->Profile == L"DeployAdmin", "Template INI profile is DeployAdmin");
  TEST_ASSERT(parsedini->UserContext.has_value() && parsedini->UserContext->Username == L"TargetUser", "Template INI username is TargetUser");
  TEST_ASSERT(parsedini->EnvMutations.size() == 3, "Template INI contains 3 env mutations (1 set, 2 remove)");
}

static void TestTemplateFileWriter() {
  wchar_t temppath[MAX_PATH] = {0};
  ::GetTempPathW(MAX_PATH, temppath);

  std::wstring tempjson = std::wstring(temppath) + L"test_template_file.clix.json";
  std::wstring tempini = std::wstring(temppath) + L"test_template_file.clix.ini";

  std::string errmsg;
  bool jsonwritten = dogdouclix::ConfigParser::WriteTemplateFile(tempjson, "json", &errmsg);
  TEST_ASSERT(jsonwritten, "WriteTemplateFile for JSON succeeds");

  auto parsedjson = dogdouclix::ConfigParser::ParseFile(tempjson);
  TEST_ASSERT(parsedjson.has_value(), "ParseFile on generated JSON file succeeds");
  ::DeleteFileW(tempjson.c_str());

  bool iniwritten = dogdouclix::ConfigParser::WriteTemplateFile(tempini, "ini", &errmsg);
  TEST_ASSERT(iniwritten, "WriteTemplateFile for INI succeeds");

  auto parsedini = dogdouclix::ConfigParser::ParseFile(tempini);
  TEST_ASSERT(parsedini.has_value(), "ParseFile on generated INI file succeeds");
  ::DeleteFileW(tempini.c_str());

  bool badformat = dogdouclix::ConfigParser::WriteTemplateFile(tempjson, "unsupported_fmt", &errmsg);
  TEST_ASSERT(!badformat, "WriteTemplateFile with unsupported format fails");
  TEST_ASSERT(!errmsg.empty(), "WriteTemplateFile with unsupported format produces error message");
}

static void TestScaffoldingCommandLine() {
  wchar_t temppath[MAX_PATH] = {0};
  ::GetTempPathW(MAX_PATH, temppath);

  std::wstring outjson = std::wstring(temppath) + L"clix_cli_scaffold_test.json";
  std::wstring outini = std::wstring(temppath) + L"clix_cli_scaffold_test.ini";

  // Test --clix-template json
  {
    wchar_t a0[] = L"dogdouclix.exe";
    wchar_t a1[] = L"--clix-template";
    wchar_t a2[] = L"json";
    wchar_t* argv[] = { a0, a1, a2 };
    const wchar_t* raw = L"dogdouclix.exe --clix-template json";
    auto opt = dogdouclix::Forwarder::ParseCommandLine(3, argv, raw);
    TEST_ASSERT(!opt.has_value(), "--clix-template json returns nullopt (intercepted command)");
  }

  // Test --clix-init json <outjson>
  {
    wchar_t a0[] = L"dogdouclix.exe";
    wchar_t a1[] = L"--clix-init";
    wchar_t a2[] = L"json";
    wchar_t a3[MAX_PATH] = {0};
    wcscpy_s(a3, outjson.c_str());
    wchar_t* argv[] = { a0, a1, a2, a3 };
    std::wstring raw = L"dogdouclix.exe --clix-init json " + outjson;
    auto opt = dogdouclix::Forwarder::ParseCommandLine(4, argv, raw.c_str());
    TEST_ASSERT(!opt.has_value(), "--clix-init json <path> returns nullopt (intercepted command)");

    auto parsed = dogdouclix::ConfigParser::ParseFile(outjson);
    TEST_ASSERT(parsed.has_value(), "Scaffolded JSON file is readable and valid");
    ::DeleteFileW(outjson.c_str());
  }

  // Test --clix-init ini <outini>
  {
    wchar_t a0[] = L"dogdouclix.exe";
    wchar_t a1[] = L"--clix-init";
    wchar_t a2[] = L"ini";
    wchar_t a3[MAX_PATH] = {0};
    wcscpy_s(a3, outini.c_str());
    wchar_t* argv[] = { a0, a1, a2, a3 };
    std::wstring raw = L"dogdouclix.exe --clix-init ini " + outini;
    auto opt = dogdouclix::Forwarder::ParseCommandLine(4, argv, raw.c_str());
    TEST_ASSERT(!opt.has_value(), "--clix-init ini <path> returns nullopt (intercepted command)");

    auto parsed = dogdouclix::ConfigParser::ParseFile(outini);
    TEST_ASSERT(parsed.has_value(), "Scaffolded INI file is readable and valid");
    ::DeleteFileW(outini.c_str());
  }
}

static void TestDiagnosticsCommandAndReport() {
  dogdouclix::FORWARDER_OPTIONS options;
  options.TargetExecutable = L"cmd.exe";
  options.FullCommandLine = L"cmd.exe /c exit 0";
  options.ContextOptions.WorkingDirectory = L"C:\\";
  options.ContextOptions.DesktopStation = L"winsta0\\default";
  options.ContextOptions.EnvMutations.push_back({
    L"DIAG_TEST_KEY",
    L"DiagTestVal",
    dogdouclix::EnvMutationSet
  });

  std::wstringstream outstream;
  std::wstringstream errstream;

  bool diagok = dogdouclix::Forwarder::RunDiagnostics(options, std::wstring(L"dummy_clix.json"), outstream, errstream);
  TEST_ASSERT(diagok, "RunDiagnostics returned true for valid options");

  std::wstring report = outstream.str();
  TEST_ASSERT(report.find(L"DogdouClix Diagnostic & Identity Probe Report") != std::wstring::npos, "Report title found");
  TEST_ASSERT(report.find(L"Computer Name") != std::wstring::npos, "Computer Name found in report");
  TEST_ASSERT(report.find(L"Caller User Account") != std::wstring::npos, "Caller User Account found in report");
  TEST_ASSERT(report.find(L"dummy_clix.json") != std::wstring::npos, "Config path found in report");
  TEST_ASSERT(report.find(L"cmd.exe") != std::wstring::npos, "Target executable found in report");
  TEST_ASSERT(report.find(L"DIAG_TEST_KEY=DiagTestVal") != std::wstring::npos, "Env mutation found in report");
  TEST_ASSERT(report.find(L"Probe Status         : SUCCESS") != std::wstring::npos, "Probe status success found in report");
  TEST_ASSERT(report.find(L"[PASS]") != std::wstring::npos, "PASS result found in report");
}

static void TestDiagnosticsCommandLineParsing() {
  // Test --clix-diag interception
  {
    wchar_t a0[] = L"dogdouclix.exe";
    wchar_t a1[] = L"--clix-diag";
    wchar_t* argv[] = { a0, a1 };
    const wchar_t* raw = L"dogdouclix.exe --clix-diag";
    auto opt = dogdouclix::Forwarder::ParseCommandLine(2, argv, raw);
    TEST_ASSERT(!opt.has_value(), "--clix-diag returns nullopt (intercepted command)");
  }

  // Test --clix-test interception
  {
    wchar_t a0[] = L"dogdouclix.exe";
    wchar_t a1[] = L"--clix-test";
    wchar_t* argv[] = { a0, a1 };
    const wchar_t* raw = L"dogdouclix.exe --clix-test";
    auto opt = dogdouclix::Forwarder::ParseCommandLine(2, argv, raw);
    TEST_ASSERT(!opt.has_value(), "--clix-test returns nullopt (intercepted command)");
  }
}

static void TestCrossUserExecutionAndEncoding() {
  wchar_t currentuser[256] = {0};
  DWORD usersize = 256;
  ::GetUserNameW(currentuser, &usersize);

  dogdouclix::FORWARDER_OPTIONS options;
  options.TargetExecutable = L"cmd.exe";
  options.FullCommandLine = L"cmd.exe /c echo DogdouClixUnicodeVerificationSuccess";

  dogdouclix::USER_CONTEXT_CONFIG usercfg;
  usercfg.Username = currentuser;
  options.ContextOptions.UserContext = usercfg;

  auto result = dogdouclix::Forwarder::Execute(options);
  TEST_ASSERT(result.Succeeded, "Execution with user context succeeded");
  TEST_ASSERT(result.ExitCode == 0, "Exit code is 0 for user context execution");
}

int main() {
  std::cout << "=== Running DogdouClix Core Test Suite ===\n";
  TestStringConversions();
  TestArgumentQuoting();
  TestConfigParserJson();
  TestConfigParserIni();
  TestJsonNestedStringBrackets();
  TestTemplateGeneration();
  TestTemplateFileWriter();
  TestScaffoldingCommandLine();
  TestTargetResolverSplitPath();
  TestTargetResolverPathPenetration();
  TestTargetResolverCompanionConfig();
  TestForwarderTransparentShimMode();
  TestTransparentShimCliOverride();
  TestTransparentShimShortFlagsPassthrough();
  TestCredManagerCrud();
  TestProfileConfigIntegration();
  TestEnvironmentBlockMutations();
  TestCommandLineParsing();
  TestProcessExecutionAndExitCodes();
  TestPipedStreamPassThrough();
  TestWorkingDirectorySwitching();
  TestEnvironmentIsolationAndMutation();
  TestDiagnosticsCommandAndReport();
  TestDiagnosticsCommandLineParsing();
  TestCrossUserExecutionAndEncoding();
  std::cout << "=== All Tests Passed Successfully! ===\n";
  return 0;
}