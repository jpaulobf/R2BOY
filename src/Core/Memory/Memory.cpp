#include "Memory.h"

namespace R2BOY::Core::Memory
{
    void Memory::reset(bool cgbMode)
    {
        vram_.fill(0);
        wram_.fill(0);
        oam_.fill(0);
        hram_.fill(0);
        cgbMode_ = cgbMode;
        vramBank_ = 0;
        wramBank_ = 1;
    }

    std::uint8_t Memory::read(std::uint16_t address) const
    {
        if (address >= 0x8000 && address < 0xA000)
            return vram_[vramBank_ * 0x2000 + address - 0x8000];
        if (address >= 0xC000 && address < 0xD000)
            return wram_[address - 0xC000];
        if (address >= 0xD000 && address < 0xE000)
            return wram_[(wramBank_ == 0 ? 1 : wramBank_) * 0x1000 + address - 0xD000];
        // E000–FDFF espelha C000–DDFF; não inclui os últimos 512 bytes de WRAM.
        if (address >= 0xE000 && address < 0xFE00)
            return read(address - 0x2000);
        if (address >= 0xFE00 && address < 0xFEA0)
            return oam_[address - 0xFE00];
        if (address >= 0xFF80 && address < 0xFFFF)
            return hram_[address - 0xFF80];
        if (cgbMode_ && address == 0xFF4F)
            return 0xFE | vramBank_;
        if (cgbMode_ && address == 0xFF70)
            return 0xF8 | wramBank_;
        // Faixa FEA0–FEFF e periféricos ainda ausentes usam 0xFF como valor de barramento.
        return 0xFF;
    }

    void Memory::write(std::uint16_t address, std::uint8_t value)
    {
        if (address >= 0x8000 && address < 0xA000)
            vram_[vramBank_ * 0x2000 + address - 0x8000] = value;
        else if (address >= 0xC000 && address < 0xD000)
            wram_[address - 0xC000] = value;
        else if (address >= 0xD000 && address < 0xE000)
            wram_[(wramBank_ == 0 ? 1 : wramBank_) * 0x1000 + address - 0xD000] = value;
        else if (address >= 0xE000 && address < 0xFE00)
            write(address - 0x2000, value);
        else if (address >= 0xFE00 && address < 0xFEA0)
            oam_[address - 0xFE00] = value;
        else if (address >= 0xFF80 && address < 0xFFFF)
            hram_[address - 0xFF80] = value;
        else if (cgbMode_ && address == 0xFF4F)
            vramBank_ = value & 1;
        else if (cgbMode_ && address == 0xFF70)
        {
            // O registrador preserva zero, mas o banco efetivo zero é sempre o banco 1.
            wramBank_ = value & 7;
        }
    }
}