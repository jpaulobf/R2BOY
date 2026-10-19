#pragma once

#include <chrono>

namespace R2BOY::System
{
    // Agenda a apresentação a 60 Hz sem somar o custo do frame à espera.
    // O relógio é recebido pelo chamador para permitir testes sem sleeps reais.
    class FramePacer
    {
    public:
        using Clock = std::chrono::steady_clock;
        using Duration = std::chrono::nanoseconds;
        static constexpr Duration FrameDuration{1000000000 / 60};

        explicit FramePacer(Clock::time_point now)
        {
            reset(now);
        }

        void reset(Clock::time_point now)
        {
            nextFrame_ = now + FrameDuration;
        }

        Duration remaining(Clock::time_point now)
        {
            if (now < nextFrame_)
            {
                const auto wait = std::chrono::duration_cast<Duration>(nextFrame_ - now);
                nextFrame_ += FrameDuration;
                return wait;
            }

            // Compensa atrasos pequenos mantendo a cadência. Após perder um
            // intervalo completo, retoma daqui sem acumular frames pendentes.
            if (now - nextFrame_ < FrameDuration)
                nextFrame_ += FrameDuration;
            else
                reset(now);
            return Duration::zero();
        }

    private:
        Clock::time_point nextFrame_;
    };
}
