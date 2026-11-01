#pragma once

#include <SDL3/SDL.h>

#include <string>

namespace pob {

// SimpleGraphic key codes: printable keys are their lowercase character (< 128),
// named keys start at 128 (engine/common/keylist.h). docs/API.md §4.1.
enum Key : int {
    kKeyNone = 0,
    kKeyLeftButton = 128,
    kKeyMiddleButton,
    kKeyRightButton,
    kKeyMouse4,
    kKeyMouse5,
    kKeyWheelUp,
    kKeyWheelDown,
    kKeyBack,
    kKeyTab,
    kKeyReturn,
    kKeyEscape,
    kKeyShift,
    kKeyCtrl,
    kKeyAlt,
    kKeyPause,
    kKeyPageUp,
    kKeyPageDown,
    kKeyEnd,
    kKeyHome,
    kKeyPrintScreen,
    kKeyInsert,
    kKeyDelete,
    kKeyUp,
    kKeyDown,
    kKeyLeft,
    kKeyRight,
    kKeyF1,
    kKeyF15 = kKeyF1 + 14,
    kKeyNumLock,
    kKeyScrollLock,
    kKeyCount
};

// Name passed to OnKeyDown/OnKeyUp; printable keys are a 1-char string.
std::string KeyName(int key);
// Inverse used by IsKeyDown (case-insensitive); returns kKeyNone if unknown.
int KeyForName(const char* name);

int KeyFromSdl(SDL_Keycode key);
int KeyFromMouseButton(Uint8 button);

// Character also delivered through OnChar when the key goes down (sys_main.cpp:343).
char ExtraCharForKey(int key);

} // namespace pob
