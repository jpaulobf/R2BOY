#include "Core/Board.h"
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace R2BOY;
namespace
{
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct Fixture
    {
        Core::Board b;
        Fixture(Core::Model model = Core::Model::CGB, bool color = true) : b(model)
        {
            std::vector<std::uint8_t> rom(4 * 16384);
            rom[0x143] = color ? 0x80 : 0;
            rom[0x147] = 0x1B; // MBC5 + RAM + battery.
            rom[0x148] = 1;
            rom[0x149] = 3;
            rom[0x4000] = 0x31;
            rom[0x8000] = 0x52;
            b.loadCartridge(rom, "VRAM DMA controlled scene");
            b.write(0xFF40, 0);
            b.write(0xFFFF, 0);
            program({0x00, 0x00, 0x00});
            for (unsigned i = 0; i < 0x800; ++i)
                b.write(0xC000 + i, static_cast<std::uint8_t>(i * 13 + 7));
        }
        void program(std::initializer_list<std::uint8_t> bytes)
        {
            unsigned address = 0xFF80;
            for (auto byte : bytes)
                b.write(address++, byte);
            auto r = b.cpu().registers();
            r.pc = 0xFF80;
            b.cpu().reset(r);
        }
        void speed()
        {
            program({0x10, 0, 0});
            b.write(0xFF4D, 1);
            b.step();
            while (b.switchingSpeed())
                b.step();
            check(b.doubleSpeed(), "test must enter double speed");
            program({0, 0, 0});
        }
        void start(unsigned source, unsigned dest, unsigned blocks)
        {
            b.write(0xFF51, source >> 8);
            b.write(0xFF52, source);
            b.write(0xFF53, dest >> 8);
            b.write(0xFF54, dest);
            b.write(0xFF55, blocks - 1);
        }
        unsigned finish()
        {
            unsigned cycles = 0;
            while (b.timing().vramDMAActive)
            {
                cycles += b.step();
                check(cycles <= 16400, "GDMA must complete in bounded time");
            }
            return cycles;
        }
    };

    void transferAndReplay()
    {
        for (const bool fast : {false, true})
            for (const bool overclock : {false, true})
                for (const unsigned blocks : {1u, 2u, 128u})
                {
                    Fixture f;
                    if (fast)
                        f.speed();
                    f.b.setCPUOverclock(overclock);
                    f.b.write(0xFF4F, 1);
                    const auto pc = f.b.cpu().registers().pc;
                    const auto beforeCPU = f.b.clockCycles();
                    const auto beforeBase = f.b.baseClockCycles();
                    const auto beforeDIV = f.b.read(0xFF04);
                    unsigned accesses = 0;
                    f.b.cpu().setCycleObserver([&](const auto &c)
                                               { if (c.type != Core::CPU::CPU::CycleType::Internal) ++accesses; });
                    f.start(0xC00F, 0xE00F, blocks); // Low nibbles and destination upper bits ignored.
                    for (unsigned a = 0xFF51; a < 0xFF55; ++a)
                        check(f.b.read(a) == 0xFF, "HDMA1-4 must be write-only");
                    check(f.b.read(0x8000) == 0 && f.b.read(0xFF55) == blocks - 1,
                          "starting GDMA must not perform an immediate copy");
                    check(f.b.step() == 4 && f.b.cpu().registers().pc == pc,
                          "GDMA wait must consume a CPU M-cycle without fetching instructions");
                    f.b.step();
                    check(f.b.read(0x8000) == 7 && f.b.read(0x8000 + blocks * 16 - 1) == 0,
                          "checkpoint must contain a partial copy in both speed modes");
                    const auto snapshot = f.b.saveState();
                    const auto finish = [&]()
                    {
                        const unsigned remaining = f.finish();
                        const unsigned total = static_cast<unsigned>(f.b.clockCycles() - beforeCPU);
                        check(total == 4 + blocks * (fast ? 64 : 32),
                              "GDMA must use 32 dots/block plus the speed-dependent startup/completion profile");
                        check(f.b.baseClockCycles() - beforeBase == blocks * 32 + (fast ? 2 : 4),
                              "GDMA transfer rate must ignore KEY1 and optional CPU overclock");
                        check(f.b.cpu().registers().pc == pc && accesses == 0,
                              "DMA waits must preserve PC and issue no CPU bus accesses");
                        for (unsigned i = 0; i < blocks * 16; ++i)
                            check(f.b.read(0x8000 + i) == static_cast<std::uint8_t>(i * 13 + 7),
                                  "GDMA must copy all requested bytes into the selected VRAM bank");
                        if (blocks == 128)
                            check(f.b.read(0xFF04) != beforeDIV, "DIV must continue while GDMA suspends CPU execution");
                        const auto future = f.b.saveState();
                        return std::make_pair(remaining, future);
                    };
                    const auto future = finish();
                    f.b.loadState(snapshot);
                    check(f.b.saveState() == snapshot, "mid-GDMA state must round-trip exactly");
                    check(finish() == future, "restoring a partial transfer must reproduce every byte, wait and future state");
                    f.b.cpu().setCycleObserver({});
                    f.b.write(0xFF4F, 0);
                    check(f.b.read(0x8000) == 0, "VRAM bank not selected by GDMA must be preserved");
                    f.b.step();
                    check(f.b.cpu().registers().pc == pc + 1, "CPU must resume only after transfer completion");
                }
    }

