#include "Core/Board.h"
#include <iostream>
#include <stdexcept>
#include <vector>

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
            Core::Cartridge::Cartridge cartridge;
            Core::Memory::Memory memory;
            Core::IO::Interrupts interrupts;
            Core::IO::Serial serial;
            Core::IO::Timer timer;
            Core::IO::DMA dma;
            Core::PPU::PPU ppu;
            Core::IO::Joypad joypad;
            Core::BUS::BUS bus{cartridge, memory, interrupts, serial, timer, dma, ppu, joypad};

            explicit Fixture(bool color = false)
            {
                memory.reset(color);
                interrupts.reset();
                serial.reset(color);
                timer.reset(color);
                dma.reset(color);
            }

            void advance(unsigned cycles)
            {
                while (cycles-- != 0)
                    dma.tick(bus);
            }

            void fill(std::uint16_t base, std::uint8_t seed)
            {
                for (unsigned i = 0; i < 160; ++i)
                    memory.write(static_cast<std::uint16_t>(base + i), static_cast<std::uint8_t>(seed + i));
            }

            void expectOAM(std::uint8_t seed)
            {
                for (unsigned i = 0; i < 160; ++i)
                    check(memory.read(static_cast<std::uint16_t>(0xFE00 + i)) == static_cast<std::uint8_t>(seed + i),
                          "DMA must copy all 160 bytes in order");
            }
        };

        void transferAndTiming()
        {
            Fixture f;
            f.fill(0xC000, 0x21);
            f.fill(0xFE00, 0xEE);
            check(f.bus.read(0xFF46) == 0xFF, "DMG post-boot FF46");
            f.bus.write(0xFF46, 0xC0);
            check(f.bus.read(0xFF46) == 0xC0 && !f.dma.active(), "FF46 latches without immediate transfer");
            f.advance(4);
            check(f.bus.read(0xFE00) == 0xEE && !f.dma.active(), "M1 still allows OAM access");
            f.advance(3);
            check(f.memory.read(0xFE00) == 0xEE, "first byte must not be copied early");
            f.advance(1);
            check(f.dma.active() && f.bus.read(0xFE00) == 0xFF, "M2 starts OAM blocking");
            check(f.memory.read(0xFE00) == 0x21 && f.memory.read(0xFE01) == 0xEF, "one byte per M-cycle");
            f.memory.write(0xC001, 0xAB); // Leitura incremental, sem snapshot da página.
            f.advance(3);
            check(f.memory.read(0xFE01) == 0xEF, "second byte waits four T-cycles");
            f.advance(1);
            check(f.memory.read(0xFE01) == 0xAB, "DMA samples source at transfer time");
            f.advance(631); // T643: falta o último byte.
            check(f.memory.read(0xFE9F) == 0x8D && f.dma.active(), "last byte must not be copied early");
            f.advance(1);
            check(f.memory.read(0xFE9F) == 0xC0 && f.dma.active(), "last byte keeps bus blocked for its M-cycle");
            f.advance(3);
            check(f.bus.read(0xFE9F) == 0xFF, "DMA remains active until M162");
            f.advance(1);
            check(!f.dma.active() && f.bus.read(0xFE9F) == 0xC0, "M162 releases CPU accesses");
            f.memory.write(0xC000, 0x55);
            f.advance(100);
            check(f.bus.read(0xFE00) == 0x21 && f.bus.read(0xFF46) == 0xC0, "completed DMA stops and preserves FF46");
        }

        void restartAndReset()
        {
            Fixture f;
            f.fill(0xC000, 0x10);
            f.fill(0xD000, 0x70);
            f.bus.write(0xFF46, 0xC0);
            f.advance(20); // Quatro bytes da primeira página.
            f.bus.write(0xFF46, 0xD0);
            f.advance(4);
            check(f.dma.active() && f.memory.read(0xFE04) == 0x14,
                  "previous DMA continues during restart delay");
            check(f.memory.read(0xFE00) == 0x10, "restart must not copy new page immediately");
            f.advance(4);
            check(f.memory.read(0xFE00) == 0x70 && f.memory.read(0xFE01) == 0x11,
                  "restart begins at destination zero using new source");
            f.advance(639);
            check(f.dma.active(), "restart extends blocking through new last byte");
            f.advance(1);
            check(!f.dma.active(), "restarted DMA releases at correct cycle");
            f.expectOAM(0x70);

            f.bus.write(0xFF46, 0xC0);
            f.advance(4);
            f.bus.write(0xFF46, 0xD0);
            f.advance(4);
            check(!f.dma.active(), "pending start can be replaced before it becomes active");
            f.advance(644);
            f.expectOAM(0x70);
            f.bus.write(0xFF46, 0xC0);
            f.advance(8);
            f.dma.reset();
            f.advance(700);
            check(!f.dma.active() && f.bus.read(0xFF46) == 0xFF && f.bus.read(0xC000) == 0x10,
                  "reset cancels active DMA and restores CPU access");
            f.bus.write(0xFF46, 0xC0);
            f.dma.reset();
            f.advance(700);
            check(!f.dma.active() && f.memory.read(0xFE01) == 0x71, "reset cancels pending startup too");
        }

        void cpuRestrictions()
        {
            Fixture f;
            f.fill(0xC000, 0x31);
            f.bus.write(0xFF01, 0x42);
            f.bus.write(0xFFFF, 0x1F);
            f.bus.write(0xFF46, 0xC0);
            f.advance(8);
            for (const auto address : {0x0000, 0x8000, 0xA000, 0xC000, 0xE000, 0xFE00, 0xFF01, 0xFF0F, 0xFF7F, 0xFFFF})
            {
                check(f.bus.read(address) == 0xFF, "DMG only permits HRAM reads during DMA");
                f.bus.write(address, 0x99);
            }
            for (const auto address : {0xFF80, 0xFFFE})
            {
                f.bus.write(address, 0x65);
                check(f.bus.read(address) == 0x65, "HRAM boundary access during DMA");
            }
            f.advance(640);
            f.expectOAM(0x31);
            check(f.memory.read(0xC000) == 0x31 && f.memory.read(0x8000) == 0,
                  "blocked writes must not mutate source or VRAM");
            check(f.bus.read(0xFF01) == 0x42 && f.bus.read(0xFFFF) == 0x1F,
                  "blocked writes must not reach I/O or IE");
        }

        void sourcesAndBanks()
        {
            Fixture f;
            // MBC1 com quatro bancos e RAM externa.
            std::vector<std::uint8_t> rom(65536);
            rom[0x147] = 3;
            rom[0x148] = 1;
            rom[0x149] = 2;
            for (unsigned i = 0; i < 160; ++i)
            {
                rom[0x1200 + i] = static_cast<std::uint8_t>(0x20 + i);
                rom[0x8000 + i] = static_cast<std::uint8_t>(0x40 + i);
            }
            f.cartridge.load(rom, "DMA sources");
            f.bus.write(0x2000, 2);
            f.bus.write(0, 0x0A);
            for (unsigned i = 0; i < 160; ++i)
                f.bus.write(static_cast<std::uint16_t>(0xA000 + i), static_cast<std::uint8_t>(0x60 + i));
            f.fill(0x8000, 0x80);
            f.fill(0xC000, 0xA0);
            f.fill(0xDE00, 0xC0);
            f.fill(0xDF00, 0xD0);
            const std::uint8_t pages[] = {0x12, 0x40, 0xA0, 0x80, 0xE0, 0xFE, 0xFF};
            const std::uint8_t seeds[] = {0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xD0};
            for (unsigned i = 0; i < 7; ++i)
            {
                f.bus.write(0xFF46, pages[i]);
                f.advance(648);
                f.expectOAM(seeds[i]);
            }

            Fixture color(true);
            check(color.bus.read(0xFF46) == 0, "CGB post-boot FF46");
            color.bus.write(0xFF70, 3);
            color.fill(0xD000, 0x11);
            color.bus.write(0xFF70, 1);
            color.fill(0xD000, 0x22);
            color.bus.write(0xFF70, 3);
            color.bus.write(0xFF46, 0xD0);
            color.advance(648);
            color.expectOAM(0x11);
            color.bus.write(0xFF4F, 1);
            color.fill(0x8000, 0x33);
            color.bus.write(0xFF4F, 0);
            color.fill(0x8000, 0x44);
            color.bus.write(0xFF4F, 1);
            color.bus.write(0xFF46, 0x80);
            color.advance(648);
            color.expectOAM(0x33);
            for (const auto page : {0xE0, 0xFE, 0xFF})
            {
                color.bus.write(0xFF46, page);
                color.advance(648);
                for (unsigned i = 0; i < 160; ++i)
                    check(color.bus.read(static_cast<std::uint16_t>(0xFE00 + i)) == 0xFF,
                          "CGB invalid DMA sources return FF instead of DMG mirror");
            }
        }

        void colorBuses()
        {
            for (const auto page : {0x40, 0x80, 0xC0})
            {
                Fixture f(true);
                f.bus.write(0xFF46, page);
                f.advance(8);
                check(f.dma.blocksCPU(0x4000) == (page == 0x40), "CGB cartridge bus arbitration");
                check(f.dma.blocksCPU(0xA000) == (page == 0x40), "ROM and cartridge RAM share a bus");
                check(f.dma.blocksCPU(0x8000) == (page == 0x80), "CGB VRAM bus arbitration");
                check(f.dma.blocksCPU(0xC000) == (page == 0xC0) &&
                          f.dma.blocksCPU(0xE000) == (page == 0xC0),
                      "CGB WRAM and echo bus arbitration");
                check(f.dma.blocksCPU(0xFE00), "CGB OAM always blocked during DMA");
                f.bus.write(0xFE00, 0x77);
                check(f.memory.read(0xFE00) != 0x77 && f.bus.read(0xFE00) == 0xFF, "CGB OAM CPU access blocked");
                for (const auto address : {0x8000, 0xC000})
                {
                    f.bus.write(address, 0x55);
                    check(f.memory.read(address) == (f.dma.blocksCPU(address) ? 0 : 0x55), "CGB write arbitration");
                    check(f.bus.read(address) == (f.dma.blocksCPU(address) ? 0xFF : 0x55), "CGB read arbitration");
                }
                f.bus.write(0xFF01, 0x32);
                f.bus.write(0xFFFF, 0x10);
                check(f.bus.read(0xFF01) == 0x32 && f.bus.read(0xFFFF) == 0x10 && f.bus.read(0xFF46) == page,
                      "CGB I/O and IE remain accessible");
            }
        }

        void boardIntegration()
        {
            for (const auto model : {Core::Model::DMG, Core::Model::CGB})
            {
                Core::Board board(model);
                board.loadCartridge(std::vector<std::uint8_t>(32768), "DMA integration");
                board.write(0xFF40, 0); // Isola DMA dos bloqueios e IRQs da PPU.
                board.write(0xFFFF, 0);
                board.write(0xFF0F, 0);
                board.write(0xFF04, 0);
                for (unsigned i = 0; i < 160; ++i)
                    board.write(static_cast<std::uint16_t>(0xC000 + i), static_cast<std::uint8_t>(i + 1));
                // HRAM: LD A,C0; LDH [46],A; NOP; HALT.
                const std::uint8_t program[] = {0x3E, 0xC0, 0xE0, 0x46, 0x00, 0x76};
                for (unsigned i = 0; i < sizeof(program); ++i)
                    board.write(static_cast<std::uint16_t>(0xFF80 + i), program[i]);
                auto registers = board.cpu().registers();
                registers.pc = 0xFF80;
                board.cpu().reset(registers);
                board.step();
                board.step();
                const auto start = board.clockCycles();
                check(start == 20 && board.read(0xFE00) == 0, "FF46 CPU write begins after instruction cycles");
                board.step();
                check(board.read(0xC000) == 1, "CPU access allowed on first M-cycle after FF46");
                board.step();
                check(board.cpu().halted() && board.read(0xC000) == 0xFF, "HRAM execution continues during DMA");
                check(board.read(0x0100) == (model == Core::Model::CGB ? 0 : 0xFF),
                      "CGB DMA arbitration follows physical model even for a DMG cartridge");
                for (unsigned i = 0; i < 159; ++i)
                    board.step();
                check(board.clockCycles() - start == 644 && board.read(0xFE00) == 0xFF,
                      "Board clocks DMA during HALT and preserves final blocked cycle");
                board.step();
                check(board.read(0xFF04) == 2, "timer keeps ticking alongside DMA");
                for (unsigned i = 0; i < 160; ++i)
                    check(board.read(static_cast<std::uint16_t>(0xFE00 + i)) == static_cast<std::uint8_t>(i + 1),
                          "Board DMA completes while CPU halted");
                check(board.executionMode() == Core::ExecutionMode::DMG, "CGB test uses a DMG cartridge");
                board.write(0xFF46, 0xC0);
                board.step();
                board.step();
                board.reset();
                check(board.read(0xFF40) == 0x91, "Board reset restores LCDC");
                board.write(0xFF40, 0);
                check(board.clockCycles() == 0 && board.read(0xFE00) == 0 && board.read(0xC000) == 0,
                      "Board reset clears DMA and restores memory access");
            }
        }
    }

    int runDMATests()
    {
        try
        {
            transferAndTiming();
            restartAndReset();
            cpuRestrictions();
            sourcesAndBanks();
            colorBuses();
            boardIntegration();
            std::cout << "OAM DMA transfer, timing, restart, bus and integration tests passed.\n";
            return 0;
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
}

int main() { return R2BOY::Test::runDMATests(); }
