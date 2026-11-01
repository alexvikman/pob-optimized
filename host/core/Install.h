#pragma once

#include "core/Paths.h"

#include <map>
#include <string>
#include <vector>

namespace pob {

// One self-closing element of a PoB manifest.xml, e.g. <File name="…" part="…" />.
struct ManifestElement {
    std::string tag;
    std::map<std::string, std::string> attrs;

    const std::string* Attr(const std::string& key) const;
};

// Parses the flat element list of a manifest (all elements are self-closing
// children of <PoBVersion>). Returns false if the text is not a manifest.
bool ParseManifest(std::string_view text, std::vector<ManifestElement>& out);

std::string ManifestVersion(const std::vector<ManifestElement>& elements);

// Our platform string in manifests (docs/API.md §8.4). Deliberately not "macos" or
// "win64" so it cannot collide with anything upstream adds.
#if defined(__APPLE__)
inline constexpr const char* kNativePlatform = "native-macos";
#elif defined(_WIN32)
inline constexpr const char* kNativePlatform = "native-win64";
#else
inline constexpr const char* kNativePlatform = "native-linux";
#endif

// Manifest part for runtime files that are not platform specific (lua/*.lua,
// fonts). They install next to the scripts, from PoB's runtime/ URL, without
// triggering the Windows "basic" update mode.
inline constexpr const char* kRuntimeNativePart = "runtime-native";

struct ManifestTransform {
    // Set Version branch/platform (for the local install manifest).
    std::string branch;
    // Drop files of other platforms instead of tagging them (local manifest).
    bool dropForeignFiles = false;
};

// Rewrites a PoB manifest for this host (docs/API.md §8.4):
//  - files marked runtime="X"/platform="X" for another platform get platform="X",
//    so UpdateCheck.lua's own filter skips them (or are dropped),
//  - other part="runtime" files move to part="runtime-native", with a matching
//    <Source> pointing at the runtime URL.
// Everything else is kept as is. Returns the input unchanged if it is not a manifest.
std::string TransformManifest(std::string_view text, const ManifestTransform& options);

// Copies the files a PoB checkout needs at runtime (manifest.xml and every
// non-Windows file it lists, in repository layout) to `out`, for packaging.
std::optional<std::string> StageBundle(const fs::path& repo, const fs::path& out);

// Copies the bundled PoB (repository layout) into the data directory using the
// install layout (docs/API.md §8.1) when the data directory has no install yet or
// holds an older version. Returns an error message on failure.
std::optional<std::string> EnsureInstalled(const HostPaths& paths);

} // namespace pob
