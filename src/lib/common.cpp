#include "dogdouclix/common.hpp"

namespace dogdouclix {

std::wstring Utf8ToWide(std::string_view Utf8Input) {
  if (Utf8Input.empty()) {
    return std::wstring();
  }
  int sizeneeded = ::MultiByteToWideChar(
    CP_UTF8,
    0,
    Utf8Input.data(),
    static_cast<int>(Utf8Input.size()),
    nullptr,
    0
  );
  if (sizeneeded <= 0) {
    return std::wstring();
  }
  std::wstring result(sizeneeded, L'\0');
  ::MultiByteToWideChar(
    CP_UTF8,
    0,
    Utf8Input.data(),
    static_cast<int>(Utf8Input.size()),
    result.data(),
    sizeneeded
  );
  return result;
}

std::string WideToUtf8(std::wstring_view WideInput) {
  if (WideInput.empty()) {
    return std::string();
  }
  int sizeneeded = ::WideCharToMultiByte(
    CP_UTF8,
    0,
    WideInput.data(),
    static_cast<int>(WideInput.size()),
    nullptr,
    0,
    nullptr,
    nullptr
  );
  if (sizeneeded <= 0) {
    return std::string();
  }
  std::string result(sizeneeded, '\0');
  ::WideCharToMultiByte(
    CP_UTF8,
    0,
    WideInput.data(),
    static_cast<int>(WideInput.size()),
    result.data(),
    sizeneeded,
    nullptr,
    nullptr
  );
  return result;
}

std::string GetLastErrorMessage(DWORD ErrorCode) {
  LPWSTR buffer = nullptr;
  DWORD size = ::FormatMessageW(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    nullptr,
    ErrorCode,
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
    reinterpret_cast<LPWSTR>(&buffer),
    0,
    nullptr
  );
  if (size == 0 || buffer == nullptr) {
    return "Unknown error (code " + std::to_string(ErrorCode) + ")";
  }
  std::wstring widemsg(buffer, size);
  ::LocalFree(buffer);
  while (!widemsg.empty() && (widemsg.back() == L'\r' || widemsg.back() == L'\n' || widemsg.back() == L' ')) {
    widemsg.pop_back();
  }
  return WideToUtf8(widemsg) + " (code " + std::to_string(ErrorCode) + ")";
}

} // namespace dogdouclix