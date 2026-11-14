#include "app/AppUpdate.h"

#include "app/Context.h"
#include "core/Install.h"
#include "core/Log.h"
#include "render/DrawList.h"
#include "render/Font.h"
#include "render/Text.h"

#include <SDL3/SDL.h>
#include <curl/curl.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace pob {

namespace {

constexpr auto kCheckInterval = std::chrono::hours(24);
constexpr const char* kLatestApi = "https://api.github.com/repos/" POB_HOST_REPO "/releases/latest";

// The release JSON is a few kilobytes; a response far beyond that is not it.
constexpr size_t kMaxResponse = 1 << 20;

size_t AppendBody(char* data, size_t size, size_t n, void* out) {
    auto* body = static_cast<std::string*>(out);
    if (body->size() + size * n > kMaxResponse) return 0; // aborts the transfer
    body->append(data, size * n);
    return size * n;
}

// Pulls one string field out of GitHub's JSON without a JSON library; the field
// read here (tag_name) never contains escaped quotes.
std::string JsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t k = json.find(needle);
    if (k == std::string::npos) return {};
    size_t colon = json.find(':', k + needle.size());
    if (colon == std::string::npos) return {};
    size_t q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return {};
    size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    return json.substr(q1 + 1, q2 - q1 - 1);
}

// The tag is the only thing taken from the response. It is drawn in the notice,
// written to the state file and put in the URL that "Download" opens, so only
// the form the release workflow produces ("v1.2.3") is accepted.
bool IsReleaseTag(std::string_view tag) {
    if (tag.starts_with('v')) tag.remove_prefix(1);
    if (tag.empty() || tag.size() > 32) return false;
    return std::all_of(tag.begin(), tag.end(), [](char c) { return (c >= '0' && c <= '9') || c == '.'; });
}

// The release page is built here, not read from the response, so "Download"
// only ever opens this repository's releases.
std::string ReleaseUrl(const std::string& tag) {
    return "https://github.com/" POB_HOST_REPO "/releases/tag/" + tag;
}

int64_t ToEpoch(std::chrono::system_clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
}

} // namespace

AppUpdate::AppUpdate(fs::path stateFile, bool enabled) : stateFile_(std::move(stateFile)), enabled_(enabled) {
    LoadState();
    // Test hook: pretend this version is the latest release, without the network.
    if (const char* fake = SDL_getenv("POB_HOST_TEST_LATEST_RELEASE")) {
        enabled_ = false;
        if (CompareVersions(fake, POB_HOST_VERSION) > 0 && dismissedVersion_ != fake) {
            latestVersion_ = fake;
            releaseUrl_ = "https://github.com/" POB_HOST_REPO "/releases";
        }
    }
}

AppUpdate::~AppUpdate() {
    if (thread_.joinable()) thread_.join();
}

void AppUpdate::LoadState() {
    std::ifstream in(stateFile_);
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        if (key == "lastAppUpdateCheck") {
            lastCheck_ = std::chrono::system_clock::time_point(std::chrono::seconds(std::atoll(val.c_str())));
        } else if (key == "dismissedAppVersion" && IsReleaseTag(val)) {
            dismissedVersion_ = val;
        }
    }
}

void AppUpdate::SaveState() const {
    std::ofstream out(stateFile_);
    out << "# Written by the host; safe to delete.\n";
    out << "lastAppUpdateCheck=" << ToEpoch(lastCheck_) << "\n";
    out << "dismissedAppVersion=" << dismissedVersion_ << "\n";
}

void AppUpdate::Poll() {
    if (!enabled_) return;
    std::lock_guard lock(mutex_);
    if (checking_) return;
    if (thread_.joinable()) thread_.join();
    auto now = std::chrono::system_clock::now();
    if (now - lastCheck_ < kCheckInterval) return;
    lastCheck_ = now;
    SaveState();
    checking_ = true;
    thread_ = std::thread([this] { CheckThread(); });
}

