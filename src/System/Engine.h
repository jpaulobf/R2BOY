#pragma once

#include "Core/Board.h"
#include "Audio.h"
#include "Input.h"

// Controls the application loop and, later, real-time hardware execution.
class Engine
{
public:
    int run();

private:
    Board board_;
    Input input_;
    Audio audio_;
};
