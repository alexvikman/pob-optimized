#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

struct lua_State;

namespace pob {

class ScriptHost;

// Values that may cross between Lua states (nil, boolean, number, string).
using SubValue = std::variant<std::monostate, bool, double, std::string>;
using SubValues = std::vector<SubValue>;

// A LaunchSubScript() worker: its own lua_State on its own thread (docs/API.md §5).
class SubScript {
public:
    SubScript(uint32_t id, std::string script, std::vector<std::string> funcs, std::vector<std::string> subs,
              SubValues args);
    ~SubScript();

    // Compiles the script; returns an error message if it does not compile.
    std::optional<std::string> Start();
    // Main thread: deliver queued calls and completion. Returns false once finished.
    bool Pump(ScriptHost& host);
    void Abort();
    bool Running() const { return !delivered_; }

private:
    struct Call {
        std::string name;
        SubValues args;
    };

    static int FuncThunk(lua_State* L);
    static int SubThunk(lua_State* L);
    void ThreadMain();

    uint32_t id_;
    std::string script_;
    std::vector<std::string> funcs_, subs_;
    SubValues args_;
    lua_State* L_ = nullptr;
    std::thread thread_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Call> subCalls_;
    std::optional<Call> funcCall_;
    std::optional<SubValues> funcResult_;
    bool aborting_ = false;

    std::atomic<bool> finished_{false};
    bool delivered_ = false;
    std::optional<std::string> error_;
    SubValues results_;
};

class SubScriptManager {
public:
    // Returns the new id, or an error message.
    std::variant<uint32_t, std::string> Launch(std::string script, const std::string& funcList,
                                               const std::string& subList, SubValues args);
    bool IsRunning(uint32_t id) const;
    // Returns false if the id is unknown.
    bool Abort(uint32_t id);
    void Pump(ScriptHost& host);
    bool Any() const { return !scripts_.empty(); }
    void Clear();

private:
    uint32_t nextId_ = 1;
    // A callback may abort the subscript that is delivering it; defer the delete.
    uint32_t pumping_ = 0;
    bool abortPumping_ = false;
    std::map<uint32_t, std::unique_ptr<SubScript>> scripts_;
};

// Pushes/reads values that may cross state boundaries.
void PushSubValues(lua_State* L, const SubValues& values);
// Reads stack values [first, top]; returns false (and sets `bad`) on a disallowed type.
bool ReadSubValues(lua_State* L, int first, SubValues& out, int* bad = nullptr);

} // namespace pob
