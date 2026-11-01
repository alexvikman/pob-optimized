#pragma once

#include <filesystem>
#include <string_view>

namespace pob {

// Opens the log file (in addition to stderr). Safe to call once at startup.
void LogInit(const std::filesystem::path& file);

// Thread-safe; appends a newline.
void LogLine(std::string_view line);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
void Log(const char* fmt, ...);

} // namespace pob
