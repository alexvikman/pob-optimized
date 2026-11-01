#include "app/Context.h"

namespace pob {

HostContext& Ctx() {
    static HostContext ctx;
    return ctx;
}

float HostContext::DisplayScale() const {
    if (!window) return 1.0f;
    float s = SDL_GetWindowDisplayScale(window);
    return s > 0.0f ? s : 1.0f;
}

float HostContext::Scale() const {
    if (!dpiAware) return 1.0f;
    if (dpiOverridePercent > 0) return dpiOverridePercent / 100.0f;
    return DisplayScale();
}

int HostContext::ScreenWidth() const {
    int w = 0, h = 0;
    if (window) SDL_GetWindowSizeInPixels(window, &w, &h);
    return dpiAware ? w : static_cast<int>(w / DisplayScale());
}

int HostContext::ScreenHeight() const {
    int w = 0, h = 0;
    if (window) SDL_GetWindowSizeInPixels(window, &w, &h);
    return dpiAware ? h : static_cast<int>(h / DisplayScale());
}

} // namespace pob
