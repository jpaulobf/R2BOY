#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "System/Engine.h"

int main(int, char **)
{
    R2BOY::System::Engine engine;
    return engine.run();
}
