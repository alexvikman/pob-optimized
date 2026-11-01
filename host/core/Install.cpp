#include "core/Install.h"

#include "core/Log.h"

#include <algorithm>
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

std::string EscapeAttr(std::string_view s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

// Serialises one element, attributes in a stable order (known keys first).
std::string FormatElement(const ManifestElement& el) {
    static const char* kOrder[] = {"number", "name", "part", "platform", "runtime", "branch", "url", "sha1"};
    std::string out = "<" + el.tag;
    std::vector<std::string> done;
    auto emit = [&](const std::string& key, const std::string& val) {
        out += " " + key + "=\"" + EscapeAttr(val) + "\"";
        done.push_back(key);
    };
    for (const char* key : kOrder) {
        if (auto* v = el.Attr(key)) emit(key, *v);
    }
    for (auto& [key, val] : el.attrs) {
        if (std::find(done.begin(), done.end(), key) == done.end()) emit(key, val);
    }
    return out + " />";
}

} // namespace

std::string TransformManifest(std::string_view text, const ManifestTransform& options) {
    std::vector<ManifestElement> probe;
    if (!ParseManifest(text, probe)) return std::string(text);

    std::string out;
    out.reserve(text.size());
    std::string runtimeUrl;
    bool hasNativeSource = false;
    size_t lineStart = 0;
    while (lineStart < text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string_view::npos) lineEnd = text.size();
        std::string_view line = text.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;
        std::string_view trimmed = line.substr(line.find_first_not_of(" \t\r") == std::string_view::npos
                                                   ? line.size()
                                                   : line.find_first_not_of(" \t\r"));
        std::string indent(line.substr(0, line.size() - trimmed.size()));
        bool crlf = !line.empty() && line.back() == '\r';
        auto emitLine = [&](const std::string& s) { out += s + (crlf ? "\r\n" : "\n"); };

        std::vector<ManifestElement> els;
        bool isElement = trimmed.starts_with("<Version") || trimmed.starts_with("<Source") || trimmed.starts_with("<File");
        if (!isElement) {
            // The closing root tag: add the runtime-native source if it is still missing.
            if (trimmed.starts_with("</PoBVersion") && !hasNativeSource && !runtimeUrl.empty()) {
                emitLine(indent + "\t<Source part=\"" + kRuntimeNativePart + "\" url=\"" + EscapeAttr(runtimeUrl) + "\" />");
                hasNativeSource = true;
            }
            out += line;
            if (lineEnd < text.size()) out += '\n';
            continue;
        }
        std::string wrapped = "<PoBVersion>" + std::string(trimmed) + "</PoBVersion>";
        if (!ParseManifest(wrapped, els) || els.size() != 1) {
            out += line;
            if (lineEnd < text.size()) out += '\n';
            continue;
        }
        ManifestElement el = std::move(els[0]);

        if (el.tag == "Version") {
            if (!options.branch.empty()) {
                el.attrs["branch"] = options.branch;
                el.attrs["platform"] = kNativePlatform;
            }
        } else if (el.tag == "Source") {
            const std::string* part = el.Attr("part");
            if (part && *part == "runtime" && el.Attr("url")) runtimeUrl = *el.Attr("url");
            if (part && *part == kRuntimeNativePart) hasNativeSource = true;
        } else if (el.tag == "File") {
            const std::string* platform = el.Attr("platform");
            if (!platform) platform = el.Attr("runtime");
            if (platform && *platform != kNativePlatform) {
                if (options.dropForeignFiles) continue;
                el.attrs["platform"] = *platform;
            } else if (const std::string* part = el.Attr("part"); part && *part == "runtime") {
                el.attrs["part"] = kRuntimeNativePart;
            }
        }
        emitLine(indent + FormatElement(el));
    }
    return out;
}

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

    // installed.cfg makes PoB keep user data in Documents rather than next to the
    // scripts (Main.lua:86, docs/API.md §8.1).
    std::ofstream(paths.data / "installed.cfg", std::ios::binary).flush();

    // The manifest is written last so an interrupted copy is retried on the next
    // start. Branch "master" is where PoB publishes releases.
    ManifestTransform transform;
    transform.branch = "master";
    transform.dropForeignFiles = true;
    std::ofstream out(paths.data / "manifest.xml", std::ios::binary);
    out << TransformManifest(*bundleText, transform);
    if (!out) return "Could not write manifest.xml";
    Log("Installed %zu files (platform %s)", copied, kNativePlatform);
    return std::nullopt;
}

} // namespace pob
