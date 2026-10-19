#include "Joypad.h"

namespace R2BOY::Core::IO
{
    std::uint8_t Joypad::read() const
    {
        std::uint8_t lines = 0x0F;
        
        // Selecionar ambos os grupos combina suas linhas: qualquer pressão baixa o bit.
        if (!(selected_ & 0x10))
            lines &= static_cast<std::uint8_t>(~buttons_);
        
        if (!(selected_ & 0x20))
            lines &= static_cast<std::uint8_t>(~(buttons_ >> 4));
        
        return 0xC0 | selected_ | lines;
    }

    void Joypad::detectEdge(std::uint8_t previous, Interrupts &interrupts) const
    {
        if ((previous & ~read() & 0x0F) != 0)
            interrupts.request(4);
    }

    void Joypad::write(std::uint8_t value, Interrupts &interrupts)
    {
        const auto previous = read();
        selected_ = value & 0x30;
        detectEdge(previous, interrupts);
    }

    void Joypad::setButtons(std::uint8_t buttons, Interrupts &interrupts)
    {
        const auto previous = read();
        buttons_ = buttons;
        detectEdge(previous, interrupts);
    }
}