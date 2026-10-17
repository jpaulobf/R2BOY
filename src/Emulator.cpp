#include <SDL.h>
#include "System/Engine.h"

int main(int, char **)
{
    R2BOY::System::Engine engine;
    return engine.run();
}
