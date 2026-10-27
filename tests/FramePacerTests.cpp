#include "System/FramePacer.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace
{
    using Pacer = R2BOY::System::FramePacer;
    using namespace std::chrono_literals;

    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
}

int main()
{
    try
    {
        const Pacer::Clock::time_point start{};
        constexpr auto period = Pacer::FrameDuration;
        check(period == 16742706ns, "native frame period must match 70224 dots at 4194304 Hz within one nanosecond");

        Pacer pacer(start);
        check(pacer.remaining(start + 5ms) == period - 5ms,
              "input, update and render time must be subtracted from the wait");
        check(pacer.remaining(start + period + 8ms) == period - 8ms,
              "different processing costs must preserve the presentation cadence");

        pacer.reset(start);
        check(pacer.remaining(start + period + 1ms) == 0ns,
              "late frames must not sleep");
        check(pacer.remaining(start + period + 4ms) == period - 4ms,
              "small overruns must preserve the next deadline");

        pacer.reset(start);
        check(pacer.remaining(start + period) == 0ns,
              "a frame ending at its deadline must not sleep");
        check(pacer.remaining(start + period + 3ms) == period - 3ms,
              "the deadline must advance even when no sleep was needed");

        pacer.reset(start);
        check(pacer.remaining(start + 1s) == 0ns, "long stalls must not sleep");
        check(pacer.remaining(start + 1s + 2ms) == period - 2ms,
              "long stalls must discard missed deadlines instead of causing a burst");

        pacer.reset(start + 10s);
        check(pacer.remaining(start + 10s + 6ms) == period - 6ms,
              "resume and ROM loading must start a fresh cadence");

        // Mesmo com oversleep recorrente, os prazos continuam na grade nativa.
        pacer.reset(start);
        for (int frame = 0; frame < 600; ++frame)
        {
            check(pacer.remaining(start + frame * period + 6ms) == period - 6ms,
                  "processing and wake-up delay must not accumulate across frames");
        }
        std::cout << "Frame pacing tests passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
