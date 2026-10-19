#pragma once

#include <cstdint>

namespace R2BOY::Core::IO
{
    // Máscara lógica ativa em 1: direções no nibble baixo, ações no nibble alto.
    // Joypad converte essa representação para a leitura ativa em 0 de FF00.
    enum GameBoyButtons : std::uint8_t
    {
        BUTTON_RIGHT = 0x01,
        BUTTON_LEFT = 0x02,
        BUTTON_UP = 0x04,
        BUTTON_DOWN = 0x08,
        BUTTON_A = 0x10,
        BUTTON_B = 0x20,
        BUTTON_SELECT = 0x40,
        BUTTON_START = 0x80
    };
}