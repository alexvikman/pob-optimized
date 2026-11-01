#include "app/InputScript.h"

#include "app/Context.h"
#include "core/Log.h"
#include "lua/ScriptHost.h"

#include <SDL3/SDL.h>

#include <fstream>
#include <sstream>

namespace pob {

namespace {

constexpr auto kSettleTime = std::chrono::milliseconds(100);

SDL_WindowID WindowId() {
    return Ctx().window ? SDL_GetWindowID(Ctx().window) : 0;
}

void Push(SDL_Event& ev) {
    if (!SDL_PushEvent(&ev)) Log("[input] SDL_PushEvent failed: %s", SDL_GetError());
}

void Motion(float x, float y) {
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.windowID = WindowId();
    ev.motion.x = x;
    ev.motion.y = y;
    Push(ev);
}

void Button(float x, float y, Uint8 button, bool down) {
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    ev.button.windowID = WindowId();
    ev.button.button = button;
    ev.button.down = down;
    ev.button.clicks = 1;
    ev.button.x = x;
    ev.button.y = y;
    Push(ev);
}

void Key(SDL_Keycode key, bool down) {
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    ev.key.windowID = WindowId();
    ev.key.key = key;
    ev.key.down = down;
    Push(ev);
}

SDL_Keycode ModifierKey(const std::string& name) {
    if (name == "ctrl") return SDLK_LCTRL;
    if (name == "cmd") return SDLK_LGUI;
    if (name == "shift") return SDLK_LSHIFT;
    if (name == "alt") return SDLK_LALT;
    return SDLK_UNKNOWN;
}

} // namespace

bool InputScript::Load(const std::filesystem::path& file, std::string& error) {
    std::ifstream in(file);
    if (!in) {
        error = "cannot open input script";
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        lines_.push_back(line);
    }
    return true;
}

bool InputScript::Step(ScriptHost& script, std::string& report) {
    auto now = std::chrono::steady_clock::now();
    if (now < resumeAt_) return false;
    for (auto it = heldModifiers_.rbegin(); it != heldModifiers_.rend(); ++it) Key(*it, false);
    heldModifiers_.clear();
    while (next_ < lines_.size()) {
        const std::string& line = lines_[next_++];
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;
        // Every command that injects events gives PoB a moment to process them.
        resumeAt_ = now + kSettleTime;
        if (cmd == "move") {
            float x, y;
            ss >> x >> y;
            Motion(x, y);
            return false;
        } else if (cmd == "click" || cmd == "dblclick") {
            float x, y;
            std::string which;
            ss >> x >> y >> which;
            Uint8 button = which == "right" ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
            Motion(x, y);
            for (int i = 0; i < (cmd == "dblclick" ? 2 : 1); ++i) {
                Button(x, y, button, true);
                Button(x, y, button, false);
            }
            return false;
        } else if (cmd == "key") {
            std::string name, mod;
            ss >> name;
            std::vector<SDL_Keycode> mods;
            while (ss >> mod) {
                if (SDL_Keycode m = ModifierKey(mod)) mods.push_back(m);
            }
            SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
            if (key == SDLK_UNKNOWN) report += "input: unknown key '" + name + "'\n";
            for (auto m : mods) Key(m, true);
            Key(key, true);
            Key(key, false);
            heldModifiers_ = mods;
            return false;
        } else if (cmd == "text") {
            std::string text = line.size() > 5 ? line.substr(5) : "";
            SDL_Event ev{};
            ev.type = SDL_EVENT_TEXT_INPUT;
            ev.text.windowID = WindowId();
            // Test-only: the copy is leaked, since the event may outlive this call.
            ev.text.text = SDL_strdup(text.c_str());
            Push(ev);
            return false;
        } else if (cmd == "wheel") {
            float n = 0;
            ss >> n;
            SDL_Event ev{};
            ev.type = SDL_EVENT_MOUSE_WHEEL;
            ev.wheel.windowID = WindowId();
            ev.wheel.y = n;
            Push(ev);
            return false;
        } else if (cmd == "wait") {
            int ms = 0;
            ss >> ms;
            resumeAt_ = now + std::chrono::milliseconds(ms);
            return false;
        } else if (cmd == "eval") {
            std::string code = line.size() > 5 ? line.substr(5) : "";
            report += "input eval: " + script.Eval(code.c_str()) + "\n";
        } else {
            report += "input: unknown command '" + cmd + "'\n";
        }
    }
    return now >= resumeAt_;
}

} // namespace pob
