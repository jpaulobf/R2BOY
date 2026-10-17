#include <SDL.h>
#include "Emulator.h"
#include "Window/Window.h"

int Emulator::run()
{
    Window window;
    return window.run();
}

int main(int, char **)
{
    Emulator emulator;
    return emulator.run();
}
