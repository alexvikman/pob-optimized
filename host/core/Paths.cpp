#include "core/Paths.h"

#include <SDL3/SDL.h>

namespace pob {

namespace {

constexpr const char* kAppDirName = "PathOfBuilding-Native";

bool LooksLikeBundle(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p / "manifest.xml", ec) && fs::is_regular_file(p / "src" / "Launch.lua", ec);
}

} // namespace

fs::path FromUtf8(std::string_view s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string ToLuaPath(const fs::path& p) {
    auto u8 = p.generic_u8string();
    std::string s(u8.begin(), u8.end());
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

std::optional<std::string> ResolvePaths(const PathOverrides& overrides, HostPaths& out) {
    std::error_code ec;

    // Bundle: explicit override, then next to the executable (inside the .app on
    // macOS SDL_GetBasePath is Contents/Resources/), then the source tree.
    if (overrides.bundle) {
        out.bundle = *overrides.bundle;
    } else {
        if (const char* base = SDL_GetBasePath()) {
            fs::path candidate = FromUtf8(base) / "PathOfBuilding";
            if (LooksLikeBundle(candidate)) out.bundle = candidate;
        }
#ifdef POB_DEV_BUNDLE_DIR
        if (out.bundle.empty()) out.bundle = FromUtf8(POB_DEV_BUNDLE_DIR);
#endif
    }
    if (out.bundle.empty() || !LooksLikeBundle(out.bundle)) {
        return "Path of Building files not found (looked for manifest.xml and src/Launch.lua in '" +
               ToLuaPath(out.bundle) + "')";
    }
    out.bundle = fs::weakly_canonical(out.bundle, ec);

    if (overrides.data) {
        out.data = *overrides.data;
    } else {
        char* pref = SDL_GetPrefPath(nullptr, kAppDirName);
        if (!pref) return std::string("Could not determine application data directory: ") + SDL_GetError();
        out.data = FromUtf8(pref);
        SDL_free(pref);
    }
    fs::create_directories(out.data, ec);
    if (ec) return "Could not create data directory '" + ToLuaPath(out.data) + "': " + ec.message();
    out.data = fs::weakly_canonical(out.data, ec);

    if (const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)) {
        out.user = FromUtf8(docs);
    } else {
        out.userError = std::string("Could not obtain the Documents folder: ") + SDL_GetError();
    }
    return std::nullopt;
}

} // namespace pob
