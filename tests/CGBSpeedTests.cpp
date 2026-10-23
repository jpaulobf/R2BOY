#include "Core/Board.h"
#include <initializer_list>
#include <iostream>
#include <stdexcept>

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
        Core::Board board;

        Fixture(Core::Model model = Core::Model::CGB, bool color = true) : board(model)
        {
            std::vector<std::uint8_t> rom(32768);
            rom[0x143] = color ? 0x80 : 0;
            board.loadCartridge(rom, "CGB speed test");
            board.write(0xFFFF, 0);
            board.write(0xFF0F, 0);
        }

        void program(std::initializer_list<std::uint8_t> bytes, std::uint16_t start = 0xC000)
        {
            auto address = start;
            for (auto byte : bytes)
                board.write(address++, byte);
            auto registers = board.cpu().registers();
            registers.pc = start;
            board.cpu().reset(registers);
        }

        void beginSwitch()
        {
            program({0x10, 0, 0x18, 0xFE}); // STOP; JR -2
            board.write(0xFF4D, 1);
            check(board.step() == 4 && board.switchingSpeed() && !board.cpu().stopped(),
                  "armed STOP must enter speed switching instead of standby");
            check(board.cpu().registers().pc == 0xC002, "STOP must consume its padding byte");
        }

        void finishSwitch()
        {
            while (board.switchingSpeed())
                board.step();
        }

        void doubleSpeed()
        {
            beginSwitch();
            finishSwitch();
            check(board.doubleSpeed(), "CGB must enter double speed");
        }

        // HALT fornece um M-cycle por chamada, sem executar código durante DMA.
        void halt()
        {
            program({0x76}, 0xFF80);
            board.step();
            check(board.cpu().halted(), "test CPU must enter HALT");
        }
    };

    void key1AndStop()
    {
        Fixture f;
        check(f.board.read(0xFF4D) == 0x7E && !f.board.doubleSpeed(), "CGB starts in normal speed");
        f.board.write(0xFF4D, 0xFF);
        check(f.board.read(0xFF4D) == 0x7F && !f.board.doubleSpeed(), "only KEY1 bit zero is writable");
        f.board.write(0xFF4D, 0x80);
        check(f.board.read(0xFF4D) == 0x7E, "writing zero must disarm KEY1");
        const auto armed = [&]()
        {
            f.board.write(0xFF4D, 1);
            return f.board.saveState();
        }();
        f.board.write(0xFF4D, 0);
        f.board.loadState(armed);
        check(f.board.read(0xFF4D) == 0x7F, "save state must restore the armed request");
        f.board.write(0xFF04, 0);
        f.board.write(0xFF01, 'P');
        f.board.write(0xFF02, 0x81);
        f.beginSwitch();
        check(f.board.read(0xFF4D) == 0xFE && f.board.read(0xFF04) == 0,
              "speed switch must expose new speed, clear preparation and reset DIV");
        const auto cpuStart = f.board.clockCycles();
        const auto baseStart = f.board.baseClockCycles();
        for (unsigned m = 0; m < 2050; ++m)
        {
            check(f.board.switchingSpeed(), "speed-switch pause must last 2050 M-cycles");
            check(f.board.step() == 4 && f.board.cpu().registers().pc == 0xC002,
                  "no instruction may execute during the speed-switch pause");
            check(f.board.read(0xFF04) == 0, "DIV must remain frozen during speed switching");
        }
        check(!f.board.switchingSpeed() && f.board.clockCycles() - cpuStart == 8200 &&
                  f.board.baseClockCycles() - baseStart == 4100,
              "double-speed stabilization must advance the base clock at half the CPU rate");
        check(f.board.read(0xFF01) == 'P' && (f.board.read(0xFF02) & 0x80) && !(f.board.read(0xFF0F) & 8),
              "serial must share the frozen system counter during speed-switch stabilization");
        check(f.board.step() == 12 && f.board.cpu().registers().pc == 0xC002,
              "execution must resume after speed stabilization");
        f.beginSwitch();
        check(f.board.read(0xFF4D) == 0x7E && !f.board.doubleSpeed(), "a second armed STOP must restore normal speed");
        const auto returnStart = f.board.baseClockCycles();
        f.finishSwitch();
        check(f.board.baseClockCycles() - returnStart == 8200, "normal-speed pause must use the normal base clock");

        for (const auto model : {Core::Model::DMG, Core::Model::CGB})
        {
            Fixture dmg(model, false);
            dmg.board.write(0xFF4D, 1);
            check(dmg.board.read(0xFF4D) == 0xFF, "KEY1 must be unavailable in DMG mode");
            dmg.program({0x10, 0});
            dmg.board.step();
            check(dmg.board.cpu().stopped() && !dmg.board.switchingSpeed() && !dmg.board.doubleSpeed(),
                  "DMG STOP must remain standby even after a KEY1 write");
        }
        f.program({0x10, 0});
        f.board.step();
        check(f.board.cpu().stopped(), "unarmed CGB STOP must remain standby");
        auto frozen = f.board.baseClockCycles();
        for (unsigned i = 0; i < 20; ++i)
            f.board.step();
        check(f.board.baseClockCycles() == frozen, "ordinary STOP must freeze the base clock");
        f.board.setButtons(1);
        check(!f.board.cpu().stopped(), "ordinary STOP must still wake on input");
    }

    void peripheralClocks()
    {
        for (const bool doubled : {false, true})
        {
            Fixture f;
            if (doubled)
                f.doubleSpeed();
            f.halt();
            f.board.write(0xFF04, 0);
            f.board.write(0xFF05, 0);
            f.board.write(0xFF07, 5);
            const auto baseStart = f.board.baseClockCycles();
            const unsigned steps = doubled ? 128 : 64;
            for (unsigned i = 0; i < steps; ++i)
                f.board.step();
            check(f.board.baseClockCycles() - baseStart == 256, "timing budget must use the base clock");
            check(f.board.read(0xFF04) == (doubled ? 2 : 1) &&
                      f.board.read(0xFF05) == (doubled ? 32 : 16),
                  "DIV and TIMA must run twice as fast per base interval in double speed");

            for (const bool fastSerial : {false, true})
            {
                f.board.write(0xFF04, 0); // Alinha a fase comum antes de medir oito bits completos.
                f.board.write(0xFF01, 'A');
                f.board.write(0xFF02, fastSerial ? 0x83 : 0x81);
                const auto serialStart = f.board.baseClockCycles();
                const unsigned duration = fastSerial ? 128 : 4096;
                for (unsigned t = 0; t < duration - 4; t += 4)
                    f.board.step();
                check((f.board.read(0xFF02) & 0x80) != 0, "serial must remain busy before its last M-cycle");
                f.board.step();
                check(f.board.takeSerialOutput() == "A" && (f.board.read(0xFF0F) & 8) != 0,
                      "serial must finish on CPU-clock timing and request its interrupt");
                check(f.board.baseClockCycles() - serialStart == duration / (doubled ? 2 : 1),
                      "both serial rates must double without changing their CPU-cycle count");
            }

            f.board.write(0xFF40, 0);
            for (unsigned i = 0; i < 160; ++i)
                f.board.write(static_cast<std::uint16_t>(0xC000 + i), static_cast<std::uint8_t>(i + 1));
            const auto dmaStart = f.board.baseClockCycles();
            f.board.write(0xFF46, 0xC0);
            for (unsigned t = 0; t < 644; t += 4)
                f.board.step();
            check(f.board.read(0xFE00) == 0xFF, "DMA must preserve its final blocked M-cycle");
            f.board.step();
            check(f.board.baseClockCycles() - dmaStart == 648 / (doubled ? 2 : 1),
                  "OAM DMA must run twice as fast in base time");
            for (unsigned i = 0; i < 160; ++i)
                check(f.board.read(static_cast<std::uint16_t>(0xFE00 + i)) == static_cast<std::uint8_t>(i + 1),
                      "OAM DMA must preserve every copied byte in both speed modes");
        }
    }

    void softwareSwitchAndInterrupts()
    {
        // Executa a sequência real da ROM, incluindo escritas e leituras temporizadas de KEY1.
        std::vector<std::uint8_t> rom(32768);
        rom[0x143] = 0x80;
        const std::uint8_t program[] = {
            0x3E, 1, 0xE0, 0x4D, 0x10, 0, // LD A,1; LDH [KEY1],A; STOP
            0xF0, 0x4D, 0xEA, 0, 0xC0,     // LDH A,[KEY1]; LD [C000],A
            0x3E, 1, 0xE0, 0x4D, 0x10, 0, // Switch back
            0xF0, 0x4D, 0xEA, 1, 0xC0,     // LDH A,[KEY1]; LD [C001],A
            0x76};
        for (unsigned i = 0; i < sizeof(program); ++i)
            rom[0x100 + i] = program[i];
        Core::Board board(Core::Model::CGB);
        board.loadCartridge(rom, "KEY1 software sequence");
        for (unsigned i = 0; i < 5000 && !board.cpu().halted(); ++i)
            board.step();
        check(board.cpu().halted() && board.read(0xC000) == 0xFE && board.read(0xC001) == 0x7E,
              "a CGB ROM must switch both ways and observe KEY1 without Engine intervention");

        Fixture f;
        f.program({0xFB, 0, 0x10, 0, 0}); // EI; NOP; STOP; NOP
        f.board.write(0xFF4D, 1);
        f.board.step();
        f.board.step();
        f.board.step();
        check(f.board.cpu().ime() && f.board.switchingSpeed(), "switching must preserve IME");
        f.board.write(0xFFFF, 1);
        f.board.write(0xFF0F, 1);
        for (unsigned m = 0; m < 2050; ++m)
        {
            f.board.step();
            check(f.board.cpu().registers().pc == 0xC004, "IRQ must not execute during the switching pause");
        }
        check(f.board.step() == 20 && f.board.cpu().registers().pc == 0x40,
              "pending IRQ must be serviced after the switching pause");
    }

    void cpuOverclock()
    {
        // DMG, CGB em compatibilidade DMG, CGB normal e CGB double speed.
        for (unsigned mode = 0; mode < 4; ++mode)
        {
            Fixture f(mode == 0 ? Core::Model::DMG : Core::Model::CGB, mode >= 2);
            if (mode == 3)
                f.doubleSpeed();
            const unsigned nativeSpeed = mode == 3 ? 2 : 1;
            const auto key1 = f.board.read(0xFF4D);
            check(!f.board.isCPUOverclockEnabled(), "CPU overclock must start disabled");
            f.program({0x03, 0x18, 0xFD}); // INC BC; JR -3 (20 T-cycles por iteração).
            for (const bool enabled : {true, false})
            {
                f.board.setCPUOverclock(enabled);
                const auto start = f.board.baseClockCycles();
                const auto cpuStart = f.board.clockCycles();
                const auto bc = f.board.cpu().registers().bc();
                while (f.board.baseClockCycles() - start < 1000)
                    f.board.step();
                const unsigned multiplier = nativeSpeed * (enabled ? 2 : 1);
                check(f.board.cpuSpeedMultiplier() == multiplier &&
                          f.board.clockCycles() - cpuStart == 1000 * multiplier &&
                          f.board.cpu().registers().bc() - bc == 1000 * multiplier / 20,
                      "overclock must double completed CPU work per fixed base interval and restore it when disabled");
                check(f.board.read(0xFF4D) == key1, "overclock must not alter KEY1 or native CGB speed");
            }

            f.board.setCPUOverclock(true);
            f.halt();
            f.board.write(0xFF04, 0);
            f.board.write(0xFF05, 0);
            f.board.write(0xFF07, 5);
            auto start = f.board.baseClockCycles();
            while (f.board.baseClockCycles() - start < 256)
                f.board.step();
            check(f.board.read(0xFF04) == nativeSpeed && f.board.read(0xFF05) == 16 * nativeSpeed,
                  "CPU-only overclock must preserve the native timer speed");
            f.board.write(0xFF01, 'O');
            f.board.write(0xFF02, 0x81);
            start = f.board.baseClockCycles();
            while (f.board.baseClockCycles() - start < 4096 / nativeSpeed)
                f.board.step();
            check(f.board.takeSerialOutput() == "O", "CPU-only overclock must preserve native serial timing");

            f.board.write(0xFF40, 0);
            for (unsigned i = 0; i < 160; ++i)
                f.board.write(static_cast<std::uint16_t>(0xC000 + i), static_cast<std::uint8_t>(i + 1));
            start = f.board.baseClockCycles();
            f.board.write(0xFF46, 0xC0);
            while (f.board.baseClockCycles() - start < 644 / nativeSpeed)
                f.board.step();
            check(f.board.read(0xFE00) == 0xFF, "overclock must not shorten OAM DMA blocking in base time");
            while (f.board.baseClockCycles() - start < 648 / nativeSpeed)
                f.board.step();
            for (unsigned i = 0; i < 160; ++i)
                check(f.board.read(static_cast<std::uint16_t>(0xFE00 + i)) == static_cast<std::uint8_t>(i + 1),
                      "DMA must finish in native base time and preserve copied bytes with overclock");

            f.board.write(0xFF40, 0x91);
            start = f.board.baseClockCycles();
            const auto frames = f.board.frameNumber();
            const auto cpuStart = f.board.clockCycles();
            while (f.board.baseClockCycles() - start < 2 * 70224)
                f.board.step();
            check(f.board.frameNumber() - frames == 2 &&
                          f.board.clockCycles() - cpuStart == 2 * 70224 * nativeSpeed * 2,
                  "overclock must preserve video cadence while supplying twice the native CPU cycles");

            const auto snapshot = f.board.saveState();
            for (unsigned i = 0; i < 100; ++i)
                f.board.step();
            const auto future = f.board.saveState();
            f.board.loadState(snapshot);
            check(f.board.isCPUOverclockEnabled(), "load state must preserve the current overclock preference");
            for (unsigned i = 0; i < 100; ++i)
                f.board.step();
            check(f.board.saveState() == future, "overclocked execution must replay deterministically");
            f.board.setCPUOverclock(false);
            f.board.loadState(snapshot);
            check(!f.board.isCPUOverclockEnabled(), "save states must not override the menu preference");
            f.board.setCPUOverclock(true);
            f.board.reset();
            check(f.board.isCPUOverclockEnabled() && f.board.cpuSpeedMultiplier() == 2,
                  "reset must preserve the hack while returning native CGB speed to normal");
        }
    }

    void overclockDMAWaitRoutine()
    {
        for (unsigned mode = 0; mode < 4; ++mode)
        {
            for (const bool overclock : {false, true})
            {
                Fixture f(mode == 0 ? Core::Model::DMG : Core::Model::CGB, mode >= 2);
                if (mode == 3)
                    f.doubleSpeed();
                f.board.write(0xFF40, 0); // Isola o bloqueio de DMA dos bloqueios da PPU.
                for (unsigned i = 0; i < 160; ++i)
                    f.board.write(static_cast<std::uint16_t>(0xC000 + i), static_cast<std::uint8_t>(i + 1));
                // Rotina HRAM usual: inicia DMA, espera 40 iterações e retorna à WRAM.
                f.program({0x3E, 0xC0, 0xE0, 0x46, 0x3E, 40, 0x3D, 0x20, 0xFD, 0xC9}, 0xFF80);
                // CALL FF80; LD A,42; LD [C110],A; HALT. Pilha em WRAM.
                f.program({0xCD, 0x80, 0xFF, 0x3E, 0x42, 0xEA, 0x10, 0xC1, 0x76}, 0xC100);
                auto registers = f.board.cpu().registers();
                registers.sp = 0xD000;
                f.board.cpu().reset(registers);
                f.board.setCPUOverclock(overclock);
                for (unsigned step = 0; step < 2000 && !f.board.cpu().halted() && !f.board.cpu().locked(); ++step)
                    f.board.step();
                check(f.board.cpu().halted() && !f.board.cpu().locked() && f.board.read(0xC110) == 0x42,
                      "cycle-counted HRAM DMA wait must finish before RET reads the WRAM stack, including with overclock");
                for (unsigned i = 0; i < 160; ++i)
                    check(f.board.read(static_cast<std::uint16_t>(0xFE00 + i)) == static_cast<std::uint8_t>(i + 1),
                          "cycle-counted DMA routine must copy all sprites before returning");
            }
        }

        Fixture switching;
        switching.board.setCPUOverclock(true);
        switching.beginSwitch();
        const auto cpuStart = switching.board.clockCycles();
        const auto baseStart = switching.board.baseClockCycles();
        for (unsigned m = 0; m < 2050; ++m)
        {
            check(switching.board.switchingSpeed(), "overclock must preserve the native switching pause");
            switching.board.step();
        }
        check(!switching.board.switchingSpeed() && switching.board.clockCycles() - cpuStart == 8200 &&
                  switching.board.baseClockCycles() - baseStart == 4100,
              "CPU overclock must be suspended throughout KEY1 stabilization");
        auto resume = switching.board.baseClockCycles();
        switching.board.step(); // JR -2: 12 T-cycles, 3 dots com CGB double speed + overclock.
        check(switching.board.baseClockCycles() - resume == 3 && switching.board.isCPUOverclockEnabled(),
              "overclock must resume after the native switching pause without changing the preference");
    }

    void videoMemoryAndIRQs()
    {
        for (const bool doubled : {false, true})
        {
            Fixture f;
            if (doubled)
                f.doubleSpeed();
            f.halt();
            f.board.setUnlimitedSprites(false);
            f.board.write(0xFF40, 0);
            f.board.write(0x8000, 0x35);
            f.board.write(0xFE00, 0x35);
            f.board.write(0xFF68, 0);
            f.board.write(0xFF69, 0x12);
            f.board.write(0xFF41, 0x10);
            f.board.write(0xFF0F, 0);
            f.board.write(0xFF40, 0x91);
            const auto start = f.board.baseClockCycles();
            const auto advanceTo = [&](unsigned dots)
            {
                while (f.board.baseClockCycles() - start < dots)
                    f.board.step();
            };
            advanceTo(80);
            check((f.board.read(0xFF41) & 3) == 3 && f.board.read(0x8000) == 0xFF &&
                      f.board.read(0xFE00) == 0xFF && f.board.read(0xFF69) == 0xFF,
                  "CGB video-memory and palette locks must enter mode 3 in the dot domain at either CPU speed");
            f.board.write(0x8000, 0x99);
            f.board.write(0xFE00, 0x99);
            f.board.write(0xFF69, 0x99);
            advanceTo(252);
            check((f.board.read(0xFF41) & 3) == 0 && f.board.read(0x8000) == 0x35 &&
                      f.board.read(0xFE00) == 0x35 && f.board.read(0xFF69) == 0x12,
                  "HBlank must release all three locks without applying blocked writes at either CPU speed");
            advanceTo(65664);
            check((f.board.read(0xFF0F) & 3) == 3 && f.board.cpu().halted(),
                  "VBlank and STAT must latch independently of IE at normal and double speed");
            f.board.write(0xFF0F, 0);
            advanceTo(65664 + 456);
            check(!(f.board.read(0xFF0F) & 3), "VBlank line changes must not repeat a held STAT request at either speed");
        }
    }

    void videoAndStates()
    {
        for (const bool doubled : {false, true})
        {
            Fixture f;
            if (doubled)
                f.doubleSpeed();
            f.halt();
            f.board.write(0xFF40, 0);
            f.board.write(0xFF40, 0x91);
            auto start = f.board.baseClockCycles();
            auto frames = f.board.frameNumber();
            auto cpu = f.board.clockCycles();
            while (f.board.baseClockCycles() - start < 2 * 70224)
                f.board.step();
            check(f.board.frameNumber() - frames == 2 && f.board.read(0xFF44) == 0,
                  "two video frames must take the same base time at both CPU speeds");
            check(f.board.clockCycles() - cpu == 2 * 70224 * (doubled ? 2 : 1),
                  "double speed must supply twice the CPU cycles per video frame");
        }

        Fixture f;
        f.beginSwitch();
        for (unsigned i = 0; i < 500; ++i)
            f.board.step();
        auto snapshot = f.board.saveState();
        for (unsigned i = 0; i < 2000; ++i)
            f.board.step();
        auto future = f.board.saveState();
        f.board.loadState(snapshot);
        check(f.board.switchingSpeed() && f.board.doubleSpeed(), "save state must restore a partially completed switch");
        check(f.board.saveState() == snapshot, "speed-switch snapshot must round-trip exactly");
        for (unsigned i = 0; i < 2000; ++i)
            f.board.step();
        check(f.board.saveState() == future, "restoring during switching must reproduce clocks and hardware exactly");
        auto active = f.board.saveState();
        f.beginSwitch();
        f.finishSwitch();
        f.board.loadState(active);
        check(f.board.doubleSpeed() && !f.board.switchingSpeed(), "save state must restore stable double speed");
        f.board.reset();
        check(!f.board.doubleSpeed() && !f.board.switchingSpeed() && f.board.baseClockCycles() == 0 &&
                  f.board.read(0xFF4D) == 0x7E, "reset must restore normal speed and clear all clock phases");
        f.doubleSpeed();
        f.board.unloadCartridge();
        check(!f.board.doubleSpeed() && f.board.read(0xFF4D) == 0xFF, "unload must clear CGB speed state");
    }
}

int main()
{
    try
    {
        key1AndStop();
        peripheralClocks();
        softwareSwitchAndInterrupts();
        videoAndStates();
        videoMemoryAndIRQs();
        cpuOverclock();
        overclockDMAWaitRoutine();
        std::cout << "CGB speed switching, CPU overclock, clocks, DMA, video and state tests passed.\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
