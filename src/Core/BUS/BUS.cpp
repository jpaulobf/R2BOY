#include "BUS.h"

namespace R2BOY::Core::BUS
{
    BUS::BUS(Cartridge::Cartridge &cartridge, Memory::Memory &memory, IO::Interrupts &interrupts,
             IO::Serial &serial, IO::Timer &timer)
        : cartridge_(cartridge), memory_(memory), interrupts_(interrupts), serial_(serial), timer_(timer) {}

    std::uint8_t BUS::read(std::uint16_t address) const
    {
        if (address >= 0xFF04 && address <= 0xFF07)
            return timer_.read(address);
        if (address == 0xFF01)
            return serial_.readSB();
        if (address == 0xFF02)
            return serial_.readSC();
        if (address == 0xFF0F)
            return interrupts_.readIF();
        if (address == 0xFFFF)
            return interrupts_.readIE();
        if (address < 0x8000 || (address >= 0xA000 && address < 0xC000))
            return cartridge_.read(address);
        return memory_.read(address);
    }

    void BUS::write(std::uint16_t address, std::uint8_t value)
    {
        if (address >= 0xFF04 && address <= 0xFF07)
        {
            timer_.write(address, value);
            return;
        }
        if (address == 0xFF01)
        {
            serial_.writeSB(value);
            return;
        }
        if (address == 0xFF02)
        {
            serial_.writeSC(value);
            return;
        }
        if (address == 0xFF0F)
        {
            interrupts_.writeIF(value);
            return;
        }
        if (address == 0xFFFF)
        {
            interrupts_.writeIE(value);
            return;
        }
        if (address < 0x8000 || (address >= 0xA000 && address < 0xC000))
            cartridge_.write(address, value);
        else
            memory_.write(address, value);
    }
}