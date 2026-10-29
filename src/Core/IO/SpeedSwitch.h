#pragma once

#include "Core/StateArchive.h"
#include <cstdint>

namespace R2BOY::Core::IO
{
    // KEY1 pertence ao modo CGB, não ao modo de compatibilidade DMG.
    class SpeedSwitch
    {
    public:
        static constexpr unsigned SwitchDelay = 2050 * 4;

        void reset(bool cgbMode)
        {
            cgbMode_ = cgbMode;
            doubleSpeed_ = armed_ = false;
            remaining_ = 0;
        }

        std::uint8_t read() const
        {
            return cgbMode_ ? static_cast<std::uint8_t>(0x7E | (doubleSpeed_ ? 0x80 : 0) | armed_) : 0xFF;
        }

        void write(std::uint8_t value)
        {
            if (cgbMode_)
                armed_ = (value & 1) != 0;
        }

        // STOP consome a preparação e bloqueia instruções durante a estabilização.
        bool beginSwitch()
        {
            if (!cgbMode_ || !armed_)
                return false;
            armed_ = false;
            doubleSpeed_ = !doubleSpeed_;
            remaining_ = SwitchDelay;
            return true;
        }

        bool doubleSpeed() const { return doubleSpeed_; }
        bool switching() const { return remaining_ != 0; }
        void tick()
        {
            if (remaining_ != 0)
                --remaining_;
        }

    private:
        friend class R2BOY::Core::StateArchive;
        void state(StateArchive &archive)
        {
            const bool expectedMode = cgbMode_;
            archive(cgbMode_, doubleSpeed_, armed_, remaining_);
            if (archive.reading() && (cgbMode_ != expectedMode || remaining_ > SwitchDelay || remaining_ % 4 != 0 ||
                                      (!cgbMode_ && (doubleSpeed_ || armed_ || remaining_ != 0))))
                throw std::runtime_error("Invalid CGB speed-switch state.");
        }

        bool cgbMode_ = false;
        bool doubleSpeed_ = false;
        bool armed_ = false;
        unsigned remaining_ = 0;
    };
}