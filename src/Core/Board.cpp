#include "Board.h"

namespace R2BOY::Core
{
    Board::Board(Model model) : model_(model)
    {
    }

    Model Board::model() const
    {
        return model_;
    }

    ExecutionMode Board::executionMode() const
    {
        return executionMode_;
    }
}