    void banksAndSources()
    {
        Fixture f;
        f.b.write(0x2000, 2);
        f.start(0x4000, 0x8000, 1);
        f.finish();
        check(f.b.read(0x8000) == 0x52, "GDMA source must use the selected ROM bank");
        f.b.write(0, 0x0A);
        f.b.write(0x4000, 2);
        f.b.write(0xA000, 0x67);
        f.start(0xA000, 0x8020, 1);
        f.finish();
        check(f.b.read(0x8020) == 0x67, "GDMA must read enabled external RAM through its mapper");
        f.b.write(0xFF70, 3);
        f.b.write(0xD000, 0x89);
        f.start(0xD000, 0x8040, 1);
        f.finish();
        check(f.b.read(0x8040) == 0x89, "GDMA must use the selected WRAM bank");
        f.start(0x8000, 0x8060, 1);
        f.finish();
        check(f.b.read(0x8060) == 0xFF, "invalid VRAM source must not be routed through CPU memory reads");
        f.start(0xC000, 0x9FF0, 2);
        f.finish();
        check(f.b.read(0x8000) == 0x52 && f.b.read(0x9FF0) == 7,
              "destination overflow must stop at VRAM end without overwriting its beginning");
        f.b.write(0xFF54, 0x1F);
        const auto afterOverflow = f.b.saveState();
        f.b.loadState(afterOverflow);
        f.b.write(0xFF51, 0xC0);
        f.b.write(0xFF52, 0);
        f.b.write(0xFF55, 0);
        f.finish();
        check(f.b.read(0x8010) == 7, "a low destination write must reapply its VRAM mask after overflow");
    }

    void cpuWriteAndMidInstructionWait()
    {
        Fixture f;
        f.b.write(0xFF51, 0xC0);
        f.b.write(0xFF52, 0);
        f.b.write(0xFF53, 0);
        f.b.write(0xFF54, 0);
        f.program({0xEA, 0x55, 0xFF, 0});
        auto r = f.b.cpu().registers();
        r.a = 0;
        f.b.cpu().reset(r);
        check(f.b.step() == 16 && f.b.read(0x8000) == 0 && f.b.read(0xFF55) == 0,
              "CPU FF55 write must start a deferred transfer at the end of its write cycle");
        const auto pc = f.b.cpu().registers().pc;
        check(f.finish() == 36 && f.b.cpu().registers().pc == pc,
              "CPU FF55 instruction must be followed by GDMA bus waits");

        f.b.write(0xFF53, 0);
        f.b.write(0xFF54, 0x20);
        f.program({0x08, 0x55, 0xFF}); // LD [FF55],SP: first write starts DMA, second must wait.
        r = f.b.cpu().registers();
        r.sp = 0;
        f.b.cpu().reset(r);
        unsigned writes = 0;
        f.b.cpu().setCycleObserver([&](const auto &c)
                                   { if (c.type == Core::CPU::CPU::CycleType::Write) ++writes; });
        check(f.b.step() == 20 + 36 && writes == 2 && f.b.read(0x8020) == static_cast<std::uint8_t>(16 * 13 + 7),
              "GDMA must stall between two writes of one instruction without changing its nominal timing");
    }

