#include "host_window.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <SDL.h>

SDL_Window* twine::host_window::create() {
    int width = 1600;
    int height = 900;
    SDL_Rect usable_bounds{};
    if (SDL_GetDisplayUsableBounds(0, &usable_bounds) == 0 &&
            usable_bounds.w > 0 && usable_bounds.h > 0) {
        width = std::min(width, usable_bounds.w);
        height = std::min(height, usable_bounds.h);
    }
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#ifndef _WIN32
    flags |= SDL_WINDOW_VULKAN;
#endif
    SDL_Window* window = SDL_CreateWindow(
        "The World Is Probably Enough",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        flags);
    if (window == nullptr) {
        throw std::runtime_error(
            std::string("Failed to create window: ") + SDL_GetError());
    }
    return window;
}
