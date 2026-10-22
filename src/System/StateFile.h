#pragma once

#include "Core/Board.h"
#include <filesystem>

namespace R2BOY::System
{
    // Três arquivos por hash de ROM, independentes dos saves de bateria (.sav).
    class StateFile
    {
    public:
        explicit StateFile(std::filesystem::path directory);
        std::filesystem::path pathFor(const Core::Board &board, int slot) const;
        void save(Core::Board &board, int slot) const;
        void load(Core::Board &board, int slot) const;

    private:
        std::filesystem::path directory_;
    };
}