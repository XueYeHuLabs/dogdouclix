#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <userenv.h>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <cstdint>
#include <cstdio>
#include <iostream>

#if defined(_DEBUG) || defined(DBG)
#define ASSERTF(exp, ...) \
  do { \
    if (!(exp)) { \
      std::fprintf(stderr, "[ASSERTION FAILED] %s at %s:%d\n", #exp, __FILE__, __LINE__); \
      __VA_OPT__(std::fprintf(stderr, "  Message: " __VA_ARGS__); std::fprintf(stderr, "\n");) \
      ::DebugBreak(); \
    } \
  } while (0)
#else
#define ASSERTF(exp, ...) ((void)0)
#endif

namespace dogdouclix {

typedef struct _HANDLE_DELETER {
  void operator()(HANDLE Handle) const noexcept {
    if (Handle != nullptr && Handle != INVALID_HANDLE_VALUE) {
      ::CloseHandle(Handle);
    }
  }
} HANDLE_DELETER;

using UniqueHandle = std::unique_ptr<void, HANDLE_DELETER>;

inline UniqueHandle MakeUniqueHandle(HANDLE Handle) noexcept {
  return UniqueHandle((Handle == INVALID_HANDLE_VALUE) ? nullptr : Handle);
}

typedef struct _FORWARDING_RESULT {
  DWORD ExitCode{0};
  bool Succeeded{false};
  std::string ErrorMessage;
} FORWARDING_RESULT;

std::wstring Utf8ToWide(std::string_view Utf8Input);
std::string WideToUtf8(std::wstring_view WideInput);
std::string GetLastErrorMessage(DWORD ErrorCode = ::GetLastError());

} // namespace dogdouclix