#include "Serial.h"

#include <utility>

namespace R2BOY::Core::IO
{
    void Serial::reset(bool cgbMode)
    {
        data_ = transmitted_ = 0;
        cgbMode_ = cgbMode;
        control_ = cgbMode ? 3 : 0;
        bits_ = 0;
        masterClock_ = false;
        output_.clear();
    }

    void Serial::writeSC(std::uint8_t value, Interrupts &interrupts)
    {
        // SC força o sinal de clock a baixo antes de aplicar o novo controle.
        // Se já havia transferência ativa, essa borda ainda pertence a ela.
        bits_ = 0;
        if (masterClock_)
            edge(interrupts);
        control_ = value & (cgbMode_ ? 0x83 : 0x81);
        transmitted_ = 0;
        bits_ = 0;
    }

    void Serial::clock(std::uint16_t previous, std::uint16_t current, Interrupts &interrupts)
    {
        const unsigned mask = cgbMode_ && (control_ & 2) != 0 ? 4 : 128;
        if ((previous & ~current & mask) != 0)
            edge(interrupts);
    }

    void Serial::edge(Interrupts &interrupts)
    {
        masterClock_ = !masterClock_;
        if (!masterClock_ && (control_ & 0x81) == 0x81)
        {
            transmitted_ = static_cast<std::uint8_t>((transmitted_ << 1) | (data_ >> 7));
            data_ = static_cast<std::uint8_t>((data_ << 1) | 1);
            if (++bits_ == 8)
            {
                control_ &= 0x7F;
                interrupts.request(3);
                output_ += static_cast<char>(transmitted_);
            }
        }
    }

    std::string Serial::takeOutput()
    {
        return std::exchange(output_, {});
    }
}