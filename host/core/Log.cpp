#include "core/Log.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace pob {

namespace {
std::mutex g_mutex;
FILE* g_file = nullptr;
} // namespace

void LogInit(const std::filesystem::path& file) {
    std::lock_guard lock(g_mutex);
    if (g_file) return;
#ifdef _WIN32
    g_file = _wfopen(file.c_str(), L"w");
#else
    g_file = std::fopen(file.c_str(), "w");
#endif
}

void LogLine(std::string_view line) {
    std::lock_guard lock(g_mutex);
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fputc('\n', stderr);
    if (g_file) {
        std::fwrite(line.data(), 1, line.size(), g_file);
        std::fputc('\n', g_file);
        std::fflush(g_file);
    }
}

void Log(const char* fmt, ...) {
    char buf[4096];
    va_list va;
    va_start(va, fmt);
    int n = std::vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    if (n < 0) return;
    if (static_cast<size_t>(n) < sizeof(buf)) {
        LogLine(std::string_view(buf, n));
        return;
    }
    std::string big(n + 1, '\0');
    va_start(va, fmt);
    std::vsnprintf(big.data(), big.size(), fmt, va);
    va_end(va);
    big.resize(n);
    LogLine(big);
}

} // namespace pob
