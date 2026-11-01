#include "app/BootCheck.h"

#include "app/Context.h"
#include "lua/ScriptHost.h"

namespace pob {

namespace {

constexpr auto kPhaseTimeout = std::chrono::seconds(120);

// PoB shows errors as launch.promptMsg instead of crashing. After boot it opens
// "Unnamed build" (BUILD) or the mode saved in Settings.xml (usually LIST).
constexpr const char* kStateQuery = R"lua(
    if not launch then return "FAIL launch table missing" end
    if launch.promptMsg then return "FAIL prompt: " .. tostring(launch.promptMsg) end
    local m = launch.main
    if not m then return "FAIL main not loaded" end
    if m.mode ~= "LIST" and m.mode ~= "BUILD" then return "FAIL mode=" .. tostring(m.mode) end
    local entries = {}
    if m.mode == "LIST" then
        for _, e in ipairs(m.modes.LIST.list) do entries[#entries+1] = (e.folderName or e.buildName or "?") end
    end
    return string.format("OK mode=%s version=%s devMode=%s userPath=%s list=[%s]",
        tostring(m.mode), tostring(launch.versionNumber), tostring(launch.devMode), tostring(m.userPath),
        table.concat(entries, ","))
)lua";

constexpr const char* kStartSubScript = R"lua(
    _hostCheck = nil
    local id = LaunchSubScript([[
        local a = ...
        ConPrintf("subscript running, arg %d", a)
        local path = GetScriptPath()
        return path, a * 2, true
    ]], "GetScriptPath", "ConPrintf", 21)
    if not id then return "FAIL LaunchSubScript returned nil" end
    launch:RegisterSubScript(id, function(path, doubled, flag)
        _hostCheck = string.format("%s|%s|%s", tostring(path), tostring(doubled), tostring(flag))
    end)
    return "started"
)lua";

} // namespace

BootCheck::BootCheck(Options options) : options_(std::move(options)) {
    std::string error;
    if (!options_.inputScript.empty() && !input_.Load(options_.inputScript, error)) {
        report_ += "input: " + error + "\n";
    }
}

BootCheck::Result BootCheck::Fail(const std::string& why) {
    report_ += "FAIL: " + why + "\n";
    return Result::Failed;
}

bool BootCheck::CheckMainMenu(ScriptHost& script, const char* label) {
    std::string state = script.Eval(kStateQuery);
    report_ += std::string(label) + ": " + state + "\n";
    return state.rfind("OK", 0) == 0;
}

BootCheck::Result BootCheck::Step(ScriptHost& script, bool settled) {
    auto now = std::chrono::steady_clock::now();
    if (now - phaseStart_ > kPhaseTimeout) return Fail("timed out");

    switch (phase_) {
    case Phase::Boot:
        if (!settled) return Result::Running;
        if (!CheckMainMenu(script, "boot")) return Result::Failed;
        if (!options_.evalCode.empty()) report_ += "eval: " + script.Eval(options_.evalCode.c_str()) + "\n";
        phase_ = Phase::Settle;
        phaseStart_ = now;
        return Result::Running;
    case Phase::Settle:
        if (!settled) return Result::Running;
        phase_ = Phase::Input;
        phaseStart_ = now;
        [[fallthrough]];
    case Phase::Input:
        if (!input_.Step(script, report_) || !settled) return Result::Running;
        if (!options_.screenshot.empty()) {
            options_.requestCapture();
            phase_ = Phase::Capture;
            return Result::Running;
        }
        phase_ = Phase::Capture;
        [[fallthrough]];
    case Phase::Capture: {
        if (!options_.screenshot.empty()) {
            if (!options_.saveCapture(options_.screenshot)) return Result::Running;
            report_ += "screenshot: " + options_.screenshot.string() + "\n";
        }
        std::string started = script.Eval(kStartSubScript);
        if (started != "started") return Fail("subscript: " + started);
        phase_ = Phase::SubScript;
        phaseStart_ = now;
        return Result::Running;
    }
    case Phase::SubScript: {
        std::string got = script.Eval("return _hostCheck");
        if (got.empty()) return Result::Running;
        std::string expected = ToLuaPath(Ctx().scriptPath) + "|42|true";
        report_ += "subscript: " + got + "\n";
        if (got != expected) return Fail("expected " + expected);
        Ctx().restartRequested = true;
        phase_ = Phase::Restart;
        phaseStart_ = now;
        return Result::Running;
    }
    case Phase::Restart:
        if (!settled || script.Eval("return _hostCheck") != "") return Result::Running;
        if (!CheckMainMenu(script, "restart")) return Result::Failed;
        phase_ = Phase::Done;
        return Result::Passed;
    case Phase::Done:
        return Result::Passed;
    }
    return Result::Running;
}

} // namespace pob
