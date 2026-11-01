#pragma once

#include <chrono>
#include <string>

namespace pob {

class ScriptHost;

// --check-boot self test, driven from the main loop once per frame:
//   1. PoB boots and settles without an error prompt,
//   2. a subscript round-trips a func call, a sub call and its return values,
//   3. Restart() re-runs Launch.lua and PoB settles again.
class BootCheck {
public:
    explicit BootCheck(std::string evalCode = {}) : evalCode_(std::move(evalCode)) {}

    enum class Result { Running, Passed, Failed };

    // `settled`: boot frames done and no background work pending.
    Result Step(ScriptHost& script, bool settled);
    const std::string& Report() const { return report_; }

private:
    enum class Phase { Boot, SubScript, Restart, Done };

    bool CheckMainMenu(ScriptHost& script, const char* label);
    Result Fail(const std::string& why);

    Phase phase_ = Phase::Boot;
    std::string evalCode_;
    std::string report_;
    std::chrono::steady_clock::time_point phaseStart_ = std::chrono::steady_clock::now();
};

} // namespace pob
