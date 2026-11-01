#include "core/Install.h"

#include "core/Log.h"

#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

namespace pob {

namespace {

std::optional<std::string> ReadFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string DecodeEntities(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            auto rest = s.substr(i);
            struct { const char* ent; char ch; } map[] = {
                {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
            bool matched = false;
            for (auto& m : map) {
                if (rest.starts_with(m.ent)) {
                    out += m.ch;
                    i += std::strlen(m.ent) - 1;
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
        }
        out += s[i];
    }
    return out;
}

// Compares dotted version numbers ("2.67.2"); non-numeric parts compare as 0.
int CompareVersions(std::string_view a, std::string_view b) {
    auto next = [](std::string_view& s) {
        int v = 0;
        size_t i = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') v = v * 10 + (s[i++] - '0');
        while (i < s.size() && s[i] != '.') ++i;
        s.remove_prefix(i < s.size() ? i + 1 : i);
        return v;
    };
    while (!a.empty() || !b.empty()) {
        int x = next(a), y = next(b);
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

// Repository subdirectory of each manifest part (PathOfBuilding/manifest.cfg).
std::optional<fs::path> RepoDirForPart(const std::string& part) {
    if (part == "default") return fs::path();
    if (part == "program" || part == "tree") return fs::path("src");
    if (part == "runtime") return fs::path("runtime");
    return std::nullopt;
}

std::string ReplaceAll(std::string s, std::string_view from, std::string_view to) {
    for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size()) {
        s.replace(pos, from.size(), to);
    }
    return s;
}

} // namespace

const std::string* ManifestElement::Attr(const std::string& key) const {
    auto it = attrs.find(key);
    return it == attrs.end() ? nullptr : &it->second;
}

bool ParseManifest(std::string_view text, std::vector<ManifestElement>& out) {
    out.clear();
    bool sawRoot = false;
    size_t pos = 0;
    while ((pos = text.find('<', pos)) != std::string_view::npos) {
        size_t end = text.find('>', pos);
        if (end == std::string_view::npos) break;
        std::string_view tag = text.substr(pos + 1, end - pos - 1);
        pos = end + 1;
        if (tag.empty() || tag[0] == '?' || tag[0] == '!' || tag[0] == '/') continue;

        ManifestElement el;
        size_t i = 0;
        while (i < tag.size() && !std::isspace((unsigned char)tag[i]) && tag[i] != '/') ++i;
        el.tag = std::string(tag.substr(0, i));
        if (el.tag == "PoBVersion") {
            sawRoot = true;
            continue;
        }
        while (i < tag.size()) {
            while (i < tag.size() && (std::isspace((unsigned char)tag[i]) || tag[i] == '/')) ++i;
            size_t keyStart = i;
            while (i < tag.size() && tag[i] != '=' && !std::isspace((unsigned char)tag[i])) ++i;
            std::string key(tag.substr(keyStart, i - keyStart));
            while (i < tag.size() && tag[i] != '"' && tag[i] != '\'') ++i;
            if (i >= tag.size()) break;
            char quote = tag[i++];
            size_t valStart = i;
            while (i < tag.size() && tag[i] != quote) ++i;
            if (!key.empty()) el.attrs[key] = DecodeEntities(tag.substr(valStart, i - valStart));
            ++i;
        }
        out.push_back(std::move(el));
    }
    return sawRoot;
}

std::string ManifestVersion(const std::vector<ManifestElement>& elements) {
    for (auto& el : elements) {
        if (el.tag == "Version") {
            if (auto* n = el.Attr("number")) return *n;
        }
    }
    return {};
}

std::optional<std::string> EnsureInstalled(const HostPaths& paths) {
    auto bundleText = ReadFile(paths.bundle / "manifest.xml");
    std::vector<ManifestElement> bundle;
    if (!bundleText || !ParseManifest(*bundleText, bundle)) {
        return "Bundled manifest.xml is missing or invalid";
    }
    std::string bundleVersion = ManifestVersion(bundle);

    std::string installedVersion;
    if (auto text = ReadFile(paths.data / "manifest.xml")) {
        std::vector<ManifestElement> installed;
        if (ParseManifest(*text, installed)) installedVersion = ManifestVersion(installed);
    }
    if (!installedVersion.empty() && CompareVersions(installedVersion, bundleVersion) >= 0) {
        Log("Installed PoB %s is current (bundle %s)", installedVersion.c_str(), bundleVersion.c_str());
        return std::nullopt;
    }

    Log("Installing bundled PoB %s into %s (was: %s)", bundleVersion.c_str(), ToLuaPath(paths.data).c_str(),
        installedVersion.empty() ? "none" : installedVersion.c_str());

    size_t copied = 0;
    for (auto& el : bundle) {
        if (el.tag != "File") continue;
        // Windows-only binaries carry runtime="win32" (docs/API.md §8.4).
        if (el.Attr("runtime") || el.Attr("platform")) continue;
        const std::string* name = el.Attr("name");
        const std::string* part = el.Attr("part");
        if (!name || !part) continue;
        auto repoDir = RepoDirForPart(*part);
        if (!repoDir) continue;

        std::string fileName = ReplaceAll(*name, "{space}", " ");
        fs::path src = paths.bundle / *repoDir / FromUtf8(fileName);
        fs::path dst = paths.data / FromUtf8(fileName);
        std::error_code ec;
        fs::create_directories(dst.parent_path(), ec);
        if (!fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec) || ec) {
            return "Could not copy '" + ToLuaPath(src) + "': " + ec.message();
        }
        ++copied;
    }

    // The manifest is written last so an interrupted copy is retried on the next
    // start. It is the repository manifest for now: without branch/platform PoB
    // runs in dev mode and does not self-update (docs/API.md §8).
    std::ofstream out(paths.data / "manifest.xml", std::ios::binary);
    out << *bundleText;
    if (!out) return "Could not write manifest.xml";
    Log("Installed %zu files", copied);
    return std::nullopt;
}

} // namespace pob