void AppUpdate::CheckThread() {
    std::string body;
    long status = 0;
    if (CURL* curl = curl_easy_init()) {
        curl_slist* headers = curl_slist_append(nullptr, "Accept: application/vnd.github+json");
        curl_easy_setopt(curl, CURLOPT_URL, kLatestApi);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "PathOfBuilding-Native/" POB_HOST_VERSION);
        // GitHub redirects the API of a renamed repository; follow that, over
        // HTTPS only. (libcurl before 7.85 lacks these options and keeps its
        // default, which allows a redirect to plain HTTP.)
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
#if LIBCURL_VERSION_NUM >= 0x075500
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#endif
        // This runs on a background thread; no signals for timeouts.
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AppendBody);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        CURLcode rc = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if (rc != CURLE_OK) Log("App update check failed: %s", curl_easy_strerror(rc));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    std::string tag = status == 200 ? JsonString(body, "tag_name") : "";
    if (!IsReleaseTag(tag)) tag.clear();
    {
        std::lock_guard lock(mutex_);
        checking_ = false;
        if (!tag.empty() && CompareVersions(tag, POB_HOST_VERSION) > 0 && tag != dismissedVersion_) {
            latestVersion_ = tag;
            releaseUrl_ = ReleaseUrl(tag);
        }
    }
    Log("App update check: latest %s (this is %s)", tag.empty() ? "unknown" : tag.c_str(), POB_HOST_VERSION);
    if (!tag.empty() && CompareVersions(tag, POB_HOST_VERSION) > 0) {
        if (auto& wake = Ctx().wake) wake();
    }
}

bool AppUpdate::Visible() const {
    std::lock_guard lock(mutex_);
    return latestVersion_.has_value();
}

void AppUpdate::Draw(DrawList& dl, const FontSet& fonts, float scale) {
    std::string version;
    {
        std::lock_guard lock(mutex_);
        if (!latestVersion_) return;
        version = *latestVersion_;
    }
    if (!version.empty() && version[0] == 'v') version.erase(0, 1);

    const Font& font = fonts.Get(1); // "VAR", PoB's UI font
    int textH = static_cast<int>(std::lround(16 * scale));
    textH = textH <= 1 ? 1 : (textH + 1) & ~1;
    auto u32 = [](const std::string& s) { return IndexUtf8(s).text; };
    std::u32string msg = u32("^7Path of Building (Native) " + version + " is available");
    std::u32string dl1 = u32("^7Download");
    std::u32string x = u32("^7x");

    float pad = 8 * scale, gap = 10 * scale, btnPad = 6 * scale;
    float msgW = static_cast<float>(font.StringWidth(textH, msg));
    float dlW = font.StringWidth(textH, dl1) + btnPad * 2;
    float xW = font.StringWidth(textH, x) + btnPad * 2;
    float h = textH + pad * 2;
    float w = pad + msgW + gap + dlW + gap / 2 + xW + pad;
    float margin = 12 * scale;
    box_ = {dl.ScreenWidth() - w - margin, dl.ScreenHeight() - h - margin - 30 * scale, w, h};
    download_ = {box_.x + pad + msgW + gap, box_.y + pad / 2, dlW, textH + pad};
    close_ = {download_.x + dlW + gap / 2, download_.y, xW, textH + pad};

    // Drawn above PoB's own layers, in PoB's look: white frame, dark fill.
    dl.SetLayer(30000, 0);
    dl.ResetViewport();
    auto rect = [&](const Rect& r, const Color& c) {
        const float xy[8] = {r.x, r.y, r.x + r.w, r.y, r.x + r.w, r.y + r.h, r.x, r.y + r.h};
        const float uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
        dl.AddQuad(nullptr, xy, uv, 0, c);
    };
    auto frame = [&](const Rect& r, const Color& border, const Color& fill) {
        rect(r, border);
        rect({r.x + scale, r.y + scale, r.w - 2 * scale, r.h - 2 * scale}, fill);
    };
    frame(box_, {0.85f, 0.65f, 0.25f, 1}, {0.08f, 0.08f, 0.08f, 0.95f});
    frame(download_, {1, 1, 1, 1}, {0.15f, 0.15f, 0.15f, 1});
    frame(close_, {1, 1, 1, 1}, {0.15f, 0.15f, 0.15f, 1});
    Color c{1, 1, 1, 1};
    font.Draw(dl, box_.x + pad, box_.y + pad, TextAlign::Left, textH, c, msg);
    font.Draw(dl, download_.x + btnPad, download_.y + pad / 2, TextAlign::Left, textH, c, dl1);
    font.Draw(dl, close_.x + btnPad, close_.y + pad / 2, TextAlign::Left, textH, c, x);
}

bool AppUpdate::HandleClick(float px, float py) {
    std::string url, version;
    {
        std::lock_guard lock(mutex_);
        if (!latestVersion_ || !box_.Contains(px, py)) return false;
        url = releaseUrl_;
        version = *latestVersion_;
    }
    if (download_.Contains(px, py)) {
        if (!url.empty()) SDL_OpenURL(url.c_str());
    } else if (close_.Contains(px, py)) {
        std::lock_guard lock(mutex_);
        dismissedVersion_ = version;
        latestVersion_.reset();
        SaveState();
    }
    return true;
}

} // namespace pob
