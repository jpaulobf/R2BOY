#include <SDL.h>
#include "Emulator.h"
#include "System/Engine.h"

int Emulator::run()
{
    Engine engine;
    return engine.run();
}

int main(int, char **)
{
    Emulator emulator;
    return emulator.run();
}
