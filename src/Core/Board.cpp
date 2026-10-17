#include "Board.h"

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
