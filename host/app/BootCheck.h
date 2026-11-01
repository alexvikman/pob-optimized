#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>

#include "app/InputScript.h"

namespace pob {

class ScriptHost;

// --check-boot self test, driven from the main loop once per frame:
//   1. PoB boots and settles without an error prompt,
//      (optionally: run --eval code, --input script, save --screenshot)
//   2. a subscript round-trips a func call, a sub call and its return values,
//   3. Restart() re-runs Launch.lua and PoB settles again.
class BootCheck {
public:
    struct Options {
        std::string evalCode;
        std::filesystem::path inputScript;
        std::filesystem::path screenshot;
        // Asks the app to capture the next frame; returns true once a capture
        // has been written to the given path.
        std::function<void()> requestCapture;
        std::function<bool(const std::filesystem::path&)> saveCapture;
    };
    explicit BootCheck(Options options);

    enum class Result { Running, Passed, Failed };

    // `settled`: boot frames done and no background work pending.
    Result Step(ScriptHost& script, bool settled);
    const std::string& Report() const { return report_; }
    const std::string& EvalCode() const { return options_.evalCode; }

private:
    enum class Phase { Boot, Settle, Input, Capture, SubScript, Restart, Done };

    bool CheckMainMenu(ScriptHost& script, const char* label);
    Result Fail(const std::string& why);

    Phase phase_ = Phase::Boot;
    Options options_;
    InputScript input_;
    std::string report_;
    std::chrono::steady_clock::time_point phaseStart_ = std::chrono::steady_clock::now();
};

} // namespace pob
