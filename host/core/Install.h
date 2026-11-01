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

// Copies the bundled PoB (repository layout) into the data directory using the
// install layout (docs/API.md §8.1) when the data directory has no install yet or
// holds an older version. Returns an error message on failure.
std::optional<std::string> EnsureInstalled(const HostPaths& paths);

} // namespace pob