    void modesAndHalt()
    {
        for (const bool compatibility : {false, true})
        {
            Fixture f(compatibility ? Core::Model::CGB : Core::Model::DMG, false);
            f.start(0xC000, 0x8000, 1);
            check(f.b.read(0xFF55) == 0xFF && f.b.step() == 4 && f.b.cpu().registers().pc == 0xFF81,
                  "DMG and CGB compatibility must not expose native-CGB GDMA registers");
            f.b.write(0xFF55, 0x80);
            check(f.b.read(0xFF55) == 0xFF && !f.b.timing().vramDMAActive,
                  "DMG and CGB compatibility must not arm HBlank DMA either");
        }
        Fixture f;
        f.program({0x76});
        f.b.step();
        f.b.write(0xFF40, 0x91);
        const auto before = f.b.baseClockCycles();
        f.start(0xC000, 0x8000, 8);
        f.finish();
        check(f.b.cpu().halted() && f.b.baseClockCycles() - before == 260 && f.b.timing().ppu.dot == 260,
              "GDMA must preserve HALT while PPU and clocks continue");
        f.start(0xC000, 0x8000, 8);
        f.b.step();
        f.b.reset();
        check(f.b.read(0xFF55) == 0xFF && f.b.step() == 4 && f.b.cpu().registers().pc == 0x101,
              "reset must cancel an active GDMA and release the CPU");
    }

    void interruptWait()
    {
        Fixture f;
        f.program({0xFB, 0x00, 0x00}); // EI; NOP.
        f.b.step();
        f.b.step();
        check(f.b.cpu().ime(), "interrupt fixture must have IME enabled");
        f.b.write(0xFFFF, 1);
        f.b.write(0xFF0F, 1);
        const auto pc = f.b.cpu().registers().pc;
        f.start(0xC000, 0x8000, 2);
        check(f.finish() == 68 && f.b.cpu().ime() && f.b.cpu().registers().pc == pc && (f.b.read(0xFF0F) & 1),
              "GDMA must defer interrupt dispatch and preserve IME and the queued request");
        check(f.b.step() == 20 && f.b.cpu().registers().pc == 0x40,
              "pending interrupt must dispatch normally after GDMA releases the CPU");
    }

    template <typename Predicate>
    void until(Fixture &f, Predicate predicate)
    {
        for (unsigned i = 0; !predicate(); ++i)
        {
            check(i < 100000, "HDMA scene must reach its next event");
            f.b.step();
        }
    }

    void armHBlank(Fixture &f, unsigned blocks)
    {
        f.b.write(0xFF51, 0xC0);
        f.b.write(0xFF52, 0);
        f.b.write(0xFF53, 0);
        f.b.write(0xFF54, 0);
        f.b.write(0xFF55, 0x80 | (blocks - 1));
    }

    void hblankSchedulingAndReplay()
    {
        for (bool fast : {false, true})
            for (bool overclock : {false, true})
            {
                Fixture f;
                if (fast)
                    f.speed();
                f.b.setCPUOverclock(overclock);
                f.program({0, 0x18, 0xFD}); // NOP; JR back to NOP, throughout the frame.
                f.b.write(0xFF40, 0x91);
                until(f, [&]
                      { return f.b.timing().ppu.mode == 3; });
                armHBlank(f, 3);
                check(f.b.read(0xFF55) == 2 && !f.b.timing().vramDMABusy,
                      "armed HDMA must expose length without holding the CPU during mode 3");
                const auto waiting = f.b.saveState();
                const auto firstLine = f.b.timing().ppu.line;
                const auto run = [&]()
                {
                    until(f, [&]
                          { return f.b.timing().vramDMABytes == 32 && !f.b.timing().vramDMABusy; });
                    check(f.b.timing().ppu.mode == 0 && f.b.timing().ppu.line == firstLine,
                          "first HDMA block must start in the visible line HBlank");
                    f.b.write(0xFF4F, 1); // Change only while the transfer is paused.
                    until(f, [&]
                          { return !f.b.timing().vramDMAActive; });
                    check(f.b.timing().ppu.line == firstLine + 2 && f.b.read(0xFF55) == 0xFF,
                          "HDMA must consume one block per line and finish after three HBlanks");
                    check(f.b.read(0x8000) == 0 && f.b.read(0x8010) == static_cast<std::uint8_t>(16 * 13 + 7) &&
                              f.b.read(0x802F) == static_cast<std::uint8_t>(47 * 13 + 7),
                          "later HDMA blocks must use the bank selected between blocks");
                    f.b.write(0xFF4F, 0);
                    check(f.b.read(0x8000) == 7 && f.b.read(0x800F) == static_cast<std::uint8_t>(15 * 13 + 7) &&
                              f.b.read(0x8010) == 0,
                          "first HDMA block must preserve the other bank and adjacent bytes");
                    return f.b.saveState();
                };
                const auto future = run();
                f.b.loadState(waiting);
                check(f.b.saveState() == waiting && run() == future,
                      "HDMA waiting for HBlank must restore both scheduling and future state");
            }
    }

