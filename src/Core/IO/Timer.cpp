#include "Timer.h"

namespace R2BOY::Core::IO
{
    void Timer::reset(bool cgbHardware, std::uint16_t divider)
    {
        divider_ = divider;
        tima_ = tma_ = tac_ = 0;
        reloadDelay_ = reloadWindow_ = 0;
        cgbHardware_ = cgbHardware;
    }

    bool Timer::signal() const
    {
        constexpr unsigned bits[] = {9, 3, 5, 7};
        return (tac_ & 4) != 0 && (divider_ & (1 << bits[tac_ & 3])) != 0;
    }

    void Timer::increment()
    {
        // O circuito de carga mantém TIMA estável durante as janelas de overflow/reload.
        if (reloadDelay_ != 0 || reloadWindow_ != 0)
            return;

        if (++tima_ == 0)
            reloadDelay_ = 4;
    }

    void Timer::tick(Interrupts &interrupts)
    {
        if (reloadWindow_ != 0)
            --reloadWindow_;

        if (reloadDelay_ != 0 && --reloadDelay_ == 0)
        {
            tima_ = tma_;
            interrupts.request(2);
            reloadWindow_ = 4;
        }

        const bool previous = signal();
        ++divider_;
        if (previous && !signal())
            increment();
    }

    std::uint8_t Timer::read(std::uint16_t address) const
    {
        switch (address)
        {
        case 0xFF04:
            return static_cast<std::uint8_t>(divider_ >> 8);
        case 0xFF05:
            return tima_;
        case 0xFF06:
            return tma_;
        case 0xFF07:
            return tac_ | 0xF8;
        default:
            return 0xFF;
        }
    }

    void Timer::write(std::uint16_t address, std::uint8_t value)
    {
        switch (address)
        {
        case 0xFF04:
        {
            const bool previous = signal();
            divider_ = 0;
            if (previous)
                increment();
            break;
        }
        case 0xFF05:
            if (reloadWindow_ != 0)
                return;
            tima_ = value;
            reloadDelay_ = 0;
            break;
        case 0xFF06:
            tma_ = value;
            if (reloadWindow_ != 0)
                tima_ = value;
            break;
        case 0xFF07:
        {
            const bool previous = signal();
            tac_ = value & 7;

            // DMG gera uma borda ao desabilitar; no CGB essa transição não incrementa.
            // A corrida variável entre revisões CGB ao habilitar usa a escolha
            // determinística sem pulso extra neste modelo.
            if (previous && !signal() && (!cgbHardware_ || (tac_ & 4) != 0))
                increment();
            break;
        }
        }
    }
}