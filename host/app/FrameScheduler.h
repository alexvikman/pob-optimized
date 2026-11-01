#pragma once

#include <chrono>
#include <filesystem>
#include <limits>
#include <string>

namespace pob {

// Frame pacing settings, read from host-settings.ini in the data directory.
struct LoopConfig {
    enum class Mode { Adaptive, Continuous };
    Mode mode = Mode::Adaptive;
    double maxFps = 60;         // cap while interacting or animating, any display
    int activeMs = 500;         // full rate after the last input (hover, tooltips)
    double animationStepPx = 1;  // slow animations: redraw once they moved this far (px)
    double idleFps = 4;         // focused, nothing happening (caret blink)
    double slowIdleFps = 0.5;   // focused, and the last frames were all identical
    int slowIdleAfter = 8;      // identical frames before switching to slowIdleFps
    double backgroundFps = 0;   // unfocused; 0 = only on events
    bool skipIdenticalFrames = true;
    bool vsync = true;
    // Not pacing, but kept in the same file: daily check for a newer host release.
    bool checkForAppUpdates = true;
};

// Reads the settings file, creating it with the defaults (documented) if missing.
LoopConfig LoadLoopConfig(const std::filesystem::path& file);

// Decides when PoB's OnFrame runs (docs/API.md §11). PoB redraws everything in
// immediate mode, so the only lever is how often frames happen:
//   - after input: up to maxFps,
//   - while content keeps changing: up to maxFps, or slower for slow animations
//     (one frame per animationStepPx of movement),
//   - while PoB has background coroutines: up to maxFps,
//   - focused and idle: idleFps, dropping to slowIdleFps once frames repeat,
//   - unfocused: backgroundFps; hidden/minimised: only events.
// Wake-ups (subscript results, finished image loads, expose/resize) always get
// the next frame, still subject to the maxFps cap.
class FrameScheduler {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr Clock::time_point kNever = Clock::time_point::max();

    explicit FrameScheduler(const LoopConfig& config) : config_(config) {}

    void OnInput(Clock::time_point now);
    void RequestFrame() { framePending_ = true; }
    void SetWindowState(bool visible, bool focused);

    // Call after every frame with the time it started. `changed`: the frame's draw
    // list differs from the previous frame's; `motion`: how far it moved, in
    // pixels (DrawList::MotionSincePrevious), infinity for any other change.
    void FrameDone(Clock::time_point frameStart, bool changed,
                   float motion = std::numeric_limits<float>::infinity());

    // When the next frame should run; kNever means "wait for events".
    Clock::time_point NextFrame(Clock::time_point now, bool busy) const;

    const LoopConfig& Config() const { return config_; }

private:
    static Clock::duration Interval(double fps);

    LoopConfig config_;
    Clock::time_point lastFrame_{};
    Clock::time_point activeUntil_{};
    Clock::duration animationInterval_{};
    bool framePending_ = true;
    bool visible_ = true;
    bool focused_ = true;
    int changedStreak_ = 0;
    int unchangedStreak_ = 0;
};

} // namespace pob
