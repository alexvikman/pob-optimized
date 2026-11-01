// Unit tests for the frame pacing rules (app/FrameScheduler.h).

#include "app/FrameScheduler.h"

#include <cstdio>
#include <cstdlib>

using pob::FrameScheduler;
using pob::LoopConfig;
using namespace std::chrono_literals;

namespace {

int g_failures = 0;

void Expect(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
        ++g_failures;
    }
}
#define EXPECT(cond) Expect((cond), #cond, __LINE__)

using Clock = FrameScheduler::Clock;

double MsUntil(const FrameScheduler& s, Clock::time_point now, bool busy = false) {
    auto next = s.NextFrame(now, busy);
    if (next == FrameScheduler::kNever) return -1;
    return std::chrono::duration<double, std::milli>(next - now).count();
}

// Runs `n` frames, each `step` after the previous, with the given change flag.
Clock::time_point RunFrames(FrameScheduler& s, Clock::time_point t, int n, Clock::duration step, bool changed) {
    for (int i = 0; i < n; ++i) {
        s.FrameDone(t, changed);
        t += step;
    }
    return t;
}

} // namespace

int main() {
    LoopConfig cfg; // defaults: 60 fps cap, 500 ms active, 4 / 0.5 fps idle
    auto t0 = Clock::now();

    {   // A pending request runs at the cap, never faster.
        FrameScheduler s(cfg);
        s.FrameDone(t0, true);
        s.RequestFrame();
        double ms = MsUntil(s, t0);
        EXPECT(ms > 16 && ms < 17.5);
    }
    {   // Input keeps the full rate for activeMs, then drops to idle.
        FrameScheduler s(cfg);
        s.OnInput(t0);
        s.FrameDone(t0, true);
        EXPECT(MsUntil(s, t0 + 1ms) < 17);
        auto later = t0 + 600ms;
        s.FrameDone(later, false);
        double ms = MsUntil(s, later);
        EXPECT(ms > 240 && ms < 260); // 4 fps
    }
    {   // Identical frames switch to slow idle; a change switches back.
        FrameScheduler s(cfg);
        auto t = RunFrames(s, t0, 10, 250ms, false);
        double ms = MsUntil(s, t - 250ms);
        EXPECT(ms > 1990 && ms < 2010); // 0.5 fps
        s.FrameDone(t, true);
        EXPECT(MsUntil(s, t) > 240 && MsUntil(s, t) < 260);
    }
    {   // A blinking caret (change every second frame) stays at idleFps.
        FrameScheduler s(cfg);
        auto t = t0;
        for (int i = 0; i < 20; ++i) {
            s.FrameDone(t, i % 2 == 0);
            t += 250ms;
        }
        double ms = MsUntil(s, t - 250ms);
        EXPECT(ms > 240 && ms < 260);
    }
    {   // Consecutive changes without input are an animation: full rate.
        FrameScheduler s(cfg);
        auto t = RunFrames(s, t0, 3, 17ms, true);
        EXPECT(MsUntil(s, t - 17ms) < 17);
    }
    {   // Busy (PoB coroutines) runs at the cap even when idle.
        FrameScheduler s(cfg);
        auto t = RunFrames(s, t0, 10, 250ms, false);
        EXPECT(MsUntil(s, t - 250ms, true) < 17);
    }
    {   // Unfocused: only events (backgroundFps = 0) ...
        FrameScheduler s(cfg);
        s.SetWindowState(true, false);
        s.FrameDone(t0, false);
        EXPECT(MsUntil(s, t0) < 0);
        // ... but a wake-up still gets a frame.
        s.RequestFrame();
        EXPECT(MsUntil(s, t0) >= 0);
    }
    {   // Hidden or minimised: only events.
        FrameScheduler s(cfg);
        s.SetWindowState(false, true);
        s.FrameDone(t0, false);
        EXPECT(MsUntil(s, t0) < 0);
    }
    {   // Background rate when configured.
        LoopConfig bg = cfg;
        bg.backgroundFps = 1;
        FrameScheduler s(bg);
        s.SetWindowState(true, false);
        s.FrameDone(t0, false);
        double ms = MsUntil(s, t0);
        EXPECT(ms > 990 && ms < 1010);
    }
    {   // Continuous mode ignores idleness, like the original.
        LoopConfig c = cfg;
        c.mode = LoopConfig::Mode::Continuous;
        FrameScheduler s(c);
        auto t = RunFrames(s, t0, 20, 17ms, false);
        EXPECT(MsUntil(s, t - 17ms) < 17);
    }

    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("FrameScheduler: all tests passed\n");
    return EXIT_SUCCESS;
}
