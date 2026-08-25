#pragma once

#include "dogdouclix/common.hpp"
#include "dogdouclix/context_transform.hpp"
#include "dogdouclix/process_launcher.hpp"

namespace dogdouclix {

typedef struct _FORWARDER_OPTIONS {
  std::wstring TargetExecutable;
  std::wstring FullCommandLine;
  CONTEXT_TRANSFORM_OPTIONS ContextOptions;
} FORWARDER_OPTIONS;

class Forwarder {
public:
  // Executes the forwarding workflow
  static FORWARDING_RESULT Execute(const FORWARDER_OPTIONS& Options);

  // Parses raw command line into FORWARDER_OPTIONS
  static std::optional<FORWARDER_OPTIONS> ParseCommandLine(
    int Argc,
    wchar_t* Argv[],
    const wchar_t* RawCommandLine
  );

  // Formats full command line preserving verbatim quoting
  static std::wstring BuildTargetCommandLine(
    std::wstring_view TargetExe,
    std::wstring_view TailArgs
  );

public:
  ~Forwarder() = default;
  Forwarder() = default;
};

} // namespace dogdouclix