    void hblankCancelAndLCD()
    {
        Fixture f;
        f.program({0, 0x18, 0xFD});
        // LCD off is a constant mode 0: one block, then wait for another edge.
        armHBlank(f, 3);
        check(f.b.timing().vramDMABusy, "HDMA started in mode 0 must request its first block");
        f.b.step();
        f.b.step();
        const auto partial = f.b.saveState();
        const auto pc = f.b.cpu().registers().pc;
        until(f, [&]
              { return !f.b.timing().vramDMABusy; });
        const auto blockFuture = f.b.saveState();
        check(f.b.cpu().registers().pc == pc, "HDMA block must hold the CPU without fetching instructions");
        f.b.loadState(partial);
        until(f, [&]
              { return !f.b.timing().vramDMABusy; });
        check(f.b.saveState() == blockFuture, "partial HDMA block must replay exactly");
        for (unsigned i = 0; i < 200; ++i)
            f.b.step();
        check(f.b.timing().vramDMABytes == 32 && f.b.read(0xFF55) == 1,
              "LCD off must not invent recurring HBlank edges or drain HDMA as GDMA");
        f.b.write(0xFF55, 0xFF);
        f.b.write(0xFF51, 0xD0);
        check(f.b.read(0xFF55) == 1, "bit-7-set writes must not restart an armed HDMA");
        f.b.write(0xFF55, 0x7F);
        check(f.b.read(0xFF55) == 0x81 && !f.b.timing().vramDMAActive,
              "HDMA cancellation must retain remaining blocks with status bit 7 set");
        const auto cancelled = f.b.saveState();
        f.b.loadState(cancelled);
        check(f.b.saveState() == cancelled, "cancelled HDMA length must round-trip");
        f.b.write(0xFF55, 0); // GDMA resumes at the next source/destination block.
        f.finish();
        check(f.b.read(0x8010) == static_cast<std::uint8_t>(16 * 13 + 7),
              "restart after cancellation must preserve source/destination progress");

        f.b.write(0xFF40, 0x91);
        until(f, [&]
              { return f.b.timing().ppu.mode == 3; });
        armHBlank(f, 2);
        f.b.write(0xFF40, 0);
        until(f, [&]
              { return !f.b.timing().vramDMABusy && f.b.timing().vramDMABytes == 16; });
        f.b.write(0xFF40, 0x91);
        until(f, [&]
              { return !f.b.timing().vramDMAActive; });
        check(f.b.timing().ppu.line == 0, "LCD re-enable must resume the next block at its first HBlank");
        until(f, [&]
              { return f.b.timing().ppu.mode == 3; });
        armHBlank(f, 2);
        f.b.reset();
        check(!f.b.timing().vramDMAActive && !f.b.timing().vramDMABusy && f.b.read(0xFF55) == 0xFF,
              "reset must cancel waiting HDMA and discard its edge and request latches");
    }

