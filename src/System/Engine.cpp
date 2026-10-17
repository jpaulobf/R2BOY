#include "Engine.h"

#include "Window/Window.h"
#include <SDL.h>

int Engine::run()
{
    Window window;
    if (!window.open()) return 1;

    bool running = true;
    while (running)
    {
        window.pollEvents(running);
        if (!running) break;

        if (window.isMinimized())
        {
            SDL_Delay(16);
            continue;
        }

        // TODO: Advance Board when hardware execution is implemented.
        if (!window.renderFrame(running)) return 1;
        SDL_Delay(16); // Temporary pacing until hardware timing is implemented.
    }
    return 0;
}
