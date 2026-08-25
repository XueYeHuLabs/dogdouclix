#pragma once

#include "dogdouclix/common.hpp"
#include "dogdouclix/context_transform.hpp"
#include "dogdouclix/config_parser.hpp"
#include "dogdouclix/target_resolver.hpp"
#include "dogdouclix/process_launcher.hpp"

namespace dogdouclix {

typedef struct _FORWARDER_OPTIONS {
  std::wstring TargetExecutable;
  std::wstring FullCommandLine;
  CONTEXT_TRANSFORM_OPTIONS ContextOptions;
  bool IsTransparentMode{false};
} FORWARDER_OPTIONS;

class Forwarder {
public:
  // Executes the forwarding workflow
  static FORWARDING_RESULT Execute(const FORWARDER_OPTIONS& Options);

  // Parses command line and automatically selects explicit or transparent shim mode
  static std::optional<FORWARDER_OPTIONS> ParseCommandLine(
    int Argc,
    wchar_t* Argv[],
    const wchar_t* RawCommandLine,
    const std::wstring& CustomSelfExe = L""
  );

  // Formats full command line preserving verbatim quoting
  static std::wstring BuildTargetCommandLine(
    std::wstring_view TargetExe,
    std::wstring_view TailArgs
  );

  // Quotes a single argument according to standard Windows CommandLineToArgvW rules
  static std::wstring QuoteArgument(std::wstring_view Arg);

  ~Forwarder() = default;
  Forwarder() = default;
};

} // namespace dogdouclix