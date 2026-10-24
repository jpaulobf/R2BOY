#include "APU.h"

namespace R2BOY::Core::APU
{
    void APU::reset()
    {
        // Perfil sem boot ROM; os canais ainda não são executados.
        powered_ = true;
        volume_ = 0x77;
        panning_ = 0xF3;
    }

    std::uint8_t APU::read(std::uint16_t address) const
    {
        switch (address)
        {
        case 0xFF24: return volume_;
        case 0xFF25: return panning_;
        case 0xFF26: return 0x70 | (powered_ ? 0x80 : 0);
        default: return 0xFF;
        }
    }

    void APU::write(std::uint16_t address, std::uint8_t value)
    {
        if (address == 0xFF26)
        {
            powered_ = (value & 0x80) != 0;
            if (!powered_)
                volume_ = panning_ = 0;
            return;
        }
        if (!powered_)
            return;
        if (address == 0xFF24)
            volume_ = value;
        else if (address == 0xFF25)
            panning_ = value;
    }
}
