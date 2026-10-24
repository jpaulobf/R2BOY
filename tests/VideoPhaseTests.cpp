#include "Core/Board.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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
        unsigned speed;
        std::uint64_t origin;
        std::vector<std::uint8_t> baseline;

        explicit Fixture(unsigned profile)
            : board(profile == 0 ? Core::Model::DMG : Core::Model::CGB), speed(profile == 3 ? 2 : 1)
        {
            std::vector<std::uint8_t> rom(32768);
            rom[0x143] = profile >= 2 ? 0x80 : 0;
            board.loadCartridge(rom, "video bus phases");
            board.setUnlimitedSprites(false);
            if (profile == 3)
            {
                board.write(0xFF80, 0x10);
                board.write(0xFF81, 0);
                auto r = board.cpu().registers();
                r.pc = 0xFF80;
                board.cpu().reset(r);
                board.write(0xFF4D, 1);
                board.step();
                while (board.switchingSpeed())
                    board.step();
            }
            board.write(0xFF80, 0x76);
            auto r = board.cpu().registers();
            r.pc = 0xFF80;
            board.cpu().reset(r);
            board.write(0xFFFF, 0);
            board.step();
            board.write(0xFF40, 0);
            for (auto address : {0x8000, 0x9FFF, 0xFE00, 0xFE9F})
                board.write(address, 0x35);
            board.write(0xFF68, 0);
            board.write(0xFF69, 0x12);
            board.write(0xFF6A, 0);
            board.write(0xFF6B, 0x12);
            board.write(0xFF41, 0x10);
            board.write(0xFF40, 0x91);
            const auto start = board.baseClockCycles();
            while (board.baseClockCycles() - start < 70224)
                board.step();
            board.write(0xFF0F, 0);
            origin = board.baseClockCycles();
            baseline = board.saveState();
        }

        void prepare(unsigned end, std::uint16_t address, bool write, unsigned fine = 0)
        {
            board.loadState(baseline);
            board.write(0xFF43, static_cast<std::uint8_t>(fine));
            while (board.baseClockCycles() - origin < end - 16 / speed)
                board.step();
            check(board.baseClockCycles() - origin == end - 16 / speed, "phase probe must start on an exact M-cycle");
            board.write(0xFF80, write ? 0xEA : 0xFA); // LD [a16],A / LD A,[a16].
            board.write(0xFF81, static_cast<std::uint8_t>(address));
            board.write(0xFF82, static_cast<std::uint8_t>(address >> 8));
            auto r = board.cpu().registers();
            r.pc = 0xFF80;
            r.a = 0x6A;
            board.cpu().reset(r);
        }
    };

    void readPhases(unsigned profile)
    {
        Fixture f(profile);
        struct Probe
        {
            unsigned end;
            std::uint16_t address;
            unsigned expected, after, mask;
            bool video;
        };
        const std::vector<Probe> probes{
            {80, 0xFF41, 2, 3, 3, true},
            {252, 0xFF41, 3, 0, 3, true},
            {456, 0xFF41, 0, 2, 3, true},
            {65664, 0xFF41, 0, 1, 3, true},
            {80, 0x8000, 0x35, 0xFF, 0xFF, true},
            {80, 0x9FFF, 0x35, 0xFF, 0xFF, true},
            {252, 0x8000, 0xFF, 0x35, 0xFF, true},
            {252, 0x9FFF, 0xFF, 0x35, 0xFF, true},
            {252, 0xFE00, 0xFF, 0x35, 0xFF, true},
            {252, 0xFE9F, 0xFF, 0x35, 0xFF, true},
            {456, 0xFE00, 0x35, 0xFF, 0xFF, true},
            {456, 0xFF44, 1, 1, 0xFF, false},
            {65664, 0xFF44, 144, 144, 0xFF, false},
            {65664, 0xFF0F, 3, 3, 3, false},
        };
        auto cases = probes;
        if (profile >= 2)
            for (const auto address : {0xFF69, 0xFF6B})
            {
                cases.push_back({80, static_cast<std::uint16_t>(address), 0x12, 0xFF, 0xFF, true});
                cases.push_back({252, static_cast<std::uint16_t>(address), 0xFF, 0x12, 0xFF, true});
            }
        for (const auto &p : cases)
        {
            f.prepare(p.end, p.address, false);
            const auto initial = f.board.saveState();
            const auto finalCPU = f.board.clockCycles() + 16;
            std::string order;
            unsigned samples = 0;
            f.board.setReadObserver([&](std::uint16_t address, std::uint8_t value, const Core::Board::Timing &s)
                                    {
                if (address != p.address) return;
                check(s.cpuCycles == finalCPU && (value & p.mask) == p.expected,
                      "video phase probe sampled the wrong value or CPU timestamp");
                check(s.baseCycles == f.origin + p.end - (p.video ? 1 : 0),
                      "read latch must use its peripheral's side of the video edge");
                ++samples;
                order += 's'; });
            f.board.setTickObserver([&](const Core::Board::Timing &s)
                                    { if (s.cpuCycles == finalCPU) order += 't'; });
            f.board.cpu().setCycleObserver([&](const Core::CPU::CPU::Cycle &c)
                                           { if (c.type == Core::CPU::CPU::CycleType::Read && c.address == p.address) order += 'r'; });
            const auto run = [&]()
            {
                check(f.board.step() == 16 && f.board.baseClockCycles() == f.origin + p.end,
                      "phase scheduling must preserve instruction duration and video time");
                check((f.board.cpu().registers().a & p.mask) == p.expected &&
                          (f.board.read(p.address) & p.mask) == p.after,
                      "CPU latch and untimed post-edge read must expose the intended distinct states");
                check(samples == 1 && order == (p.video ? "str" : "tsr"),
                      "CPU must sample exactly once, then retire after the video edge");
                return f.board.saveState();
            };
            const auto future = run();
            f.board.loadState(initial);
            samples = 0;
            order.clear();
            check(run() == future, "read-latch phase must replay after loadState without a pending transaction");
            f.board.setReadObserver({});
            f.board.setTickObserver({});
            f.board.cpu().setCycleObserver({});
            f.board.loadState(initial);
            f.board.step();
            check(f.board.saveState() == future, "sampling diagnostics must not change the emulated state");
        }
    }

    void writePhases(unsigned profile)
    {
        Fixture f(profile);
        std::vector<std::uint16_t> addresses{0x8000, 0x9FFF, 0xFE00, 0xFE9F};
        if (profile >= 2)
        {
            addresses.push_back(0xFF69);
            addresses.push_back(0xFF6B);
        }
        for (const auto address : addresses)
            for (unsigned end : {80u, 252u})
            {
                f.prepare(end, address, true);
                unsigned dataReads = 0;
                f.board.setReadObserver([&](std::uint16_t a, std::uint8_t, const Core::Board::Timing &)
                                        { if (a == address) ++dataReads; });
                check(f.board.step() == 16 && dataReads == 0, "a write cycle must not read its destination");
                f.board.write(0xFF40, 0);
                const auto original = address >= 0xFF00 ? 0x12 : 0x35;
                check(f.board.read(address) == (end == 80 ? original : 0x6A),
                      "write strobe must observe post-edge locks: blocked at mode 3, accepted at HBlank");
                f.board.setReadObserver({});
            }

        // OAM release varies with fine SCX; sample one video edge before the
        // CPU-visible end, without rounding the PPU's actual HBlank boundary.
        for (unsigned fine = 0; fine < 8; ++fine)
        {
            const unsigned boundary = 252 + fine;
            const unsigned end = ((boundary * f.speed + 3) / 4) * 4 / f.speed;
            f.prepare(end, 0xFE00, false, fine);
            f.board.step();
            check(f.board.cpu().registers().a == (end == boundary ? 0xFF : 0x35),
                  "fine SCX must move the OAM release edge without a global phase offset");
        }
    }

    void horizontalScrollWritePhase(unsigned profile)
    {
        Fixture f(profile);
        f.prepare(120, 0xFF43, true, 7);
        auto r = f.board.cpu().registers();
        r.a = 8;
        f.board.cpu().reset(r);
        const auto snapshot = f.board.saveState();
        const auto end = f.board.clockCycles() + 16;
        std::vector<std::uint8_t> values;
        f.board.setTickObserver([&](const Core::Board::Timing &s)
                                {
            if (s.cpuCycles > end - 4)
                values.push_back(f.board.read(0xFF43)); });
        const auto run = [&]()
        {
            check(f.board.step() == 16, "SCX strobe must preserve CPU instruction length");
            check(values == (profile == 0 ? std::vector<std::uint8_t>{7, 8, 8, 8} : std::vector<std::uint8_t>{7, 7, 7, 7}) &&
                      f.board.read(0xFF43) == 8,
                  "DMG SCX must become visible at T2; CGB keeps its existing write phase");
            return f.board.saveState();
        };
        const auto future = run();
        f.board.loadState(snapshot);
        values.clear();
        check(run() == future, "SCX carry write must replay identically after load");
        f.board.setTickObserver({});
    }

    void rasterWritePhases(unsigned profile)
    {
        Fixture f(profile);
        f.board.write(0xFF47, 0xE4);
        for (unsigned col = 0; col < 32; ++col)
            f.board.write(0x9800 + col, 1);
        f.board.write(0x8010, 0xFF); // Unsigned tile 1, row 0: light.
        f.board.write(0x8011, 0);
        f.board.write(0x8014, 0); // Row 2: dark.
        f.board.write(0x8015, 0xFF);
        f.board.write(0x9010, 0); // Signed tile 1, row 0: dark.
        f.board.write(0x9011, 0xFF);
        f.baseline = f.board.saveState();

        // The final write M-cycle is dots 93..96 at normal speed. Map T1=93,
        // low-plane address T1=95: T2 visibility must affect the low plane too.
        // At double speed the same test retains the CGB's late-register profile.
        for (const auto address : {0xFF42, 0xFF40})
        {
            f.prepare(96, address, true);
            auto r = f.board.cpu().registers();
            r.a = address == 0xFF42 ? 2 : 0xE3;
            f.board.cpu().reset(r);
            const auto initial = f.board.saveState();
            const auto finalCPU = f.board.clockCycles() + 16;
            std::vector<std::uint8_t> values;
            f.board.setTickObserver([&](const Core::Board::Timing &s)
                                    {
                if (s.cpuCycles > finalCPU - 4)
                    values.push_back(f.board.read(address)); });
            const auto run = [&]()
            {
                check(f.board.step() == 16 && f.board.baseClockCycles() == f.origin + 96,
                      "raster writes must preserve four M-cycles and the video cadence");
                const std::uint8_t old = address == 0xFF42 ? 0 : 0x91;
                const std::uint8_t early = address == 0xFF42 ? 2 : 0xC1;
                const std::vector<std::uint8_t> expected = profile == 0 ? std::vector<std::uint8_t>{old, early, early, early} : std::vector<std::uint8_t>{old, old, old, old};
                check(values == expected && f.board.read(address) == r.a,
                      "DMG address sources must latch at T2 while other LCDC bits and CGB registers wait for completion");
                if (f.speed == 1)
                {
                    const auto expectedAddress = address == 0xFF42 ? (profile == 0 ? 0x8014 : 0x8010) : (profile == 0 ? 0x9010 : 0x8010);
                    check(f.board.timing().ppu.tileAddress == expectedAddress,
                          "the low-plane address must use the value visible at its own T1");
                }
                return f.board.saveState();
            };
            const auto future = run();
            f.board.loadState(initial);
            values.clear();
            check(run() == future, "staged LCDC/SCY writes must replay exactly after loadState");
            f.board.setTickObserver({});
            if (profile == 0 || profile == 1)
            {
                while (f.board.baseClockCycles() - f.origin < 65664)
                    f.board.step();
                const auto light = 0xFFAAAAAAu, dark = 0xFF555555u, black = 0xFF000000u;
                check(f.board.framebuffer()[0] == light,
                      "raster writes must retain the first BG tile already queued before the write");
                const auto expected = profile == 0 ? dark : address == 0xFF42 ? light
                                                                              : black;
                check(f.board.framebuffer()[8] == expected,
                      "CPU writes must affect the intended bitplanes without replacing a latched address");
            }
        }

        for (const bool enable : {false, true})
        {
            if (enable)
            {
                f.board.loadState(f.baseline);
                f.board.write(0xFF40, 0);
                f.baseline = f.board.saveState();
            }
            f.prepare(96, 0xFF40, true);
            auto r = f.board.cpu().registers();
            r.a = enable ? 0x91 : 0;
            f.board.cpu().reset(r);
            const auto finalCPU = f.board.clockCycles() + 16;
            f.board.setTickObserver([&](const Core::Board::Timing &s)
                                    {
                if (s.cpuCycles > finalCPU - 4)
                    check(f.board.read(0xFF40) == (enable ? 0 : 0x91),
                          "LCD power transitions must not expose partial address-source writes at T2"); });
            f.board.step();
            check(f.board.read(0xFF40) == r.a && f.board.timing().ppu.dot == 0,
                  "LCD power must change at completion and leave the startup clock unshifted");
            f.board.setTickObserver({});
        }
    }
}

int main()
{
    try
    {
        for (unsigned profile = 0; profile < 4; ++profile)
        {
            readPhases(profile);
            writePhases(profile);
            rasterWritePhases(profile);
            horizontalScrollWritePhase(profile);
        }
        std::cout << "CPU video read/write phase and replay tests passed.\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}