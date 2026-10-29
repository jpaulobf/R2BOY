#include "VRAMDMA.h"
#include "Core/BUS/BUS.h"

namespace R2BOY::Core::IO
{
    void VRAMDMA::reset(bool enabled)
    {
        *this = VRAMDMA{};
        enabled_ = enabled;
    }

    std::uint8_t VRAMDMA::read(std::uint16_t address) const
    {
        // HDMA1-4 are write-only; FF55 reports blocks left minus one.
        if (enabled_ && address == 0xFF55 && remaining_)
            return static_cast<std::uint8_t>((active_ ? 0 : 0x80) | ((remaining_ - 1) / 16));
        return 0xFF;
    }

    void VRAMDMA::write(std::uint16_t address, std::uint8_t value, bool doubleSpeed, unsigned bank, bool hblank)
    {
        if (!enabled_ || busy_)
            return;
        if (active_)
        {
            if (address == 0xFF55 && !(value & 0x80))
            {
                active_ = false; // Preserve remaining length and addresses for a later restart.
                requestDelay_ = 0;
            }
            return;
        }
        switch (address)
        {
        case 0xFF51:
            source_ = static_cast<std::uint16_t>((source_ & 0xF0) | (value << 8));
            break;
        case 0xFF52:
            source_ = static_cast<std::uint16_t>((source_ & 0xFF00) | (value & 0xF0));
            break;
        case 0xFF53:
            destination_ = static_cast<std::uint16_t>(0x8000 | ((value & 31) << 8) | (destination_ & 0xF0));
            break;
        case 0xFF54:
            destination_ = static_cast<std::uint16_t>(0x8000 | (destination_ & 0x1F00) | (value & 0xF0));
            break;
        case 0xFF55:
            remaining_ = ((value & 0x7F) + 1) * 16;
            source_ &= 0xFFF0;
            destination_ = static_cast<std::uint16_t>(0x8000 | (destination_ & 0x1FF0));
            hblankMode_ = (value & 0x80) != 0;
            inHblank_ = hblank;
            active_ = true;
            if (!hblankMode_ || hblank)
                beginBlock(doubleSpeed, bank);
            break;
        }
    }

    void VRAMDMA::beginBlock(bool doubleSpeed, unsigned bank)
    {
        busy_ = true;
        blockLeft_ = hblankMode_ ? 16 : remaining_;
        startup_ = 2;
        phase_ = 0;
        tail_ = doubleSpeed ? 0 : 2;
        bank_ = static_cast<std::uint8_t>(bank & 1);
    }

    void VRAMDMA::observeDisplay(bool hblank, bool runnable, bool doubleSpeed, unsigned bank)
    {
        if (requestDelay_ && --requestDelay_ == 0 && runnable)
            beginBlock(doubleSpeed, bank);
        if (active_ && hblankMode_ && !busy_ && hblank && !inHblank_ && runnable)
        {
            // HBlank request follows the mode-0 edge by two base dots.
            requestDelay_ = 2;
        }
        inHblank_ = hblank;
    }

    void VRAMDMA::release()
    {
        busy_ = false;
        if (!remaining_)
            active_ = false;
    }

    void VRAMDMA::tick(BUS::BUS &bus)
    {
        if (!busy_)
            return;
        if (startup_)
        {
            --startup_;
            return;
        }
        if (!blockLeft_)
        {
            if (tail_ == 0 || --tail_ == 0)
                release();
            return;
        }
        if (++phase_ < 2)
            return;
        phase_ = 0;
        bus.writeVRAMDMA(destination_, bus.readVRAMDMA(source_), bank_);
        ++source_;
        ++destination_;
        --remaining_;
        --blockLeft_;
        // Do not wrap past the VRAM destination range.
        if (destination_ == 0xA000)
            remaining_ = blockLeft_ = 0;
        if (!blockLeft_ && !tail_)
            release();
    }
}