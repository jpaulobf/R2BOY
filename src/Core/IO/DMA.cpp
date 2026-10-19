#include "DMA.h"
#include "Core/BUS/BUS.h"

namespace R2BOY::Core::IO
{
    void DMA::reset(bool colorHardware)
    {
        colorHardware_ = colorHardware;
        register_ = colorHardware ? 0x00 : 0xFF;
        sourcePage_ = 0;
        index_ = startup_ = phase_ = 0;
        active_ = false;
    }

    void DMA::start(std::uint8_t page)
    {
        register_ = page;
        // A CPU efetua a escrita ao final de M0. M1 permanece livre; a
        // primeira cópia/bloqueio é visível no acesso de M2 (Mooneye oam_dma_start).
        startup_ = 8;
    }

    void DMA::tick(BUS::BUS &bus)
    {
        if (startup_ != 0 && --startup_ == 0)
        {
            sourcePage_ = register_;
            index_ = 0;
            active_ = true;
            phase_ = 1;
        }
        if (!active_ || --phase_ != 0)
            return;
        if (index_ == 160)
        {
            // Mantém OAM bloqueada durante o M-cycle do último byte.
            active_ = false;
            return;
        }

        auto source = static_cast<std::uint16_t>((sourcePage_ << 8) | index_);
        std::uint8_t value = 0xFF;
        if (source < 0xE000 || !colorHardware_)
        {
            // No DMG, E000–FFFF vem de C000–DFFF, inclusive FE/FF.
            // No CGB, fontes E000–FFFF produzem FF.
            if (source >= 0xE000)
                source &= 0xDFFF;
            value = bus.readDMA(source);
        }
        bus.writeOAM(static_cast<std::uint16_t>(0xFE00 + index_), value);
        ++index_;
        phase_ = 4;
    }

    bool DMA::blocksCPU(std::uint16_t address) const
    {
        if (!active_ || (address >= 0xFF80 && address <= 0xFFFE))
            return false;
        if (!colorHardware_)
            return true;
        if (address >= 0xFE00)
            return address < 0xFF00; // OAM indisponível; I/O e IE continuam acessíveis.

        // CGB: barramentos separados para cartucho, VRAM e WRAM.
        const auto busFor = [](std::uint16_t a)
        {
            if (a >= 0x8000 && a < 0xA000)
                return 1;
            if (a >= 0xC000)
                return 2;
            return 0;
        };
        return busFor(address) == busFor(static_cast<std::uint16_t>(sourcePage_ << 8));
    }
}