#include "dogdouclix/forwarder.hpp"
#include <iostream>

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

  return static_cast<int>(result.ExitCode);
}