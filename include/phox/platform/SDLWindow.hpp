#ifndef PHOX_SDLWINDOW_HPP
#define PHOX_SDLWINDOW_HPP

#include "phox/Window.hpp"

struct SDL_Window;

namespace phox {
class SDLWindow : public Window {
    
private:
    SDL_Window *window = nullptr;
};
}

#endif // PHOX_SDLWINDOW_HPP