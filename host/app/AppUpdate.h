#pragma once

#include "core/Paths.h"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace pob {

class DrawList;
class FontSet;

// Tells the user when a newer release of this host (not of PoB, which updates
// itself) is on GitHub. Checks at most once a day, in the background, and shows
// a small notice the host draws over PoB's UI: "Download" opens the release
// page, "×" hides it for that version. State lives in host-state.ini.
class AppUpdate {
public:
    AppUpdate(fs::path stateFile, bool enabled);
    ~AppUpdate();

    // Starts a background check if one is due. Call regularly from the main loop.
    void Poll();

    // Draws the notice (physical pixels) on top of everything; no-op when hidden.
    void Draw(DrawList& dl, const FontSet& fonts, float scale);

    // Mouse button press at physical pixel `px, py`. Returns true if the notice
    // handled it (PoB must not see it).
    bool HandleClick(float px, float py);
    bool Visible() const;

private:
    struct Rect {
        float x = 0, y = 0, w = 0, h = 0;
        bool Contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    };

    void LoadState();
    void SaveState() const;
    void CheckThread();

    fs::path stateFile_;
    bool enabled_;
    std::chrono::system_clock::time_point lastCheck_{};
    std::string dismissedVersion_;

    mutable std::mutex mutex_;
    std::thread thread_;
    bool checking_ = false;
    std::optional<std::string> latestVersion_;
    std::string releaseUrl_;

    Rect box_, download_, close_;
};

} // namespace pob
