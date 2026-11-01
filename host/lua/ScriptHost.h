#pragma once

#include <string>

struct lua_State;

namespace pob {

// Owns PoB's main Lua state and drives its callbacks (docs/API.md §1–§2).
class ScriptHost {
public:
    ~ScriptHost();

    // Creates the state, registers the API, runs Launch.lua and OnInit.
    // Returns false on a fatal script error (see FatalError()).
    bool Start();
    void Shutdown();

    // One frame: subscripts, then OnFrame.
    void Frame();

    void KeyDown(int key, bool doubleClick);
    void KeyUp(int key);
    void Char(const std::string& ch);
    bool CanExit();

    // True while PoB has yielded coroutines (background work needs frames).
    bool HasActiveCoroutines() const { return activeCoroutines_; }
    bool HasFatalError() const { return !fatalError_.empty(); }
    const std::string& FatalError() const { return fatalError_; }

    lua_State* State() const { return L_; }

    // Pushes callback `name` (and self for main-object methods). Returns the number
    // of extra arguments pushed (0 or 1), or -1 if there is no such callback.
    int PushCallback(const char* name);
    // Calls the function pushed by PushCallback plus `nargs` arguments.
    bool Call(int nargs, int nresults);

    // Runs a Lua chunk in the main state and returns its result as a string
    // (or "error: ..."). Used by the --check-boot self test.
    std::string Eval(const char* code);

private:
    void ScanCoroutines();

    lua_State* L_ = nullptr;
    bool activeCoroutines_ = false;
    std::string fatalError_;
};

} // namespace pob
