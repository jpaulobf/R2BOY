#include "BUS.h"

namespace R2BOY::Core::BUS
{
    BUS::BUS(Cartridge::Cartridge &cartridge, Memory::Memory &memory, IO::Interrupts &interrupts,
             IO::Serial &serial, IO::Timer &timer, IO::DMA &dma, PPU::PPU &ppu, IO::Joypad &joypad,
             IO::SpeedSwitch &speedSwitch, APU::APU &apu, IO::VRAMDMA *vramDMA)
        : cartridge_(cartridge), memory_(memory), interrupts_(interrupts), serial_(serial), timer_(timer), dma_(dma), ppu_(ppu), joypad_(joypad), speedSwitch_(speedSwitch), apu_(apu), vramDMA_(vramDMA) {}

    std::uint8_t BUS::readVRAMDMA(std::uint16_t address) const
    {
        if (address < 0x8000 || (address >= 0xA000 && address < 0xC000))
            return cartridge_.read(address);
        if (address >= 0xC000 && address < 0xE000)
            return memory_.read(address);
        return 0xFF; // Invalid source ranges are not routed through CPU I/O.
    }

    std::uint8_t BUS::read(std::uint16_t address) const
    {
        if (dma_.blocksCPU(address))
            return dma_.readCPUConflict(address);
        if (address >= 0xFE00 && address < 0xFEA0 && ppu_.blocksOAM())
            return 0xFF;
        return readDMA(address);
    }

    std::uint8_t BUS::readDMA(std::uint16_t address) const
    {
        if (address >= 0xFF51 && address <= 0xFF55)
            return vramDMA_ ? vramDMA_->read(address) : 0xFF;
        if ((address >= 0xFF10 && address <= 0xFF26) || (address >= 0xFF30 && address <= 0xFF3F))
            return apu_.read(address);
        if (address == 0xFF4D)
            return speedSwitch_.read();
        if (address == 0xFF00)
            return joypad_.read();
        if (address == 0xFF46)
            return dma_.read();
        if ((address >= 0xFF40 && address <= 0xFF4B) || (address >= 0xFF68 && address <= 0xFF6C))
            return ppu_.read(address);
        if (address >= 0x8000 && address < 0xA000 && ppu_.blocksVRAM())
            return 0xFF;
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
        if (address >= 0xFF51 && address <= 0xFF55)
        {
            if (vramDMA_)
                vramDMA_->write(address, value, speedSwitch_.doubleSpeed(), memory_.read(0xFF4F) & 1, ppu_.vramDMAHBlank());
            return;
        }
        // FF46 aceita reinícios mesmo com o barramento da CPU bloqueado.
        if (address == 0xFF46)
        {
            dma_.start(value);
            return;
        }
        if (dma_.blocksCPU(address))
            return;
        if ((address >= 0xFF10 && address <= 0xFF26) || (address >= 0xFF30 && address <= 0xFF3F))
        {
            apu_.write(address, value);
            return;
        }
        if (address == 0xFF4D)
        {
            speedSwitch_.write(value);
            return;
        }
        if (address == 0xFF00)
        {
            joypad_.write(value, interrupts_);
            return;
        }
        if ((address >= 0x8000 && address < 0xA000 && ppu_.blocksVRAM()) ||
            (address >= 0xFE00 && address < 0xFEA0 && ppu_.blocksOAM()))
            return;
        if ((address >= 0xFF40 && address <= 0xFF4B) || (address >= 0xFF68 && address <= 0xFF6C))
        {
            ppu_.write(address, value, interrupts_);
            return;
        }
        if (address >= 0xFF04 && address <= 0xFF07)
        {
            const auto previousCounter = timer_.systemCounter();
            timer_.write(address, value);
            serial_.clock(previousCounter, timer_.systemCounter(), interrupts_);
            return;
        }
        if (address == 0xFF01)
        {
            serial_.writeSB(value);
            return;
        }
        if (address == 0xFF02)
        {
            serial_.writeSC(value, interrupts_);
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