#include "Serial.h"

#include <utility>

namespace R2BOY::Core::IO
{
    void Serial::reset(bool cgbMode)
    {
        data_ = transmitted_ = 0;
        cgbMode_ = cgbMode;
        control_ = cgbMode ? 3 : 0;
        bits_ = elapsed_ = 0;
        output_.clear();
    }

    void Serial::writeSC(std::uint8_t value)
    {
        control_ = value & (cgbMode_ ? 0x83 : 0x81);
        bits_ = elapsed_ = 0;
        transmitted_ = 0;
    }

    void Serial::tick(unsigned tCycles, Interrupts &interrupts)
    {
        if ((control_ & 0x81) != 0x81)
            return;
            
        elapsed_ += tCycles;
        const unsigned period = cgbMode_ && (control_ & 2) != 0 ? 16 : 512;

        while (elapsed_ >= period && (control_ & 0x80) != 0)
        {
            elapsed_ -= period;
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