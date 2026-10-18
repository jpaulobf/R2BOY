#include "Core/Board.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace R2BOY::Test
{
    namespace
    {
        void check(bool condition, const char *message)
        {
            if (!condition)
                throw std::runtime_error(message);
        }

        struct Fixture
        {
            Core::IO::Timer timer;
            Core::IO::Interrupts interrupts;
            void reset(bool color = false, std::uint16_t divider = 0)
            {
                timer.reset(color, divider);
                interrupts.reset();
            }
            void advance(unsigned count)
            {
                while (count-- != 0)
                    timer.tick(interrupts);
            }
            void overflow()
            {
                reset();
                timer.write(0xFF06, 0x42);
                timer.write(0xFF05, 0xFF);
                timer.write(0xFF07, 5);
                advance(16);
                check(timer.read(0xFF05) == 0 && (interrupts.readIF() & 4) == 0, "overflow must expose zero before reload");
            }
        };

        void frequenciesAndDivider()
        {
            Fixture f;
            constexpr std::array<unsigned, 4> periods = {1024, 16, 64, 256};
            for (unsigned source = 0; source < 4; ++source)
            {
                f.reset();
                f.timer.write(0xFF07, static_cast<std::uint8_t>(4 | source));
                f.advance(periods[source] - 1);
                check(f.timer.read(0xFF05) == 0, "timer must not tick early");
                f.advance(1);
                check(f.timer.read(0xFF05) == 1, "timer selected frequency");
                f.advance(periods[source] * 3);
                check(f.timer.read(0xFF05) == 4, "timer continuous counting");
            }
            f.reset();
            f.advance(255);
            check(f.timer.read(0xFF04) == 0 && f.timer.read(0xFF05) == 0, "DIV low-byte boundary");
            f.advance(1);
            check(f.timer.read(0xFF04) == 1 && f.timer.read(0xFF05) == 0, "DIV ticks with TIMA disabled");
            f.timer.write(0xFF05, 0x33);
            f.timer.write(0xFF04, 0xFF);
            check(f.timer.read(0xFF04) == 0 && f.timer.read(0xFF05) == 0x33, "DIV write resets counter, not TIMA");
            f.advance(255);
            check(f.timer.read(0xFF04) == 0, "DIV write clears hidden low bits too");
            f.advance(1);
            check(f.timer.read(0xFF04) == 1, "DIV restarts from zero");
            f.reset(false, 0xFFFF);
            f.advance(1);
            check(f.timer.read(0xFF04) == 0, "16-bit divider wraps");
            f.timer.write(0xFF07, 0xFF);
            check(f.timer.read(0xFF07) == 0xFF, "TAC readable mask");
            f.timer.write(0xFF07, 0);
            check(f.timer.read(0xFF07) == 0xF8 && f.timer.read(0xFFFF) == 0xFF, "TAC unused bits and invalid address");
        }

        void fallingEdges()
        {
            Fixture f;
            f.reset();
            f.timer.write(0xFF07, 5);
            f.advance(8);
            f.timer.write(0xFF04, 0);
            check(f.timer.read(0xFF05) == 1, "DIV high-to-low selected bit increments TIMA");
            f.timer.write(0xFF04, 0);
            check(f.timer.read(0xFF05) == 1, "DIV low-to-low must not increment");
            for (bool color : {false, true})
            {
                f.reset(color);
                f.timer.write(0xFF07, 5);
                f.advance(8);
                f.timer.write(0xFF07, 6); // bit 3 alto -> bit 5 baixo.
                check(f.timer.read(0xFF05) == 1, "TAC clock-source falling edge");
                f.timer.write(0xFF07, 5); // transição para bit alto não incrementa.
                check(f.timer.read(0xFF05) == 1, "TAC rising edge must not increment");
                f.timer.write(0xFF07, 1); // Desabilita com bit alto.
                check(f.timer.read(0xFF05) == (color ? 1 : 2), "TAC disable differs by physical model");
                f.timer.write(0xFF07, 5);
                check(f.timer.read(0xFF05) == (color ? 1 : 2), "deterministic enabling without CGB race pulse");
            }
            f.reset();
            f.timer.write(0xFF07, 5);
            f.timer.write(0xFF05, 0xFF);
            f.advance(8);
            f.timer.write(0xFF04, 0); // Borda artificial também agenda overflow, sem IRQ imediata.
            check(f.timer.read(0xFF05) == 0 && (f.interrupts.readIF() & 4) == 0, "DIV-triggered overflow delay");
            f.advance(4);
            check((f.interrupts.readIF() & 4) != 0, "DIV-triggered overflow requests interrupt after delay");
        }

        void reloadConflicts()
        {
            Fixture f;
            f.overflow();
            f.advance(3);
            check(f.timer.read(0xFF05) == 0 && (f.interrupts.readIF() & 4) == 0, "reload delay is four T-cycles");
            f.advance(1);
            check(f.timer.read(0xFF05) == 0x42 && (f.interrupts.readIF() & 4) != 0, "reload and timer IRQ occur together");
            f.interrupts.writeIF(0);
            f.advance(4);
            check((f.interrupts.readIF() & 4) == 0, "reload does not request interrupt repeatedly");
            f.timer.write(0xFF05, 0xFF);
            check((f.interrupts.readIF() & 4) == 0, "writing FF to TIMA is not an overflow");

            for (unsigned delay = 0; delay < 4; ++delay)
            {
                f.overflow();
                f.advance(delay);
                f.timer.write(0xFF05, 0x77);
                f.advance(4 - delay);
                check(f.timer.read(0xFF05) == 0x77 && (f.interrupts.readIF() & 4) == 0, "TIMA write cancels pending reload/IRQ");
            }
            f.overflow();
            f.timer.write(0xFF06, 0x99);
            f.advance(4);
            check(f.timer.read(0xFF05) == 0x99, "pending reload uses latest TMA");
            for (unsigned offset = 0; offset < 4; ++offset)
            {
                f.overflow();
                f.advance(4 + offset);
                f.timer.write(0xFF05, 0x88);
                check(f.timer.read(0xFF05) == 0x42, "TIMA write ignored during reload M-cycle");
                f.timer.write(0xFF06, 0x66);
                check(f.timer.read(0xFF05) == 0x66 && f.timer.read(0xFF06) == 0x66, "TMA write updates TIMA during reload M-cycle");
            }
            f.overflow();
            f.advance(8);
            f.timer.write(0xFF05, 0x55);
            check(f.timer.read(0xFF05) == 0x55, "TIMA writable again after reload window");
            f.overflow();
            f.timer.write(0xFF04, 0);
            f.timer.write(0xFF07, 0);
            f.advance(4);
            check(f.timer.read(0xFF05) == 0x42 && (f.interrupts.readIF() & 4) != 0, "DIV/TAC writes do not cancel pending reload");
        }

        void boardIntegration()
        {
            std::vector<std::uint8_t> image(32768);
            image[0x100] = 0xFB;
            image[0x101] = 0;
            image[0x102] = 0x76; // EI, NOP, HALT.
            image[0x50] = 0xD9;  // RETI no vetor do timer.
            Core::Board board;
            board.loadCartridge(image, "timer IRQ");
            check(board.read(0xFF04) == 0xAB && board.read(0xFF05) == 0 &&
                      board.read(0xFF06) == 0 && board.read(0xFF07) == 0xF8,
                  "timer post-boot profile");
            board.write(0xFF04, 0);
            board.write(0xFF05, 0xFF);
            board.write(0xFF06, 0xA5);
            board.write(0xFF07, 5);
            board.write(0xFF0F, 0);
            board.write(0xFFFF, 4);
            board.step();
            board.step();
            board.step();
            check(board.cpu().halted(), "CPU waits in HALT for timer");
            board.step();
            check(board.clockCycles() == 16 && board.read(0xFF05) == 0 && (board.read(0xFF0F) & 4) == 0, "overflow during HALT");
            board.step();
            check(board.clockCycles() == 20 && (board.read(0xFF0F) & 4) != 0, "reload requests IRQ while CPU halted");
            check(board.step() == 20 && board.cpu().registers().pc == 0x50 && !board.cpu().halted(), "timer wakes CPU and selects vector 50");
            check((board.read(0xFF0F) & 4) == 0 && board.read(0xFFFC) == 3 && board.read(0xFFFD) == 1, "timer IRQ acknowledgement/return address");
            check(board.step() == 16 && board.cpu().registers().pc == 0x103 && board.cpu().ime(), "RETI returns from timer IRQ");

            image.assign(32768, 0);
            image[0x101] = 0xF0;
            image[0x102] = 0x05;
            image[0x103] = 0xF0;
            image[0x104] = 0x05;
            board.loadCartridge(image, "timer bus sampling");
            board.write(0xFF04, 0);
            board.write(0xFF05, 0xFF);
            board.write(0xFF06, 0x42);
            board.write(0xFF07, 5);
            board.step();
            board.step();
            check(board.clockCycles() == 16 && board.cpu().registers().a == 0, "CPU samples TIMA zero at overflow boundary");
            board.step();
            check(board.clockCycles() == 28 && board.cpu().registers().a == 0x42, "CPU samples reloaded TIMA during later instruction");

            image.assign(32768, 0);
            image[0x100] = 0x10;
            board.loadCartridge(image, "STOP divider");
            check(board.read(0xFF04) != 0, "STOP test starts with nonzero DIV");
            board.step();
            const auto stoppedClock = board.clockCycles();
            check(board.cpu().stopped() && board.read(0xFF04) == 0, "STOP resets DIV");
            for (unsigned i = 0; i < 100; ++i)
                board.step();
            check(board.clockCycles() == stoppedClock && board.read(0xFF04) == 0, "timer frozen during STOP API waits");

            // CGB executando jogo DMG preserva o comportamento físico CGB de TAC.
            image.assign(32768, 0);
            Core::Board color(Core::Model::CGB);
            color.loadCartridge(image, "CGB DMG compatibility");
            color.write(0xFF04, 0);
            color.write(0xFF07, 5);
            color.step();
            color.step();
            color.write(0xFF07, 0);
            check(color.executionMode() == Core::ExecutionMode::DMG && color.read(0xFF05) == 0, "CGB TAC behavior depends on hardware, not execution mode");
        }
    }

    int runTimerTests()
    {
        try
        {
            frequenciesAndDivider();
            fallingEdges();
            reloadConflicts();
            boardIntegration();
            std::cout << "Timer frequencies, edge effects, reload and integration tests passed.\n";
            return 0;
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
}

int main() { return R2BOY::Test::runTimerTests(); }
