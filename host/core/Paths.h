#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace pob {

namespace fs = std::filesystem;

struct HostPaths {
    // Read-only copy of PoB in repository layout (src/, runtime/, manifest.xml).
    fs::path bundle;
    // Writable install: script dir == runtime dir (docs/API.md §8.2).
    fs::path data;
    // Documents folder; PoB appends "Path of Building/" itself.
    std::optional<fs::path> user;
    std::string userError;
};

struct PathOverrides {
    std::optional<fs::path> bundle;
    std::optional<fs::path> data;
};

// Resolves all paths; returns an error message on failure.
std::optional<std::string> ResolvePaths(const PathOverrides& overrides, HostPaths& out);

// Forward slashes, no trailing slash, UTF-8: the format PoB expects (docs/API.md §3.6).
std::string ToLuaPath(const fs::path& p);

fs::path FromUtf8(std::string_view s);

} // namespace pob
