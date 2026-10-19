#pragma once

#include "Interrupts.h"
#include <cstdint>

namespace R2BOY::Core::IO
{
    // Matriz JOYP: estado lógico ativo em 1, leitura de hardware ativa em 0.
    class Joypad
    {
    public:
        void reset()
        {
            selected_ = 0x30;
            buttons_ = 0;
        }
        std::uint8_t read() const;
        void write(std::uint8_t value, Interrupts &interrupts);
        void setButtons(std::uint8_t buttons, Interrupts &interrupts);

    private:
        void detectEdge(std::uint8_t previous, Interrupts &interrupts) const;
        std::uint8_t selected_ = 0x30;
        std::uint8_t buttons_ = 0;
    };
}
