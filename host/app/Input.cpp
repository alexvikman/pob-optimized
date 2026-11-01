#include "app/Input.h"

#include <cctype>
#include <cstring>

namespace pob {

namespace {

struct NamedKey {
    int key;
    const char* name;
};

constexpr NamedKey kNamedKeys[] = {
    {kKeyLeftButton, "LEFTBUTTON"}, {kKeyMiddleButton, "MIDDLEBUTTON"}, {kKeyRightButton, "RIGHTBUTTON"},
    {kKeyMouse4, "MOUSE4"},         {kKeyMouse5, "MOUSE5"},             {kKeyWheelUp, "WHEELUP"},
    {kKeyWheelDown, "WHEELDOWN"},   {kKeyBack, "BACK"},                 {kKeyTab, "TAB"},
    {kKeyReturn, "RETURN"},         {kKeyEscape, "ESCAPE"},             {kKeyShift, "SHIFT"},
    {kKeyCtrl, "CTRL"},             {kKeyAlt, "ALT"},                   {kKeyPause, "PAUSE"},
    {kKeyPageUp, "PAGEUP"},         {kKeyPageDown, "PAGEDOWN"},         {kKeyEnd, "END"},
    {kKeyHome, "HOME"},             {kKeyPrintScreen, "PRINTSCREEN"},   {kKeyInsert, "INSERT"},
    {kKeyDelete, "DELETE"},         {kKeyUp, "UP"},                     {kKeyDown, "DOWN"},
    {kKeyLeft, "LEFT"},             {kKeyRight, "RIGHT"},               {kKeyF1, "F1"},
    {kKeyF1 + 1, "F2"},             {kKeyF1 + 2, "F3"},                 {kKeyF1 + 3, "F4"},
    {kKeyF1 + 4, "F5"},             {kKeyF1 + 5, "F6"},                 {kKeyF1 + 6, "F7"},
    {kKeyF1 + 7, "F8"},             {kKeyF1 + 8, "F9"},                 {kKeyF1 + 9, "F10"},
    {kKeyF1 + 10, "F11"},           {kKeyF1 + 11, "F12"},               {kKeyF1 + 12, "F13"},
    {kKeyF1 + 13, "F14"},           {kKeyF15, "F15"},                   {kKeyNumLock, "NUMLOCK"},
    {kKeyScrollLock, "SCROLLLOCK"},
};

} // namespace

std::string KeyName(int key) {
    if (key > 0 && key < 128) return std::string(1, static_cast<char>(key));
    for (auto& k : kNamedKeys) {
        if (k.key == key) return k.name;
    }
    return "?";
}

int KeyForName(const char* name) {
    if (!name || !name[0]) return kKeyNone;
    if (name[1]) {
        for (auto& k : kNamedKeys) {
#ifdef _WIN32
            if (_stricmp(name, k.name) == 0) return k.key;
#else
            if (strcasecmp(name, k.name) == 0) return k.key;
#endif
        }
        return kKeyNone;
    }
    unsigned char c = static_cast<unsigned char>(name[0]);
    if (std::isalpha(c)) return std::tolower(c);
    if (c == ' ' || std::isdigit(c)) return c;
    return kKeyNone;
}

int KeyFromSdl(SDL_Keycode key) {
    if (key >= SDLK_A && key <= SDLK_Z) return static_cast<int>(key); // SDL keycodes are lowercase ASCII
    if (key >= SDLK_0 && key <= SDLK_9) return static_cast<int>(key);
    if (key >= SDLK_F1 && key <= SDLK_F12) return kKeyF1 + static_cast<int>(key - SDLK_F1);
    if (key >= SDLK_F13 && key <= SDLK_F15) return kKeyF1 + 12 + static_cast<int>(key - SDLK_F13);
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) return '1' + static_cast<int>(key - SDLK_KP_1);
    switch (key) {
    case SDLK_BACKSPACE: return kKeyBack;
    case SDLK_TAB: return kKeyTab;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return kKeyReturn;
    case SDLK_ESCAPE: return kKeyEscape;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: return kKeyShift;
    case SDLK_LCTRL:
    case SDLK_RCTRL: return kKeyCtrl;
#ifdef __APPLE__
    // ⌘ acts as Ctrl so PoB's shortcuts (copy, paste, undo, save) work natively.
    case SDLK_LGUI:
    case SDLK_RGUI: return kKeyCtrl;
#endif
    case SDLK_LALT:
    case SDLK_RALT: return kKeyAlt;
    case SDLK_PAUSE: return kKeyPause;
    case SDLK_SPACE: return ' ';
    case SDLK_PAGEUP: return kKeyPageUp;
    case SDLK_PAGEDOWN: return kKeyPageDown;
    case SDLK_END: return kKeyEnd;
    case SDLK_HOME: return kKeyHome;
    case SDLK_LEFT: return kKeyLeft;
    case SDLK_UP: return kKeyUp;
    case SDLK_RIGHT: return kKeyRight;
    case SDLK_DOWN: return kKeyDown;
    case SDLK_PRINTSCREEN: return kKeyPrintScreen;
    case SDLK_INSERT: return kKeyInsert;
    case SDLK_DELETE: return kKeyDelete;
    case SDLK_NUMLOCKCLEAR: return kKeyNumLock;
    case SDLK_SCROLLLOCK: return kKeyScrollLock;
    case SDLK_SEMICOLON: return ';';
    case SDLK_EQUALS:
    case SDLK_PLUS:
    case SDLK_KP_PLUS: return '+';
    case SDLK_COMMA: return ',';
    case SDLK_MINUS:
    case SDLK_KP_MINUS: return '-';
    case SDLK_PERIOD: return '.';
    case SDLK_SLASH: return '/';
    case SDLK_GRAVE: return '`';
    case SDLK_LEFTBRACKET: return '[';
    case SDLK_BACKSLASH: return '\\';
    case SDLK_RIGHTBRACKET: return ']';
    case SDLK_APOSTROPHE: return '\'';
    case SDLK_KP_0: return '0';
    default: return kKeyNone;
    }
}

int KeyFromMouseButton(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT: return kKeyLeftButton;
    case SDL_BUTTON_MIDDLE: return kKeyMiddleButton;
    case SDL_BUTTON_RIGHT: return kKeyRightButton;
    case SDL_BUTTON_X1: return kKeyMouse4;
    case SDL_BUTTON_X2: return kKeyMouse5;
    default: return kKeyNone;
    }
}

char ExtraCharForKey(int key) {
    switch (key) {
    case kKeyBack: return '\b';
    case kKeyTab: return '\t';
    case kKeyReturn: return '\r';
    case kKeyEscape: return 0x1b;
    default: return 0;
    }
}

} // namespace pob