    void hblankVblankAndHalt()
    {
        for (bool fast : {false, true})
        {
            Fixture f;
            if (fast)
                f.speed();
            f.program({0, 0x18, 0xFD});
            f.b.write(0xFF40, 0x91);
            until(f, [&]
                  { return f.b.timing().ppu.line == 143 && f.b.timing().ppu.mode == 3; });
            armHBlank(f, 3);
            until(f, [&]
                  { return f.b.timing().ppu.mode == 1; });
            check(f.b.timing().vramDMABytes == 32, "last visible HBlank must transfer exactly one block");
            const auto vblank = f.b.saveState();
            until(f, [&]
                  { return f.b.timing().ppu.line == 0 && f.b.timing().ppu.mode == 2; });
            check(f.b.timing().vramDMABytes == 32, "HDMA must transfer no bytes during all ten VBlank lines");
            until(f, [&]
                  { return !f.b.timing().vramDMAActive; });
            const auto future = f.b.saveState();
            f.b.loadState(vblank);
            until(f, [&]
                  { return !f.b.timing().vramDMAActive; });
            check(f.b.saveState() == future, "VBlank-paused HDMA must replay across frame wrap");

            until(f, [&]
                  { return f.b.timing().ppu.mode == 3; });
            armHBlank(f, 2);
            f.program({0x76, 0, 0x18, 0xFD});
            f.b.step();
            const auto start = f.b.baseClockCycles();
            until(f, [&]
                  { return f.b.baseClockCycles() - start >= 1000; });
            check(f.b.cpu().halted() && f.b.timing().vramDMABytes == 32,
                  "HALT must pause HDMA across HBlanks while PPU keeps running");
            const auto halted = f.b.saveState();
            const auto resume = [&]()
            {
                f.b.write(0xFFFF, 1);
                f.b.write(0xFF0F, 1); // IME is clear: wake HALT without dispatch.
                until(f, [&]
                      { return !f.b.timing().vramDMAActive; });
                check(!f.b.cpu().halted(), "HDMA must resume after the CPU wakes from HALT");
                return f.b.saveState();
            };
            const auto resumed = resume();
            f.b.loadState(halted);
            check(resume() == resumed, "HALT-paused HDMA must replay its wake and completion");
        }
    }

    void hblankLimitsAndTiming()
    {
        for (bool fast : {false, true})
        {
            Fixture f;
            if (fast)
                f.speed();
            f.program({0, 0x18, 0xFD});
            armHBlank(f, 2);
            const auto before = f.b.clockCycles();
            until(f, [&]
                  { return !f.b.timing().vramDMABusy; });
            check(f.b.clockCycles() - before == (fast ? 68 : 36),
                  "each HDMA block must retain the GDMA one-block bus timing in both speeds");
            f.b.write(0xFF55, 0);
            f.b.write(0xFF40, 0x91);
            armHBlank(f, 128);
            check(!f.b.timing().vramDMABusy, "LCD-on mode-0 warmup must not trigger HDMA");
            unsigned blocks = 0;
            unsigned lastLine = 999;
            bool previouslyBusy = false;
            bool previousHBlank = false;
            std::uint64_t edge = 0, blockStart = 0;
            f.b.setTickObserver([&](const auto &s)
                                {
                const bool hblank = s.ppu.mode == 0 && s.ppu.dot >= 80;
                if (hblank && !previousHBlank)
                    edge = s.baseCycles;
                previousHBlank = hblank;
                if (s.vramDMABusy && !previouslyBusy)
                {
                    check(s.ppu.mode == 0 && s.ppu.line < 144 && s.ppu.line != lastLine,
                          "HDMA must never start twice in one HBlank or during VBlank");
                    lastLine = s.ppu.line;
                    check(s.baseCycles == edge + 2, "HDMA bus request must follow HBlank entry by two dots");
                    blockStart = s.baseCycles;
                    ++blocks;
                }
                if (!s.vramDMABusy && previouslyBusy)
                    check(s.baseCycles - blockStart == (fast ? 34 : 36),
                          "HDMA block occupancy must use base-clock dots independently of KEY1");
                previouslyBusy = s.vramDMABusy; });
            until(f, [&]
                  { return !f.b.timing().vramDMAActive; });
            f.b.setTickObserver({});
            check(blocks == 128 && f.b.read(0xFF55) == 0xFF,
                  "maximum HDMA must complete all 128 distinct HBlank blocks");
            f.b.write(0xFF40, 0);
            for (unsigned i = 0; i < 0x800; ++i)
                check(f.b.read(0x8000 + i) == static_cast<std::uint8_t>(i * 13 + 7),
                      "maximum HDMA must copy exactly 2048 consecutive bytes");
        }
    }
}

int main()
{
    try
    {
        transferAndReplay();
        banksAndSources();
        cpuWriteAndMidInstructionWait();
        modesAndHalt();
        interruptWait();
        hblankSchedulingAndReplay();
        hblankCancelAndLCD();
        hblankVblankAndHalt();
        hblankLimitsAndTiming();
        std::cout << "CGB GDMA/HDMA copies, scheduling, cancellation, HALT, LCD, speed and replay passed.\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}