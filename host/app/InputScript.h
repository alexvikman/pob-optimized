#pragma once

#include <SDL3/SDL_keycode.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace pob {

class ScriptHost;

// Scripted input for the self test (--input FILE). Each line is one command;
// coordinates are window points. Events go through the SDL queue, so they take
// the same path as real input.
//
//   move X Y              mouse motion
//   click X Y [right]     press and release (left button by default)
//   dblclick X Y          two clicks in quick succession
//   key NAME [ctrl|cmd|shift|alt]...   SDL key name, e.g. "key v cmd"
//   text STRING           text input (rest of the line, UTF-8)
//   wheel N               N wheel steps (negative scrolls down)
//   wait MS               let frames run for MS milliseconds
//   eval LUA              run a line of Lua, append the result to the report
//   # comment
class InputScript {
public:
    bool Load(const std::filesystem::path& file, std::string& error);
    bool Empty() const { return lines_.empty(); }
    // Runs commands until one needs time to pass; returns true when finished.
    bool Step(ScriptHost& script, std::string& report);

private:
    std::vector<std::string> lines_;
    size_t next_ = 0;
    // Modifiers stay held until the next command, so PoB sees them in OnFrame.
    std::vector<SDL_Keycode> heldModifiers_;
    std::chrono::steady_clock::time_point resumeAt_{};
};

} // namespace pob
