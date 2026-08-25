#include "dogdouclix/common.hpp"
#include "dogdouclix/context_transform.hpp"
#include "dogdouclix/forwarder.hpp"
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
}

static void TestProcessExecutionSmoke() {
  dogdouclix::FORWARDER_OPTIONS options;
  options.TargetExecutable = L"cmd.exe";
  options.FullCommandLine = L"cmd.exe /c exit 42";

  auto result = dogdouclix::Forwarder::Execute(options);
  TEST_ASSERT(result.Succeeded, "Process execution succeeded");
  TEST_ASSERT(result.ExitCode == 42, "Process exit code accurately forwarded (42)");
}

int main() {
  std::cout << "=== Running DogdouClix Core Test Suite ===\n";
  TestStringConversions();
  TestEnvironmentBlockMutations();
  TestCommandLineParsing();
  TestProcessExecutionSmoke();
  std::cout << "=== All Tests Passed Successfully! ===\n";
  return 0;
}