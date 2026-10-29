#pragma once

#include "Core/StateArchive.h"
#include <cstdint>

namespace R2BOY::Core::BUS
{
    class BUS;
}

namespace R2BOY::Core::IO
{
    // CGB general-purpose and HBlank VRAM DMA, clocked in base dots.
    class VRAMDMA
    {
    public:
        void reset(bool enabled);
        std::uint8_t read(std::uint16_t address) const;
        void write(std::uint16_t address, std::uint8_t value, bool doubleSpeed, unsigned bank, bool hblank);
        void observeDisplay(bool hblank, bool runnable, bool doubleSpeed, unsigned bank);
        // Base-clock dots: 2 dots/byte, independent of KEY1 and CPU overclock.
        void tick(BUS::BUS &bus);
        bool active() const { return active_; }
        bool cpuHeld() const { return busy_; }
        unsigned remaining() const { return remaining_; }

    private:
        friend class R2BOY::Core::StateArchive;
        void state(StateArchive &archive)
        {
            const bool expectedEnabled = enabled_;
            archive(enabled_, active_, source_, destination_, remaining_, startup_, phase_, tail_, bank_,
                    busy_, hblankMode_, inHblank_, blockLeft_, requestDelay_);
            if (archive.reading() && (destination_ < 0x8000 || destination_ > 0xA000 ||
                                      enabled_ != expectedEnabled ||
                                      remaining_ > 0x800 || startup_ > 2 || phase_ > 1 || tail_ > 2 || bank_ > 1 ||
                                      requestDelay_ > 2 || (requestDelay_ && (!active_ || !hblankMode_ || busy_)) ||
                                      blockLeft_ > remaining_ || (!enabled_ && (active_ || busy_)) ||
                                      (!active_ && (busy_ || startup_ || phase_ || tail_)) ||
                                      (!busy_ && (blockLeft_ || startup_ || phase_ || tail_)) ||
                                      (active_ && !remaining_ && !tail_) || (busy_ && !active_) ||
                                      (busy_ && !blockLeft_ && !tail_) || (startup_ && !blockLeft_) ||
                                      (hblankMode_ && blockLeft_ > 16) ||
                                      (active_ && !hblankMode_ && !busy_) ||
                                      (active_ && remaining_ && ((source_ ^ destination_) & 15))))
                throw std::runtime_error("Invalid CGB VRAM DMA state.");
        }
        bool enabled_ = false, active_ = false;
        bool busy_ = false, hblankMode_ = false, inHblank_ = false;
        void beginBlock(bool doubleSpeed, unsigned bank);
        void release();
        std::uint16_t source_ = 0, destination_ = 0x8000;
        unsigned remaining_ = 0, startup_ = 0, phase_ = 0, tail_ = 0;
        unsigned blockLeft_ = 0, requestDelay_ = 0;
        std::uint8_t bank_ = 0;
    };
}