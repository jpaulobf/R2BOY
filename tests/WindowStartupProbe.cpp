#include "Core/Board.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Accuracy diagnostic, not a passing CTest: compare against the independent
// SameBoy fixture with matching SCX bus strobes, not GB_run() boundaries.
int main(int argc, char **argv)
{
    using namespace R2BOY;
    try
    {
        const bool trace = argc == 4 && std::string(argv[1]) == "--trace";
        if (argc != 1 && !trace)
            throw std::runtime_error("Usage: window_startup_probe [--trace <initial fine 0..7> <new fine 0..7>]");
        const unsigned selectedInitial = trace ? std::stoul(argv[2]) : 0;
        const unsigned selectedNext = trace ? std::stoul(argv[3]) : 0;
        if (selectedInitial > 7 || selectedNext > 7)
            throw std::runtime_error("Fine SCX must be 0..7.");
        std::cout << "initial,next,pixels\n";
        if (trace)
            std::cerr << "dot,scx,warmup,discard,window,window_delay,fifo,fetch_stage,fetch_phase,x\n";
        for (unsigned initial = 0; initial < 8; ++initial)
            for (unsigned next = 0; next < 8; ++next)
            {
                if (trace && (initial != selectedInitial || next != selectedNext))
                    continue;
                Core::Board board;
                std::vector<std::uint8_t> rom(32768);
                board.loadCartridge(rom, "WX=0 CPU startup diagnostic");
                board.setUnlimitedSprites(false);
                board.write(0xFFFF, 0);
                board.write(0xFF40, 0);
                board.write(0xFF47, 0xE4);
                board.write(0xFF43, initial);
                board.write(0xFF4A, 0);
                board.write(0xFF4B, 0);
                for (unsigned row = 0; row < 8; ++row)
                {
                    board.write(0x8010 + row * 2, 0x55);
                    board.write(0x8011 + row * 2, 0x33);
                }
                for (unsigned a = 0x9C00; a < 0xA000; ++a)
                    board.write(a, 1);
                board.write(0xFF80, 0x76);
                auto r = board.cpu().registers();
                r.pc = 0xFF80;
                board.cpu().reset(r);
                board.write(0xFF40, 0xF1);
                while (board.baseClockCycles() < 70224 + 2 * 456 + 72)
                    board.step();
                const auto before = board.timing().ppu;
                if (before.line != 2 || before.dot != 72)
                    throw std::runtime_error("CPU instruction must start at line 2 dot 72.");
                unsigned changes = 0;
                bool badStrobe = false;
                std::uint8_t previousSCX = initial;
                board.setTickObserver([&](const Core::Board::Timing &s)
                                          {
                        const auto &p = s.ppu;
                        if (p.scx != previousSCX)
                        {
                            ++changes;
                            badStrobe |= p.line != 2 || p.dot != 86 || p.scx != next;
                            previousSCX = p.scx;
                        }
                        if (trace && p.line == 2 && p.dot <= 120)
                            std::cerr << p.dot << ',' << +p.scx << ',' << p.warmup << ',' << p.discard
                                      << ',' << p.windowActive << ',' << p.windowDelay << ',' << p.fifoSize
                                      << ',' << p.fetchStage << ',' << p.fetchPhase << ',' << p.x << '\n'; });
                board.write(0xFF80, 0xEA); // LD [FF43],A, then HALT.
                board.write(0xFF81, 0x43);
                board.write(0xFF82, 0xFF);
                board.write(0xFF83, 0x76);
                r = board.cpu().registers();
                r.pc = 0xFF80;
                r.a = next;
                board.cpu().reset(r);
                if (board.step() != 16 || board.timing().ppu.dot != 88)
                    throw std::runtime_error("SCX instruction must complete at dot 88.");
                if (badStrobe || changes != (initial != next ? 1u : 0u) || board.read(0xFF43) != next)
                    throw std::runtime_error("SCX must change once at the calibrated dot-86 strobe.");
                while (board.frameNumber() < 2)
                    board.step();
                std::cout << initial << ',' << next << ',';
                for (unsigned x = 0; x < 24; ++x)
                    std::cout << (255 - (board.framebuffer()[2 * 160 + x] & 255)) / 85;
                std::cout << '\n';
            }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
