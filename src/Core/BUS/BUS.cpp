#include "BUS.h"

namespace R2BOY::Core::BUS
{
    BUS::BUS(Cartridge::Cartridge &cartridge, Memory::Memory &memory)
        : cartridge_(cartridge), memory_(memory) {}

    std::uint8_t BUS::read(std::uint16_t address) const
    {
        if (address < 0x8000 || (address >= 0xA000 && address < 0xC000))
            return cartridge_.read(address);
        return memory_.read(address);
    }

    void BUS::write(std::uint16_t address, std::uint8_t value)
    {
        if (address < 0x8000 || (address >= 0xA000 && address < 0xC000))
            cartridge_.write(address, value);
        else
            memory_.write(address, value);
    }
}
