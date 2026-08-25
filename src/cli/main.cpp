#include "dogdouclix/forwarder.hpp"
#include <iostream>
#include <cstring>

int wmain(int Argc, wchar_t* Argv[]) {
  const wchar_t* rawcommandline = ::GetCommandLineW();

  auto options = dogdouclix::Forwarder::ParseCommandLine(Argc, Argv, rawcommandline);
  if (!options.has_value()) {
    if (Argc < 2) {
      std::wcerr << L"dogdouclix: Missing target executable.\n"
                 << L"Try 'dogdouclix --clix-help' for more information.\n";
      return 1;
    }
    return 0;
  }

  auto result = dogdouclix::Forwarder::Execute(*options);
  if (!result.Succeeded && !result.ErrorMessage.empty()) {
    std::cerr << "dogdouclix error: " << result.ErrorMessage << "\n";
  }

  int exitcode = 0;
  static_assert(sizeof(DWORD) == sizeof(int), "size mismatch");
  std::memcpy(&exitcode, &result.ExitCode, sizeof(int));
  return exitcode;
}