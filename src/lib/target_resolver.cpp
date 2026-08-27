#include "dogdouclix/target_resolver.hpp"
#include <algorithm>

namespace dogdouclix {

namespace {

static bool FileExists(const std::wstring& Path) {
  DWORD attrs = ::GetFileAttributesW(Path.c_str());
  return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

static std::wstring NormalizePath(const std::wstring& Path) {
  DWORD needed = ::GetFullPathNameW(Path.c_str(), 0, nullptr, nullptr);
  if (needed > 0) {
    std::wstring result(needed, L'\0');
    DWORD len = ::GetFullPathNameW(Path.c_str(), needed, result.data(), nullptr);
    if (len > 0 && len < needed) {
      result.resize(len);
      std::transform(result.begin(), result.end(), result.begin(), ::towlower);
      while (!result.empty() && (result.back() == L'\\' || result.back() == L'/')) {
        result.pop_back();
      }
      return result;
    }
  }
  std::wstring lower = Path;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
  while (!lower.empty() && (lower.back() == L'\\' || lower.back() == L'/')) {
    lower.pop_back();
  }
  return lower;
}

} // namespace

std::wstring TargetResolver::GetCurrentExecutablePath() {
  DWORD capacity = 1024;
  while (capacity <= 32768) {
    std::wstring buffer(capacity, L'\0');
    DWORD len = ::GetModuleFileNameW(nullptr, buffer.data(), capacity);
    if (len == 0) {
      break;
    }
    if (len < capacity) {
      buffer.resize(len);
      return buffer;
    }
    capacity *= 2;
  }
  return std::wstring();
}

void TargetResolver::SplitPath(
  const std::wstring& FullPath,
  std::wstring& OutDir,
  std::wstring& OutFilename,
  std::wstring& OutBasename
) {
  size_t pos = FullPath.find_last_of(L"\\/");
  if (pos != std::wstring::npos) {
    OutDir = FullPath.substr(0, pos);
    OutFilename = FullPath.substr(pos + 1);
  } else {
    OutDir = L".";
    OutFilename = FullPath;
  }

  size_t dotpos = OutFilename.find_last_of(L'.');
  if (dotpos != std::wstring::npos) {
    OutBasename = OutFilename.substr(0, dotpos);
  } else {
    OutBasename = OutFilename;
  }
}

std::optional<std::wstring> TargetResolver::PenetratePath(
  const std::wstring& ExeName,
  const std::wstring& SkipDir
) {
  std::wstring normalizedskip = NormalizePath(SkipDir);

  DWORD pathlen = ::GetEnvironmentVariableW(L"PATH", nullptr, 0);
  if (pathlen == 0) {
    return std::nullopt;
  }

  std::vector<wchar_t> pathbuf(pathlen);
  ::GetEnvironmentVariableW(L"PATH", pathbuf.data(), pathlen);

  std::wstring pathstr(pathbuf.data());
  size_t start = 0;
  while (start < pathstr.size()) {
    size_t end = pathstr.find(L';', start);
    if (end == std::wstring::npos) {
      end = pathstr.size();
    }

    std::wstring entry = pathstr.substr(start, end - start);
    start = end + 1;

    while (!entry.empty() && (entry.front() == L' ' || entry.front() == L'\t')) {
      entry.erase(entry.begin());
    }
    while (!entry.empty() && (entry.back() == L' ' || entry.back() == L'\t' || entry.back() == L'\\' || entry.back() == L'/')) {
      entry.pop_back();
    }

    if (entry.empty()) {
      continue;
    }

    std::wstring normalizedentry = NormalizePath(entry);
    if (normalizedentry == normalizedskip) {
      // Skip self directory to avoid infinite recursive execution
      continue;
    }

    std::wstring cand = entry + L"\\" + ExeName;
    if (FileExists(cand)) {
      return cand;
    }

    if (!ExeName.ends_with(L".exe")) {
      std::wstring candexe = cand + L".exe";
      if (FileExists(candexe)) {
        return candexe;
      }
    }
  }

  return std::nullopt;
}

std::optional<TARGET_RESOLUTION> TargetResolver::Resolve(const std::wstring& CustomExePath) {
  std::wstring selfpath = CustomExePath.empty() ? GetCurrentExecutablePath() : CustomExePath;
  if (selfpath.empty()) {
    return std::nullopt;
  }

  std::wstring dir;
  std::wstring filename;
  std::wstring basename;
  SplitPath(selfpath, dir, filename, basename);

  TARGET_RESOLUTION res;
  bool isexplicit = (::_wcsnicmp(basename.c_str(), L"dogdouclix", 10) == 0);
  res.IsTransparentShim = !isexplicit;

  // Discover companion configuration files
  std::vector<std::wstring> configcandidates = {
    dir + L"\\" + filename + L".clix.json",
    dir + L"\\" + basename + L".clix.json",
    dir + L"\\" + filename + L".clix.ini",
    dir + L"\\" + basename + L".clix.ini",
    dir + L"\\clix.json",
    dir + L"\\clix.ini"
  };

  for (const auto& cfgpath : configcandidates) {
    if (FileExists(cfgpath)) {
      auto parsed = ConfigParser::ParseFile(cfgpath);
      if (parsed.has_value()) {
        res.LoadedConfig = *parsed;
        res.LoadedConfigPath = cfgpath;
        res.IsTransparentShim = true;
        break;
      }
    }
  }

  // 1. Target from companion configuration
  if (res.LoadedConfig.has_value() && res.LoadedConfig->Target.has_value()) {
    std::wstring target = *res.LoadedConfig->Target;
    if (FileExists(target)) {
      res.TargetExecutable = target;
      return res;
    }
    // Relative to companion config directory
    std::wstring reltarget = dir + L"\\" + target;
    if (FileExists(reltarget)) {
      res.TargetExecutable = reltarget;
      return res;
    }
    res.TargetExecutable = target;
    return res;
  }

  // 2. Target from DOGDOUCLIX_TARGET environment variable
  DWORD envlen = ::GetEnvironmentVariableW(L"DOGDOUCLIX_TARGET", nullptr, 0);
  if (envlen > 0) {
    std::wstring envbuf(envlen, L'\0');
    DWORD actuallen = ::GetEnvironmentVariableW(L"DOGDOUCLIX_TARGET", envbuf.data(), envlen);
    if (actuallen > 0 && actuallen < envlen) {
      envbuf.resize(actuallen);
      res.TargetExecutable = envbuf;
      res.IsTransparentShim = true;
      return res;
    }
  }

  // 3. Target from PATH penetration
  if (res.IsTransparentShim) {
    auto penetrated = PenetratePath(filename, dir);
    if (penetrated.has_value()) {
      res.TargetExecutable = *penetrated;
      return res;
    }
  }

  if (res.IsTransparentShim) {
    return res;
  }

  return std::nullopt;
}

} // namespace dogdouclix