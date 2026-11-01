#include "app/FrameScheduler.h"

#include "core/Log.h"
#include "core/Paths.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace pob {

namespace {

constexpr const char* kDefaultSettings = R"(# Path of Building native host: frame pacing.
# PoB redraws its whole UI every frame, so these values decide how much CPU/GPU
# the app uses. Delete this file to restore the defaults.

# adaptive = event driven (default); continuous = redraw every frame like the
# original Windows build (for comparison).
mode = adaptive

# Frame rate cap while you interact or something animates, on any display.
maxFps = 60

# Keep the full rate this long after the last mouse/keyboard input (ms).
activeMs = 500

# Focused but idle: enough for the blinking text caret.
idleFps = 4

# Focused and the last frames were identical: slow down further.
slowIdleFps = 0.5
slowIdleAfter = 8

# Window not focused (0 = only redraw on events).
backgroundFps = 0

# Skip GPU work when a frame is identical to the one on screen.
skipIdenticalFrames = true

vsync = true
)";

std::string Trim(std::string s) {
    auto issp = [](unsigned char c) { return std::isspace(c); };
    s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), issp));
    s.erase(std::find_if_not(s.rbegin(), s.rend(), issp).base(), s.end());
    return s;
}

} // namespace

LoopConfig LoadLoopConfig(const std::filesystem::path& file) {
    LoopConfig cfg;
    std::ifstream in(file);
    if (!in) {
        std::ofstream(file) << kDefaultSettings;
        return cfg;
    }
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line.substr(0, line.find('#')));
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        auto num = [&](auto& out) {
            std::istringstream ss(val);
            ss >> out;
        };
        auto flag = [&](bool& out) { out = val == "true" || val == "1" || val == "yes"; };
        if (key == "mode") cfg.mode = val == "continuous" ? LoopConfig::Mode::Continuous : LoopConfig::Mode::Adaptive;
        else if (key == "maxFps") num(cfg.maxFps);
        else if (key == "activeMs") num(cfg.activeMs);
        else if (key == "idleFps") num(cfg.idleFps);
        else if (key == "slowIdleFps") num(cfg.slowIdleFps);
        else if (key == "slowIdleAfter") num(cfg.slowIdleAfter);
        else if (key == "backgroundFps") num(cfg.backgroundFps);
        else if (key == "skipIdenticalFrames") flag(cfg.skipIdenticalFrames);
        else if (key == "vsync") flag(cfg.vsync);
        else Log("%s: unknown setting '%s'", ToLuaPath(file).c_str(), key.c_str());
    }
    cfg.maxFps = std::clamp(cfg.maxFps, 1.0, 1000.0);
    return cfg;
}

FrameScheduler::Clock::duration FrameScheduler::Interval(double fps) {
    return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / fps));
}

void FrameScheduler::OnInput(Clock::time_point now) {
    activeUntil_ = now + std::chrono::milliseconds(config_.activeMs);
    framePending_ = true;
}

void FrameScheduler::SetWindowState(bool visible, bool focused) {
    if (visible != visible_ || focused != focused_) framePending_ = true;
    visible_ = visible;
    focused_ = focused;
}

void FrameScheduler::FrameDone(Clock::time_point frameStart, bool changed) {
    lastFrame_ = frameStart;
    framePending_ = false;
    if (changed) {
        ++changedStreak_;
        unchangedStreak_ = 0;
    } else {
        changedStreak_ = 0;
        ++unchangedStreak_;
    }
}

FrameScheduler::Clock::time_point FrameScheduler::NextFrame(Clock::time_point now, bool busy) const {
    const auto earliest = lastFrame_ + Interval(config_.maxFps);
    if (config_.mode == LoopConfig::Mode::Continuous) return earliest;
    if (framePending_ || busy || now < activeUntil_) return earliest;
    // Content changed on consecutive frames without input: an animation.
    if (changedStreak_ >= 2) return earliest;
    if (!visible_) return kNever;
    if (!focused_) return config_.backgroundFps > 0 ? lastFrame_ + Interval(config_.backgroundFps) : kNever;
    double fps = unchangedStreak_ >= config_.slowIdleAfter ? config_.slowIdleFps : config_.idleFps;
    return fps > 0 ? std::max(earliest, lastFrame_ + Interval(fps)) : kNever;
}

} // namespace pob
