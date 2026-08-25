#include "dogdouclix/common.hpp"
#include "dogdouclix/context_transform.hpp"
#include "dogdouclix/forwarder.hpp"
#include <shellapi.h>
#include <iostream>
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
  // Create an anonymous pipe to capture child process stdout
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

  // Restore original stdout
  ::SetStdHandle(STD_OUTPUT_HANDLE, holdstdout);
  ::CloseHandle(hwritepipe);

  TEST_ASSERT(result.Succeeded, "Forwarder execution with piped stdout succeeded");
  TEST_ASSERT(result.ExitCode == 0, "Exit code is 0");

  // Read captured output from the read end of the pipe
  char readbuffer[256] = {0};
  DWORD bytesread = 0;
  BOOL readok = ::ReadFile(hreadpipe, readbuffer, sizeof(readbuffer) - 1, &bytesread, nullptr);
  ::CloseHandle(hreadpipe);

  TEST_ASSERT(readok, "ReadFile from anonymous pipe succeeded");
  std::string captured(readbuffer, bytesread);
  TEST_ASSERT(captured.find("PipedOutputVerificationString") != std::string::npos, "Piped stream captured expected child output");
}

int main() {
  std::cout << "=== Running DogdouClix Core Test Suite ===\n";
  TestStringConversions();
  TestArgumentQuoting();
  TestEnvironmentBlockMutations();
  TestCommandLineParsing();
  TestProcessExecutionAndExitCodes();
  TestPipedStreamPassThrough();
  std::cout << "=== All Tests Passed Successfully! ===\n";
  return 0;
}