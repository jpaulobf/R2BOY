#include "Core/Board.h"
#include <iostream>
#include <stdexcept>

namespace R2BOY::Test
{
    namespace
    {
        constexpr std::uint32_t white = 0xFFFFFFFF, light = 0xFFAAAAAA, dark = 0xFF555555, black = 0xFF000000;

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
            Core::IO::SpeedSwitch speedSwitch;
            Core::APU::APU apu;
            Core::BUS::BUS bus{cartridge, memory, interrupts, serial, timer, dma, ppu, joypad, speedSwitch, apu};

            explicit Fixture(bool color = false)
            {
                memory.reset(color);
                interrupts.reset();
                dma.reset(color);
                ppu.reset(color, color, false);
                bus.write(0xFF47, 0xE4);
                bus.write(0xFF48, 0xE4);
                bus.write(0xFF49, 0xE4);
                bus.write(0xFF4B, 167); // Comparador fora da tela, salvo nos testes de window.
            }

            void advance(unsigned dots)
            {
                while (dots-- != 0)
                {
                    dma.tick(bus);
                    ppu.tick(memory, interrupts, dma.active());
                }
            }

            void tile(unsigned index, std::uint8_t low, std::uint8_t high, unsigned bank = 0)
            {
                memory.write(0xFF4F, bank);
                for (unsigned row = 0; row < 8; ++row)
                {
                    memory.write(static_cast<std::uint16_t>(0x8000 + index * 16 + row * 2), low);
                    memory.write(static_cast<std::uint16_t>(0x8001 + index * 16 + row * 2), high);
                }
                memory.write(0xFF4F, 0);
            }

            void sprite(unsigned index, std::uint8_t x, std::uint8_t y, std::uint8_t tile, std::uint8_t flags = 0)
            {
                const auto base = static_cast<std::uint16_t>(0xFE00 + index * 4);
                memory.write(base, y);
                memory.write(base + 1, x);
                memory.write(base + 2, tile);
                memory.write(base + 3, flags);
            }

            void color(bool object, unsigned palette, unsigned color, unsigned rgb)
            {
                bus.write(object ? 0xFF6A : 0xFF68, static_cast<std::uint8_t>(0x80 | (palette * 8 + color * 2)));
                bus.write(object ? 0xFF6B : 0xFF69, static_cast<std::uint8_t>(rgb));
                bus.write(object ? 0xFF6B : 0xFF69, static_cast<std::uint8_t>(rgb >> 8));
            }

            void render(std::uint8_t lcdc = 0x91)
            {
                bus.write(0xFF40, lcdc);
                advance(70224 + 65664); // Primeiro frame após LCD on fica branco.
                check(ppu.frameNumber() == 2 && ppu.mode() == 1, "render publishes second frame at VBlank");
            }

            // Ensaios da cadência normal devem começar após a linha especial de LCD on.
            void steady(std::uint8_t lcdc = 0x91)
            {
                bus.write(0xFF40, lcdc);
                advance(70224);
            }

            std::uint32_t pixel(unsigned x, unsigned y = 0) const { return ppu.framebuffer()[y * 160 + x]; }
        };

        void modesAndBus()
        {
            Fixture f;
            check(f.bus.read(0xFF44) == 0 && (f.bus.read(0xFF41) & 0x87) == 0x84, "LCD off LY, STAT mask and coincidence");
            f.bus.write(0x8000, 0x12);
            f.bus.write(0xFE00, 0x34);
            f.steady();
            check(f.ppu.mode() == 2 && f.bus.read(0xFE00) == 0xFF && f.bus.read(0x8000) == 0x12,
                  "mode 2 blocks OAM but permits VRAM");
            f.bus.write(0xFE00, 0x99);
            f.advance(79);
            check(f.ppu.mode() == 2, "OAM search takes 80 dots");
            f.advance(1);
            check(f.ppu.mode() == 3 && f.bus.read(0x8000) == 0xFF, "mode 3 blocks VRAM");
            f.bus.write(0x8000, 0x99);
            f.bus.write(0xFE00, 0x99);
            f.bus.write(0xFF44, 99);
            check(f.bus.read(0xFF44) == 0, "LY ignores writes");
            f.advance(171);
            check(f.ppu.mode() == 3 && f.ppu.dot() == 251, "minimum mode 3 must not end early");
            f.advance(1);
            check(f.ppu.mode() == 0 && f.bus.read(0x8000) == 0x12 && f.bus.read(0xFE00) == 0x34,
                  "mode 3 lasts 172 dots and blocked writes leave memory intact");
            f.advance(204);
            check(f.ppu.mode() == 2 && f.ppu.dot() == 0 && f.bus.read(0xFF44) == 1, "scanline is 456 dots");
            f.bus.write(0xFF40, 0);
            f.advance(1000);
            check(f.ppu.mode() == 0 && f.ppu.dot() == 0 && f.bus.read(0xFF44) == 0,
                  "LCD off freezes PPU and resets LY");
            f.bus.write(0x8000, 0x55);
            f.bus.write(0xFE00, 0x66);
            check(f.bus.read(0x8000) == 0x55 && f.bus.read(0xFE00) == 0x66, "LCD off grants memory access");
        }

        void frameAndInterrupts()
        {
            Fixture f(true); // Evita a escrita espúria DMG neste teste da linha STAT.
            f.bus.write(0xFF41, 0x20);
            check((f.interrupts.readIF() & 2) == 0, "LCD off gates STAT interrupt line");
            f.steady();
            check((f.interrupts.readIF() & 2) != 0, "mode 2 STAT rising edge");
            f.interrupts.writeIF(0);
            f.advance(4);
            check((f.interrupts.readIF() & 2) == 0, "STAT does not repeatedly request while high");
            f.bus.write(0xFF41, 0x60);
            f.advance(452);
            check((f.interrupts.readIF() & 2) == 0, "LYC and mode sources share one STAT line without artificial edges");
            f.bus.write(0xFF41, 0x40);
            f.bus.write(0xFF45, 1);
            check((f.interrupts.readIF() & 2) != 0 && (f.bus.read(0xFF41) & 4), "LYC writes immediately update coincidence and IRQ");
            f.bus.write(0xFF41, 0);
            f.interrupts.writeIF(0);
            f.advance(65664 - 456);
            check(f.bus.read(0xFF44) == 144 && f.ppu.mode() == 1 && f.ppu.frameNumber() == 2,
                  "VBlank begins after 144 visible lines");
            check((f.interrupts.readIF() & 1) != 0, "VBlank requests interrupt");
            f.interrupts.writeIF(0);
            f.advance(456 * 9);
            check(f.bus.read(0xFF44) == 153 && f.ppu.mode() == 1 && (f.interrupts.readIF() & 1) == 0,
                  "VBlank IRQ occurs once, not every VBlank scanline");
            f.advance(3);
            check(f.bus.read(0xFF44) == 153, "LY 153 persists for first four dots");
            f.advance(1);
            check(f.bus.read(0xFF44) == 0 && f.ppu.mode() == 1, "LY resets early on line 153 without leaving VBlank");
            f.advance(452);
            check(f.ppu.mode() == 2 && f.ppu.dot() == 0, "frame period is 70224 dots");

            Fixture dmg;
            dmg.bus.write(0xFF40, 0x91);
            dmg.bus.write(0xFF41, 0);
            check((dmg.interrupts.readIF() & 2) != 0, "DMG STAT write quirk");
        }

        void completeFrameCadence()
        {
            for (unsigned fine = 0; fine < 8; ++fine)
            {
                Fixture f;
                f.ppu.setUnlimitedSprites(false);
                f.ppu.reset(false, false, true); // Perfil ligado, sem a linha especial de LCD on.
                f.bus.write(0xFF43, static_cast<std::uint8_t>(fine));
                for (unsigned frame = 0; frame < 2; ++frame)
                {
                    for (unsigned line = 0; line < 154; ++line)
                    {
                        unsigned modes[4]{};
                        f.interrupts.writeIF(0);
                        for (unsigned dot = 0; dot < 456; ++dot)
                        {
                            check(f.ppu.dot() == dot, "every scanline must retain its 456-dot phase");
                            const auto expectedLY = line == 153 && dot >= 4 ? 0 : line;
                            check(f.bus.read(0xFF44) == expectedLY, "LY must track visible/VBlank lines including early line-153 reset");
                            const auto mode = f.ppu.mode();
                            ++modes[mode];
                            check(f.ppu.blocksVRAM() == (mode == 3) &&
                                      f.ppu.blocksOAM() == (mode == 2 || mode == 3),
                                  "regular mode changes must consistently gate CPU video-memory access");
                            f.advance(1);
                        }
                        if (line < 144)
                            check(modes[2] == 80 && modes[3] == 172 + fine && modes[0] == 204 - fine && modes[1] == 0,
                                  "SCX penalty must lengthen mode 3 and shorten HBlank without changing line length");
                        else
                            check(modes[1] == 456, "all ten VBlank lines must stay in mode 1 even while LY reads zero");
                        check(bool(f.interrupts.readIF() & 1) == (line == 143),
                              "VBlank IRQ must occur only on entry to line 144");
                    }
                    check(f.ppu.frameNumber() == frame + 1 && f.ppu.dot() == 0 && f.ppu.mode() == 2 &&
                              f.bus.read(0xFF44) == 0,
                          "each 70224-dot frame must return to line zero OAM search");
                }
            }
        }

        void lcdStartupAndCoincidence()
        {
            for (const bool color : {false, true})
            {
                Fixture startup(color);
                startup.ppu.setUnlimitedSprites(false);
                startup.bus.write(0xFF41, 8);
                startup.bus.write(0xFF40, 0x91);
                check(startup.ppu.mode() == 0 && !startup.ppu.blocksOAM() && !startup.ppu.blocksVRAM() &&
                          !(startup.interrupts.readIF() & 2),
                      "LCD startup reports mode zero with accessible memory but no HBlank interrupt");
                startup.advance(80);
                check(startup.ppu.mode() == 3 && !(startup.interrupts.readIF() & 2),
                      "startup must enter transfer without a normal mode-2 interrupt");
                startup.advance(172);
                check(startup.ppu.mode() == 0 && (startup.interrupts.readIF() & 2),
                      "real HBlank after startup must raise the enabled mode-zero source");

                for (const bool previous : {false, true})
                {
                    for (const bool next : {false, true})
                    {
                        Fixture f(color);
                        f.ppu.reset(color, color, true);
                        f.bus.write(0xFF45, previous ? 144 : 1);
                        f.bus.write(0xFF41, 0x40);
                        f.advance(65664);
                        check(bool(f.bus.read(0xFF41) & 4) == previous, "coincidence fixture must reach the requested VBlank state");
                        f.interrupts.writeIF(0);
                        f.bus.write(0xFF40, 0);
                        f.bus.write(0xFF45, next ? 0 : 1);
                        f.advance(1000);
                        check(f.bus.read(0xFF44) == 0 && bool(f.bus.read(0xFF41) & 4) == previous &&
                                  !(f.interrupts.readIF() & 2),
                              "LCD off must freeze comparison despite LY reset, LYC writes and elapsed time");
                        f.bus.write(0xFF40, 0x91);
                        check(bool(f.bus.read(0xFF41) & 4) == next &&
                                  bool(f.interrupts.readIF() & 2) == (!previous && next),
                              "LCD on must recompute coincidence and request STAT only on a new rising edge");
                    }
                }

                for (const auto mask : {0x20, 0x28})
                {
                    Fixture f(color);
                    f.ppu.reset(color, color, true);
                    f.bus.write(0xFF41, static_cast<std::uint8_t>(mask));
                    f.advance(65663);
                    check(f.ppu.mode() == 0 && f.bus.read(0xFF44) == 143, "VBlank edge fixture must be in final HBlank");
                    f.interrupts.writeIF(0);
                    f.advance(1);
                    check(f.ppu.mode() == 1 && (f.interrupts.readIF() & 1), "line 144 must report VBlank and request VBlank IRQ");
                    check(bool(f.interrupts.readIF() & 2) == (!color && mask == 0x20),
                          "DMG mode-2 source pulses at VBlank entry unless another source already holds STAT high");
                    f.interrupts.writeIF(0);
                    f.advance(456 * 9);
                    check(!(f.interrupts.readIF() & 3), "VBlank entry requests must not repeat on later VBlank lines");
                }
            }
        }

        void statWritePulse()
        {
            for (unsigned profile = 0; profile < 3; ++profile)
            {
                const bool dmg = profile == 0;
                Fixture f(profile == 1);
                f.ppu.reset(!dmg, profile == 1, true);
                f.bus.write(0xFF45, 1);
                f.bus.write(0xFF41, 0);
                check(bool(f.interrupts.readIF() & 2) == dmg && (f.bus.read(0xFF41) & 0x78) == 0,
                      "STAT write pulse is internal and exists only on physical DMG");
                f.interrupts.writeIF(0);
                f.advance(1);
                f.bus.write(0xFF45, 0);
                check(!(f.interrupts.readIF() & 2), "LYC change during an already-high write pulse must not retrigger STAT");
                f.bus.write(0xFF45, 1);
                f.advance(2);
                f.bus.write(0xFF41, 0);
                check(!(f.interrupts.readIF() & 2), "overlapping STAT writes must retain the shared line level");
                f.advance(4);
                f.bus.write(0xFF41, 0);
                check(bool(f.interrupts.readIF() & 2) == dmg,
                      "a new STAT write may raise an edge after the previous pulse expires");

                Fixture crossing(profile == 1);
                crossing.ppu.reset(!dmg, profile == 1, true);
                crossing.bus.write(0xFF45, 1);
                crossing.advance(250);
                crossing.bus.write(0xFF41, 0);
                check(!(crossing.interrupts.readIF() & 2), "STAT pulse in mode 3 with unequal LY/LYC starts low");
                crossing.advance(1);
                check(!(crossing.interrupts.readIF() & 2), "STAT pulse must not create an IRQ before HBlank");
                crossing.advance(1);
                check(bool(crossing.interrupts.readIF() & 2) == dmg,
                      "a still-pending write pulse must observe entry to HBlank");
                crossing.interrupts.writeIF(0);
                crossing.advance(2);
                check(!(crossing.interrupts.readIF() & 2), "pulse expiration must not request another interrupt");
                crossing.bus.write(0xFF41, 0x40);
                crossing.interrupts.writeIF(0);
                crossing.bus.write(0xFF40, 0);
                crossing.bus.write(0xFF45, 0);
                crossing.advance(4);
                check(!(crossing.interrupts.readIF() & 2), "LCD off cancels a pending pulse without recomputing frozen coincidence");
            }
        }

        void videoMemoryAccess()
        {
            for (unsigned profile = 0; profile < 3; ++profile)
            {
                Fixture f(profile == 1);
                f.ppu.reset(profile != 0, profile == 1, true);
                f.ppu.setUnlimitedSprites(false);
                const auto probe = [&](bool vramBlocked, bool oamBlocked)
                {
                    for (const auto address : {0x8000, 0x9FFF, 0xFE00, 0xFE9F})
                    {
                        const bool blocked = address >= 0xFE00 ? oamBlocked : vramBlocked;
                        const auto a = static_cast<std::uint16_t>(address);
                        f.memory.write(a, 0x35);
                        check(f.bus.read(a) == (blocked ? 0xFF : 0x35), "CPU video reads must respect both ends of VRAM/OAM");
                        f.bus.write(a, 0x6A);
                        check(f.memory.read(a) == (blocked ? 0x35 : 0x6A), "blocked CPU writes must leave video memory intact");
                    }
                };
                f.advance(79);
                probe(false, true);
                f.advance(1);
                probe(true, true);
                f.advance(171);
                probe(true, true);
                f.advance(1);
                probe(false, false);
                f.advance(203);
                probe(false, false);
                f.advance(1);
                probe(false, true);
                f.advance(65664 - 456);
                probe(false, false);
                f.advance(9 * 456 + 4);
                check(f.bus.read(0xFF44) == 0 && f.ppu.mode() == 1, "early LY zero must remain in VBlank");
                probe(false, false);
                f.advance(452);
                probe(false, true);
                f.advance(80);
                probe(true, true);
                f.bus.write(0xFF40, 0);
                probe(false, false);
            }

            // LCD off libera a PPU, mas não desativa a posse de OAM pelo DMA.
            Fixture dma;
            dma.bus.write(0xC000, 0x42);
            dma.bus.write(0xFF46, 0xC0);
            dma.advance(8);
            dma.bus.write(0xFF40, 0);
            dma.bus.write(0xFE00, 0x99);
            check(dma.bus.read(0xFE00) == 0xFF && dma.memory.read(0xFE00) == 0x42,
                  "DMA must keep OAM blocked while LCD is off");
            dma.advance(640);
            check(dma.bus.read(0xFE00) == 0x42, "OAM must become accessible after DMA releases it with LCD off");
        }

        void boardVideoInterrupts()
        {
            for (const auto model : {Core::Model::DMG, Core::Model::CGB})
            {
                Core::Board board(model);
                std::vector<std::uint8_t> rom(32768);
                rom[0x143] = model == Core::Model::CGB ? 0x80 : 0;
                rom[0x40] = rom[0x48] = 0xD9; // RETI em VBlank e STAT.
                rom[0x100] = 0xFB;
                rom[0x102] = 0x76; // EI; NOP; HALT com IE=0.
                board.loadCartridge(rom, "video IRQ integration");
                board.setUnlimitedSprites(false);
                board.write(0xFF0F, 0);
                board.step();
                board.step();
                board.step();
                check(board.cpu().halted(), "video IRQ fixture must enter HALT with IME on and IE off");
                board.write(0xFF40, 0);
                board.write(0xFF45, 1);
                board.write(0xFF41, 0x10);
                board.write(0xFF40, 0x91);
                const auto start = board.baseClockCycles();
                while (board.baseClockCycles() - start < 65664 - 16)
                    board.step();
                board.write(0xFF46, 0xC0);
                while (board.baseClockCycles() - start < 65664)
                    board.step();
                check(board.frameNumber() == 1 && board.read(0xFF44) == 144 && (board.read(0xFF41) & 3) == 1 &&
                          (board.read(0xFF0F) & 3) == 3 && board.cpu().halted(),
                      "VBlank publication and both video IRQs must occur with IE off during DMA and HALT");
                while (board.baseClockCycles() - start < 65664 - 16 + 648)
                    board.step();
                check((board.read(0xFF0F) & 3) == 3, "DMA completion must preserve pending video IRQs");
                board.write(0xFFFF, 3);
                check(board.step() == 20 && board.cpu().registers().pc == 0x40 && (board.read(0xFF0F) & 3) == 2,
                      "enabling IE must wake HALT and service VBlank before STAT without losing STAT");
                board.step(); // RETI.
                check(board.step() == 20 && board.cpu().registers().pc == 0x48 && !(board.read(0xFF0F) & 3),
                      "RETI must allow the pending STAT interrupt to be serviced next");
                board.step(); // RETI.
                board.step();
                check(!(board.read(0xFF0F) & 3), "acknowledging STAT while its source stays high must not retrigger it");
            }
        }

        void backgroundAndScroll()
        {
            Fixture f;
            f.tile(0, 0xAA, 0xCC); // IDs 3,2,1,0 repetidos.
            f.bus.write(0xFF43, 3);
            f.bus.write(0xFF40, 0x91);
            f.advance(254);
            check(f.ppu.mode() == 3, "fine SCX adds discard dots to mode 3");
            f.advance(1);
            check(f.ppu.mode() == 0, "fine SCX penalty is SCX modulo 8");
            f.advance(70224 - 255 + 65664);
            check(f.pixel(0) == white && f.pixel(1) == black && f.pixel(2) == dark && f.pixel(3) == light,
                  "FIFO decodes bitplanes and discards fine-scroll pixels");
            check(f.pixel(159, 143) == light, "entire visible frame is rendered");

            Fixture signedTiles;
            signedTiles.tile(256, 0xFF, 0); // Tile 0 em endereçamento 9000.
            signedTiles.tile(255, 0, 0xFF); // Tile -1 em 8FF0.
            signedTiles.memory.write(0x9FFF, 0xFF);
            signedTiles.bus.write(0xFF43, 248);
            signedTiles.bus.write(0xFF42, 248);
            signedTiles.render(0x89); // BG map 9C00, tiles signed.
            check(signedTiles.pixel(0) == dark && signedTiles.pixel(8) == light,
                  "signed tile addressing, map selection and horizontal wrap");
            check(signedTiles.pixel(0, 8) == light, "vertical scroll wraps at 256");

            Fixture disabled;
            disabled.tile(0, 0xFF, 0xFF);
            disabled.bus.write(0xFF47, 0xFF);
            disabled.render(0x90);
            check(disabled.pixel(0) == white, "DMG LCDC bit zero blanks BG independently of BGP");
        }

        void windowAndRaster()
        {
            Fixture f;
            f.tile(1, 0xFF, 0);
            for (unsigned i = 0; i < 32; ++i)
                f.memory.write(static_cast<std::uint16_t>(0x9C00 + i), 1);
            f.bus.write(0xFF4A, 1);
            f.bus.write(0xFF4B, 87); // Início em x=80, y=1.
            f.render(0xF1);
            check(f.pixel(80, 0) == white && f.pixel(79, 1) == white && f.pixel(80, 1) == light,
                  "window uses WX minus seven and WY trigger");
            check(f.pixel(80, 8) == light && f.pixel(80, 9) == white, "window maintains its own line counter");

            Fixture timing;
            timing.bus.write(0xFF4B, 87);
            timing.bus.write(0xFF40, 0xB1);
            timing.advance(257);
            check(timing.ppu.mode() == 3, "window restart adds six dots");
            timing.advance(1);
            check(timing.ppu.mode() == 0, "window FIFO restarts without full scanline rendering");

            Fixture paused;
            paused.tile(1, 0xFF, 0);
            paused.tile(2, 0, 0xFF);
            for (unsigned i = 0; i < 32; ++i)
            {
                paused.memory.write(static_cast<std::uint16_t>(0x9C00 + i), 1);
                paused.memory.write(static_cast<std::uint16_t>(0x9C20 + i), 2);
            }
            paused.bus.write(0xFF4B, 7);
            paused.bus.write(0xFF40, 0xF1);
            paused.advance(70224 + 456 * 4);
            paused.bus.write(0xFF40, 0xD1);
            paused.advance(456 * 8);
            paused.bus.write(0xFF40, 0xF1);
            paused.advance(65664 - 456 * 12);
            check(paused.pixel(0, 4) == white && paused.pixel(0, 11) == white &&
                      paused.pixel(0, 12) == light && paused.pixel(0, 16) == dark,
                  "hidden window lines pause its internal Y counter and preserve WY trigger");

            Fixture raster;
            raster.tile(0, 0xFF, 0);
            raster.bus.write(0xFF40, 0x91);
            raster.advance(70224 + 80 + 12 + 80); // Segunda linha 0: 80 pixels já emitidos.
            raster.bus.write(0xFF47, 0xFC);       // ID 1 muda de cinza para preto na saída.
            raster.advance(65664 - 172);
            check(raster.pixel(79) == light && raster.pixel(80) == black,
                  "palette writes affect pixels as FIFO emits them, not whole scanlines");
            const auto published = raster.ppu.framebuffer();
            raster.advance(456 * 10 + 100);
            check(raster.ppu.framebuffer() == published, "published framebuffer remains stable during next visible frame");
            raster.bus.write(0xFF40, 0);
            check(raster.pixel(0) == white, "LCD off immediately publishes blank display");
        }

        void windowDisableAndRestart()
        {
            for (unsigned fine = 0; fine < 8; ++fine)
            {
                for (const bool moveWX : {false, true})
                {
                    Fixture f;
                    f.ppu.setUnlimitedSprites(false);
                    f.tile(0, 0xFF, 0); // BG cinza claro.
                    f.tile(1, 0, 0);    // Janela: linha 0 escura, linha 1 preta, linha 2 branca.
                    f.memory.write(0x8011, 0xFF);
                    f.memory.write(0x8012, 0xFF);
                    f.memory.write(0x8013, 0xFF);
                    for (unsigned i = 0; i < 32; ++i)
                        f.memory.write(static_cast<std::uint16_t>(0x9C00 + i), 1);
                    f.bus.write(0xFF43, static_cast<std::uint8_t>(fine));
                    f.bus.write(0xFF4B, 7);
                    f.steady(0xF1);
                    f.advance(101 + fine); // Três pixels da janela já saíram.
                    f.bus.write(0xFF40, 0xD1);
                    f.advance(5); // Termina o tile corrente, em vez de cortar no pixel 3.
                    if (moveWX)
                        f.bus.write(0xFF4B, 31); // Novo comparador em x=24, à frente do feixe.
                    f.bus.write(0xFF40, 0xF1);
                    f.advance(65664 - 106 - fine);
                    for (unsigned x = 0; x < 160; ++x)
                    {
                        const auto expected = x < 8 ? dark : moveWX && x >= 24 ? black
                                                                               : light;
                        check(f.pixel(x) == expected,
                              "DMG must finish the window tile, resume BG without fine-scroll discard, and restart only at future WX");
                    }
                    check(f.pixel(moveWX ? 24 : 0, 1) == (moveWX ? white : black),
                          "each window restart must advance its internal row even within the same scanline");
                }
            }

            for (const bool future : {false, true})
            {
                Fixture f;
                f.ppu.setUnlimitedSprites(false);
                f.tile(0, 0xFF, 0);
                f.tile(1, 0, 0xFF);
                for (unsigned i = 0; i < 32; ++i)
                    f.memory.write(static_cast<std::uint16_t>(0x9C00 + i), 1);
                f.bus.write(0xFF4B, 15); // x=8 já terá passado quando a janela for habilitada.
                f.steady(0xD1);
                f.advance(124); // x=32.
                f.bus.write(0xFF40, 0xF1);
                if (future)
                    f.bus.write(0xFF4B, 87);
                f.advance(65664 - 124);
                for (unsigned x = 0; x < 160; ++x)
                    check(f.pixel(x) == (future && x >= 80 ? dark : x == 8 ? white
                                                                           : light),
                          "enabling a window behind the beam must wait for a new horizontal match");
            }
        }

        void fetchRegisterSampling()
        {
            for (unsigned change = 0; change < 3; ++change)
            {
                Fixture f;
                f.ppu.setUnlimitedSprites(false);
                f.tile(0, 0xFF, 0);
                f.tile(1, 0, 0xFF);
                f.tile(2, 0xFF, 0xFF);
                f.tile(3, 0, 0);
                for (unsigned x = 0; x < 32; ++x)
                {
                    f.memory.write(static_cast<std::uint16_t>(0x9800 + x), change == 0 ? x % 4 : 0);
                    f.memory.write(static_cast<std::uint16_t>(0x9820 + x), 1);
                    f.memory.write(static_cast<std::uint16_t>(0x9C00 + x), 1);
                }
                f.steady();
                f.advance(90); // Primeiro tile buscado; o seguinte aguarda o FIFO.
                if (change == 0)
                    f.bus.write(0xFF43, 15); // Coarse SCX muda; fine SCX não é reaplicado nesta linha.
                else if (change == 1)
                    f.bus.write(0xFF42, 8);
                else
                    f.bus.write(0xFF40, 0x99);
                f.advance(162);
                check(f.ppu.mode() == 0 && f.ppu.dot() == 252,
                      "mid-transfer coarse scroll or map changes must not add a new fine-scroll penalty");
                f.advance(65664 - 252);
                check(f.pixel(0) == light && f.pixel(7) == light,
                      "register writes must preserve tile pixels already fetched into the FIFO");
                check(f.pixel(8) == (change == 0 ? white : dark) &&
                          f.pixel(16) == (change == 0 ? light : dark),
                      "later tile addresses must combine live SCX with the beam, and resample SCY and LCDC map");
            }
        }

        void scrollCarryAndDisabledWindow()
        {
            Fixture carry;
            carry.ppu.setUnlimitedSprites(false);
            carry.tile(0, 0xFF, 0);
            carry.tile(1, 0, 0xFF);
            carry.tile(2, 0xFF, 0xFF);
            carry.tile(3, 0, 0);
            for (unsigned i = 0; i < 32; ++i)
                carry.memory.write(static_cast<std::uint16_t>(0x9800 + i), i % 4);
            carry.bus.write(0xFF43, 7);
            carry.steady();
            carry.advance(120);
            carry.bus.write(0xFF43, 8);
            carry.advance(65664 - 120);
            const std::uint32_t colors[] = {light, dark, black, white};
            for (unsigned x = 0; x < 160; ++x)
                check(carry.pixel(x) == colors[((x + 7) / 8) % 4],
                      "SCX carry during transfer must not skip a queued background tile");

            for (const bool color : {false, true})
                for (const bool triggered : {false, true})
                    for (unsigned fine = 0; fine < 8; ++fine)
                    {
                        Fixture f(color);
                        f.tile(0, 0xFF, 0);
                        if (color)
                        {
                            f.color(false, 0, 0, 0x7FFF);
                            f.color(false, 0, 1, 0);
                        }
                        f.bus.write(0xFF4A, triggered ? 0 : 145);
                        f.bus.write(0xFF4B, 7);
                        f.bus.write(0xFF43, fine);
                        f.render(); // Window permanece desligada em LCDC.
                        const bool inserted = !color && triggered && fine == 0;
                        check(f.pixel(0) == (inserted ? white : color ? black
                                                                      : light),
                              "disabled-window WX match inserts color zero only on triggered DMG push");
                        check(f.pixel(1) == (color ? black : light),
                              "disabled-window insertion must retain the fetched tile behind the extra pixel");
                    }
        }

        void liveFineScrollStartup()
        {
            // Matriz sintética também executada no SameBoy: escrita após dot 90.
            // SCX 0..5 já encontrou o comparador; 6/7 ainda pode perder o match.
            for (const unsigned wx : {7u, 167u})
                for (unsigned initial = 0; initial < 8; ++initial)
                    for (unsigned next = 0; next < 8; ++next)
                    {
                        Fixture f;
                        f.tile(0, 0xFF, 0);
                        f.bus.write(0xFF4B, wx);
                        f.bus.write(0xFF43, initial);
                        f.steady();
                        f.advance(90);
                        f.bus.write(0xFF43, next);
                        const unsigned discarded = initial < 6 ? initial : next < 6 ? next + 8
                                                                                    : next;
                        const bool inserted = wx == 7 && (initial == 0 || (initial >= 6 && next == 0));
                        const unsigned end = 252 + discarded;
                        f.advance(end - 91);
                        check(f.ppu.mode() == 3, "live fine-scroll match must retain the final transfer dot");
                        f.advance(1);
                        check(f.ppu.mode() == 0, "live fine-scroll wrap must determine HBlank without a global offset");
                        f.advance(65664 - end);
                        check(f.pixel(0) == (inserted ? white : light),
                              "SCX wrap during startup must expose the disabled-window insertion at WX=7");
                        for (unsigned x = 1; x < 16; ++x)
                            check(f.pixel(x) == light, "startup insertion must preserve the following tile pixels");
                    }
        }

        void sharedFetcherAlignment()
        {
            Fixture f;
            f.ppu.setUnlimitedSprites(false);
            f.sprite(0, 8, 16, 1);        // Transparente, mas disputa a busca de tiles.
            f.memory.write(0x8002, 0xFF); // Plano baixo da linha 1.
            f.memory.write(0x8005, 0xFF); // Plano alto da linha 2.
            f.steady(0x93);
            f.advance(94); // Índice do próximo BG já lido durante a espera de OBJ.
            f.bus.write(0xFF42, 1);
            f.advance(2);
            f.bus.write(0xFF42, 2);
            f.advance(65664 - 96);
            check(f.pixel(0) == white && f.pixel(7) == white && f.pixel(8) == black && f.pixel(15) == black,
                  "BG must advance through address/data phases during OBJ alignment, retaining queued pixels");
            check(f.pixel(16) == dark, "later DMG planes must sample SCY again after the shared fetch resumes");

            Fixture overlap;
            overlap.ppu.setUnlimitedSprites(false);
            overlap.sprite(0, 8, 16, 1);
            overlap.sprite(1, 9, 16, 1);
            overlap.steady(0x93);
            overlap.advance(104);
            overlap.bus.write(0xFF43, 8); // Muda geometria de tile, não o estágio real do fetcher.
            overlap.advance(164);
            check(overlap.ppu.mode() == 3, "two overlapping OBJ fetches must preserve the last transfer dot");
            overlap.advance(1);
            check(overlap.ppu.mode() == 0,
                  "the second OBJ must use the ready fetcher instead of recomputing a wait from changed SCX");
        }

        void fetchAddressAndState()
        {
            Fixture f;
            f.tile(1, 0xFF, 0);
            f.tile(2, 0, 0xFF);
            f.tile(3, 0xFF, 0xFF);
            f.memory.write(0x9801, 1);
            f.memory.write(0x9802, 3);
            f.steady();
            f.advance(93); // Endereço do próximo índice escolhido em T1; leitura em T2 pendente.
            std::vector<std::uint8_t> snapshot;
            Core::StateArchive writer(snapshot);
            writer(f.ppu, f.memory, f.interrupts, f.dma);
            const auto finish = [](Fixture &scene)
            {
                scene.memory.write(0x9801, 2); // Porta interna: altera dado, preserva endereço T1.
                scene.bus.write(0xFF43, 8);
                scene.advance(65664 - 93);
                check(scene.pixel(0) == white && scene.pixel(8) == dark,
                      "map address must latch at T1, while tile-index data is read at T2");
                std::vector<std::uint8_t> state;
                Core::StateArchive archive(state);
                archive(scene.ppu, scene.memory, scene.interrupts, scene.dma);
                return state;
            };
            const auto future = finish(f);
            Fixture restored;
            Core::StateArchive reader(static_cast<const std::vector<std::uint8_t> &>(snapshot));
            reader(restored.ppu, restored.memory, restored.interrupts, restored.dma);
            reader.finish();
            check(finish(restored) == future, "loading between address and data phases must reproduce the fetch exactly");
        }

        void spritesAndPriority()
        {
            Fixture f;
            f.tile(0, 0xFF, 0); // BG ID 1.
            f.tile(1, 0, 0xFF); // OBJ ID 2.
            f.sprite(0, 8, 16, 1, 0x80);
            f.sprite(1, 16, 16, 1);
            f.render(0x93);
            check(f.pixel(0) == light && f.pixel(8) == dark, "OBJ-to-BG priority uses raw BG color ID");

            Fixture overlap;
            overlap.tile(1, 0xFF, 0);
            overlap.tile(2, 0, 0xFF);
            overlap.sprite(0, 12, 16, 2);
            overlap.sprite(1, 8, 16, 1);
            overlap.render(0x93);
            check(overlap.pixel(4) == light && overlap.pixel(8) == dark, "DMG sprites prioritize smaller X before OAM order");

            Fixture tall;
            tall.tile(2, 0x80, 0);
            tall.tile(3, 0, 0x80);
            tall.sprite(0, 8, 16, 3, 0x60); // Índice ímpar ignorado, flip X/Y do objeto inteiro.
            tall.render(0x97);
            check(tall.pixel(7) == dark && tall.pixel(7, 8) == light && tall.pixel(0) == white,
                  "8x16 sprite tile masking, vertical flip and horizontal flip");

            Fixture limit;
            limit.ppu.setUnlimitedSprites(false);
            limit.tile(1, 0xFF, 0xFF);
            for (unsigned i = 0; i < 10; ++i)
                limit.sprite(i, 0, 16, 1);
            limit.sprite(10, 8, 16, 1);
            limit.render(0x93);
            check(limit.pixel(0) == white, "ten-object scanline limit counts hidden X-zero objects");

            Fixture timing;
            timing.sprite(0, 8, 16, 0);
            timing.steady(0x93);
            timing.advance(262);
            check(timing.ppu.mode() == 3, "object fetch stalls pixel output even for transparent pixels");
            timing.advance(1);
            check(timing.ppu.mode() == 0, "aligned object incurs eleven-dot penalty");
        }

        void objectFetchSampling()
        {
            for (const bool color : {false, true})
            {
                Fixture f(color);
                f.ppu.setUnlimitedSprites(false);
                f.tile(1, 0xFF, 0);
                f.tile(2, 0, 0xFF, color ? 1 : 0);
                if (color)
                    f.color(true, 1, 2, 0x001F);
                f.sprite(0, 8, 16, 1);
                f.steady(0x93);
                f.advance(80);
                // Porta interna: simula OAM alterada pelo DMA, não uma escrita
                // CPU (bloqueada). Y/X já selecionados não devem ser relidos.
                f.sprite(0, 80, 80, 2, color ? 0x09 : 0x10);
                if (!color)
                    f.bus.write(0xFF49, 0x30); // ID 2 -> preto na OBP1.
                f.advance(19);
                // Tile/atributos já buscados; alteração posterior não os substitui.
                f.sprite(0, 80, 80, 1);
                f.advance(65664 - 99);
                check(f.pixel(0) == (color ? 0xFFFF0000 : black) && f.pixel(8) == white,
                      "OBJ fetch must use mode-3 tile/flags with mode-2 Y/X and latch its metadata");
            }

            Fixture planes;
            planes.tile(2, 0, 0xFF);
            planes.tile(3, 0xFF, 0);
            planes.sprite(0, 8, 16, 3);
            planes.steady(0x93);
            planes.advance(101);            // Plano baixo já lido em 8x8.
            planes.bus.write(0xFF40, 0x97); // Plano alto usa índice par em 8x16.
            planes.advance(65664 - 101);
            check(planes.pixel(0) == black && planes.pixel(7) == black,
                  "LCDC object size must be sampled separately for each data plane");

            // Busca cancelada no DMG, mas não no CGB físico, mesmo em modo DMG.
            for (unsigned profile = 0; profile < 3; ++profile)
            {
                Fixture f(profile == 1);
                if (profile == 2)
                    f.ppu.reset(true, false, false);
                f.ppu.setUnlimitedSprites(false);
                f.sprite(0, 8, 16, 0);
                f.steady(0x93);
                f.advance(98);
                f.bus.write(0xFF40, 0x91);
                f.advance(3);
                f.bus.write(0xFF40, 0x93);
                f.advance(159); // Dot 260: DMG cancelado termina antes de 263.
                check(f.ppu.mode() == (profile == 0 ? 0u : 3u),
                      "disabling OBJ mid-fetch must cancel only on physical DMG hardware");
                f.advance(3);
                check(f.ppu.mode() == 0, "CGB fetch must finish its original penalty with OBJ off");

                Fixture disabled(profile == 1);
                if (profile == 2)
                    disabled.ppu.reset(true, false, false);
                disabled.sprite(0, 8, 16, 0);
                disabled.steady(0x91);
                disabled.advance(252);
                check(disabled.ppu.mode() == (profile == 0 ? 0u : 3u),
                      "physical CGB must fetch selected objects even when OBJ was disabled before mode 3");
            }
        }

        void objectSelectionAndMasking()
        {
            for (const bool color : {false, true})
                for (const bool transparent : {false, true})
                {
                    Fixture f(color);
                    f.ppu.setUnlimitedSprites(false);
                    f.tile(0, 0xFF, 0);
                    f.tile(1, transparent ? 0 : 0xFF, 0);
                    f.tile(2, 0, 0xFF);
                    f.sprite(0, 8, 16, 1, 0x80);
                    f.sprite(1, 8, 16, 2);
                    if (color)
                    {
                        f.color(false, 0, 1, 0x03E0);
                        f.color(true, 0, 2, 0x001F);
                    }
                    f.render(0x93);
                    check(f.pixel(0) == (transparent ? (color ? 0xFFFF0000 : dark) : (color ? 0xFF00FF00 : light)),
                          "OBJ priority precedes BG masking; only raw zero exposes the lower OBJ");
                }

            Fixture offscreen;
            offscreen.ppu.setUnlimitedSprites(false);
            offscreen.tile(1, 0xFF, 0xFF);
            for (unsigned i = 0; i < 10; ++i)
                offscreen.sprite(i, 168, 16, 1);
            offscreen.sprite(10, 8, 16, 1);
            offscreen.steady(0x93);
            offscreen.advance(252);
            check(offscreen.ppu.mode() == 0, "right-hidden objects count in OAM selection but cause no fetch stalls");
            offscreen.advance(65664 - 252);
            check(offscreen.pixel(0) == white, "select first ten by OAM order before sorting by X");

            Fixture opaqueWhite;
            opaqueWhite.tile(1, 0xFF, 0);
            opaqueWhite.tile(2, 0, 0xFF);
            opaqueWhite.sprite(0, 8, 16, 1);
            opaqueWhite.sprite(1, 8, 16, 2, 0x10);
            opaqueWhite.bus.write(0xFF48, 0); // ID 1 branco, mas não transparente.
            opaqueWhite.bus.write(0xFF49, 0x30);
            opaqueWhite.render(0x93);
            check(opaqueWhite.pixel(0) == white, "palette white must not make a nonzero OBJ color transparent");

            for (unsigned fine = 0; fine < 8; ++fine)
                for (unsigned x = 0; x <= 168; ++x)
                {
                    Fixture f;
                    f.ppu.setUnlimitedSprites(false);
                    f.sprite(0, static_cast<std::uint8_t>(x), 16, 0);
                    f.bus.write(0xFF43, static_cast<std::uint8_t>(fine));
                    f.steady(0x93);
                    const unsigned alignment = (x + fine) & 7;
                    const unsigned penalty = x == 168 ? 0 : x == 0 ? 11
                                                                   : 6 + (alignment < 5 ? 5 - alignment : 0);
                    f.advance(251 + fine + penalty);
                    check(f.ppu.mode() == 3, "OBJ fetch alignment must preserve the last mode-3 dot");
                    f.advance(1);
                    check(f.ppu.mode() == 0, "OBJ penalties must respect clipping and every fine-SCX alignment");
                }
        }

        void overlappingObjectsAtZero()
        {
            // Mooneye intr_2_mode0_timing_sprites, casos 0..9: a aquisição
            // inicial custa cinco dots; cada OBJ sobreposto usa mais seis.
            for (const bool color : {false, true})
                for (unsigned fine = 0; fine < 8; ++fine)
                    for (unsigned count = 1; count <= 10; ++count)
                    {
                        Fixture f(color);
                        f.ppu.setUnlimitedSprites(false);
                        for (unsigned i = 0; i < count; ++i)
                            f.sprite(i, 0, 16, 0);
                        f.bus.write(0xFF43, static_cast<std::uint8_t>(fine));
                        f.steady(0x93);
                        const unsigned hblank = 252 + fine + 5 + 6 * count;
                        f.advance(hblank - 1);
                        check(f.ppu.mode() == 3, "overlapping X=0 objects must retain every six-dot fetch");
                        f.advance(1);
                        check(f.ppu.mode() == 0, "ready BG fetcher must not repeat the X=0 acquisition wait");
                        f.advance(456 - hblank);
                        check(f.ppu.dot() == 0 && f.ppu.mode() == 2 && f.ppu.read(0xFF44) == 1,
                              "object handoff must shorten HBlank without shifting the scanline");
                    }
        }

        void unlimitedSprites()
        {
            for (const bool color : {false, true})
            {
                Fixture limited(color), unlimited(color);
                limited.ppu.setUnlimitedSprites(false);
                check(unlimited.ppu.isUnlimitedSpritesEnabled(), "unlimited sprites must start enabled");
                for (auto *f : {&limited, &unlimited})
                {
                    f->tile(1, 0xFF, 0xFF);
                    if (color)
                    {
                        f->color(false, 0, 0, 0x7FFF);
                        f->color(true, 0, 3, 0);
                    }
                    // Dez objetos invisíveis ocupam o pipeline nativo; os outros
                    // trinta cobrem a tela sem acrescentar penalidades de fetch.
                    for (unsigned i = 0; i < 40; ++i)
                        f->sprite(i, i < 10 ? 0 : 8 + ((i - 10) % 20) * 8, 16, 1);
                    f->bus.write(0xFF40, 0x93);
                }
                for (unsigned dot = 0; dot < 70224 + 65664; ++dot)
                {
                    limited.advance(1);
                    unlimited.advance(1);
                    check(limited.ppu.mode() == unlimited.ppu.mode() &&
                              limited.ppu.dot() == unlimited.ppu.dot() &&
                              limited.bus.read(0xFF41) == unlimited.bus.read(0xFF41) &&
                              limited.bus.read(0xFF44) == unlimited.bus.read(0xFF44) &&
                              limited.interrupts.readIF() == unlimited.interrupts.readIF(),
                          "extra sprites must preserve mode boundaries, memory locks and interrupt timing");
                }
                for (unsigned x = 0; x < 160; ++x)
                    check(limited.pixel(x) == white && unlimited.pixel(x) == black,
                          "unlimited sprites must fill the complete scanline without truncating it");
                unlimited.ppu.setUnlimitedSprites(false);
                unlimited.advance(70224);
                check(unlimited.pixel(0) == white, "disabling the hack must restore the ten-sprite limit");
                unlimited.ppu.reset(color, color, false);
                check(!unlimited.ppu.isUnlimitedSpritesEnabled(), "reset must preserve the selected sprite limit");

                Fixture last(color);
                last.tile(1, 0xFF, 0xFF);
                if (color)
                {
                    last.color(false, 0, 0, 0x7FFF);
                    last.color(true, 0, 3, 0);
                }
                for (unsigned i = 0; i < 40; ++i)
                    last.sprite(i, i == 39 ? 8 : 0, 16, 1);
                last.render(0x93);
                check(last.pixel(0) == black, "the fortieth OAM sprite must remain eligible");

                Fixture priority(color);
                priority.tile(1, 0xFF, 0);
                priority.tile(2, 0, 0xFF);
                if (color)
                {
                    priority.color(true, 0, 1, 0x7FFF);
                    priority.color(true, 0, 2, 0);
                }
                priority.sprite(0, 12, 16, 2);
                for (unsigned i = 1; i < 10; ++i)
                    priority.sprite(i, 0, 16, 0);
                priority.sprite(10, 8, 16, 1);
                priority.render(0x93);
                check(priority.pixel(4) == (color ? black : light),
                      "native and extra sprites must share DMG X priority and CGB OAM priority");
            }
            Fixture behind;
            behind.tile(0, 0xFF, 0);
            behind.tile(1, 0, 0xFF);
            for (unsigned i = 0; i < 10; ++i)
                behind.sprite(i, 0, 16, 0);
            behind.sprite(10, 8, 16, 1, 0x80);
            behind.render(0x93);
            check(behind.pixel(0) == light, "extra sprites must respect background priority");
        }

        void renderLayerVisibility()
        {
            for (const bool color : {false, true})
                for (const bool tiles : {false, true})
                    for (const bool sprites : {false, true})
                    {
                        Fixture f(color);
                        f.ppu.setRenderLayers(tiles, sprites, true);
                        // Reset mantém as preferências, como ao abrir outra ROM.
                        f.ppu.reset(color, color, false);
                        f.bus.write(0xFF47, 0xE4);
                        f.bus.write(0xFF48, 0xE4);
                        f.tile(0, 0xFF, 0); // Fundo de cor 1.
                        f.tile(1, 0, 0xFF); // OBJ de cor 2, atrás do fundo.
                        f.sprite(0, 8, 16, 1, 0x80);
                        f.sprite(1, 16, 16, 1);
                        if (color)
                        {
                            f.color(false, 0, 1, 0x001F); // Vermelho.
                            f.color(true, 0, 2, 0x03E0); // Verde.
                        }
                        f.render(0x93);
                        const auto background = color ? 0xFFFF0000u : light;
                        const auto object = color ? 0xFF00FF00u : dark;
                        check(f.pixel(0, 0) == (tiles ? background : sprites ? object : white),
                              "hidden tiles must reveal behind-priority sprites; hidden layers must be blank");
                        check(f.pixel(20, 0) == (tiles ? background : white),
                              "tiles visibility must apply outside sprite coverage");
                        check(f.pixel(8, 0) == (sprites ? object : tiles ? background : white),
                              "sprite visibility must hide foreground objects independently of tiles");
                        check(f.ppu.read(0xFF40) == 0x93, "visual preferences must preserve LCDC");
                        f.ppu.setRenderLayers(true, true, true);
                        f.advance(70224);
                        check(f.pixel(0, 0) == background, "layers must render again when reenabled");
                    }

            for (const bool color : {false, true})
                for (const bool tiles : {false, true})
                    for (const bool renderWindow : {false, true})
                    {
                        Fixture f(color);
                        f.ppu.setRenderLayers(tiles, true, renderWindow);
                        f.ppu.reset(color, color, false);
                        f.bus.write(0xFF47, 0xE4);
                        f.tile(0, 0xFF, 0);
                        f.tile(1, 0xFF, 0xFF);
                        f.memory.write(0x9C00, 1);
                        f.bus.write(0xFF4A, 0);
                        f.bus.write(0xFF4B, 87); // BG até x=79; window a partir de x=80.
                        if (color)
                        {
                            f.color(false, 0, 1, 0x001F);
                            f.color(false, 0, 3, 0x03E0);
                        }
                        f.render(0xF1);
                        check(f.pixel(0, 0) == (tiles ? (color ? 0xFFFF0000u : light) : white),
                              "window preference must leave background visibility independent");
                        check(f.pixel(80, 0) == (renderWindow ? (color ? 0xFF00FF00u : black) : white),
                              "window visibility must be independent of Render Tiles and survive reset");
                        f.ppu.setRenderLayers(true, true, true);
                        f.advance(70224);
                        check(f.pixel(80, 0) == (color ? 0xFF00FF00u : black),
                              "reenabling window must restore its pixels");
                    }
        }

        void colorPalettesAndAttributes()
        {
            Fixture f(true);
            f.tile(0, 0, 0, 0);
            f.tile(0, 0x80, 0, 1);
            f.memory.write(0xFF4F, 1);
            f.memory.write(0x9800, 0x29); // Palette 1, bank 1, X flip.
            f.color(false, 1, 1, 0x001F);
            f.render();
            check(f.pixel(7) == 0xFFFF0000 && f.pixel(0) == white,
                  "CGB BG attributes select bank, palette and horizontal flip independently of VBK");

            Fixture priority(true);
            priority.tile(0, 0xFF, 0);
            priority.tile(1, 0, 0xFF, 1);
            priority.color(false, 0, 1, 0x03E0);
            priority.color(true, 2, 2, 0x7C00);
            priority.sprite(0, 8, 16, 1, 0x8A); // Behind, bank 1, palette 2.
            priority.render(0x92);              // LCDC.0 limpa apenas prioridade no CGB.
            check(priority.pixel(0) == 0xFF0000FF && priority.pixel(8) == 0xFF00FF00,
                  "CGB master priority preserves BG colors while allowing objects in front");

            Fixture bgPriority(true);
            bgPriority.tile(0, 0xFF, 0);
            bgPriority.tile(1, 0, 0xFF);
            bgPriority.tile(3, 0, 0);
            bgPriority.memory.write(0x9801, 3);
            bgPriority.memory.write(0xFF4F, 1);
            bgPriority.memory.write(0x9800, 0x80);
            bgPriority.memory.write(0x9801, 0x80);
            bgPriority.memory.write(0xFF4F, 0);
            bgPriority.color(false, 0, 1, 0x03E0);
            bgPriority.color(true, 0, 2, 0x7C00);
            bgPriority.sprite(0, 8, 16, 1);
            bgPriority.sprite(1, 16, 16, 1);
            bgPriority.render(0x93);
            check(bgPriority.pixel(0) == 0xFF00FF00 && bgPriority.pixel(8) == 0xFF0000FF,
                  "CGB tile priority covers objects except where BG raw color is zero");

            for (const auto coordinatePriority : {false, true})
            {
                Fixture overlap(true);
                overlap.tile(1, 0xFF, 0);
                overlap.tile(2, 0, 0xFF);
                overlap.color(true, 0, 1, 0x001F);
                overlap.color(true, 0, 2, 0x7C00);
                overlap.sprite(0, 12, 16, 2);
                overlap.sprite(1, 8, 16, 1);
                overlap.bus.write(0xFF6C, coordinatePriority ? 1 : 0);
                overlap.render(0x93);
                check(overlap.pixel(4) == (coordinatePriority ? 0xFFFF0000 : 0xFF0000FF),
                      "CGB OAM-index priority and OPRI coordinate override");
            }

            Fixture access(true);
            access.bus.write(0xFF68, 0xBF);
            access.bus.write(0xFF69, 0x12);
            check(access.bus.read(0xFF68) == 0xC0, "CGB palette auto-increment wraps and unused bit reads high");
            access.bus.write(0xFF68, 0xBF);
            check(access.bus.read(0xFF69) == 0x12 && access.bus.read(0xFF68) == 0xFF, "palette read does not increment");
            access.bus.write(0xFF40, 0x91);
            access.advance(80);
            access.bus.write(0xFF69, 0x34);
            check(access.bus.read(0xFF69) == 0xFF && access.bus.read(0xFF68) == 0xC0,
                  "blocked palette write still increments index during mode 3");
            access.advance(172);
            access.bus.write(0xFF68, 63);
            check(access.bus.read(0xFF69) == 0x12, "mode 3 palette write must not change CRAM");
        }

        void dmaAndBoard()
        {
            Fixture f;
            f.tile(1, 0xFF, 0xFF);
            f.sprite(0, 8, 16, 1);
            f.bus.write(0xC000, 16);
            f.bus.write(0xC001, 8);
            f.bus.write(0xC002, 1);
            f.bus.write(0xFF46, 0xC0);
            f.advance(8);
            f.ppu.reset(false, false, true);
            f.ppu.write(0xFF40, 0x93, f.interrupts); // Porta interna, sem bloqueio da CPU por DMA.
            f.advance(65664);
            check(f.memory.read(0xFE00) == 16 && f.memory.read(0xFE01) == 8, "DMA writes OAM despite PPU mode locks");
            check(f.pixel(0, 0) == white && f.pixel(0, 2) == black,
                  "OAM scan hides sprites examined during DMA and sees them after transfer");

            Fixture vram;
            vram.memory.write(0x8000, 0x55);
            vram.bus.write(0xFF40, 0x91);
            vram.advance(80);
            vram.bus.write(0xFF46, 0x80);
            vram.advance(8);
            check(vram.memory.read(0xFE00) == 0xFF, "DMA VRAM source observes PPU mode 3 lock");

            Core::Board board;
            std::vector<std::uint8_t> rom(32768);
            rom[0x100] = 0x76; // HALT sem IME; PPU continua recebendo dots.
            board.loadCartridge(rom, "PPU integration");
            check(board.read(0xFF40) == 0x91 && (board.read(0xFF41) & 3) == 2, "Board post-boot PPU profile");
            board.write(0xFF0F, 0);
            while (board.clockCycles() < 65664)
                board.step();
            check(board.frameNumber() == 1 && board.read(0xFF44) == 144 && (board.read(0xFF0F) & 1),
                  "Board clocks PPU and VBlank while CPU halted");
            check(board.framebuffer()[0] == white, "Board exposes published framebuffer");
        }
    }

    int runPPUTests()
    {
        try
        {
            modesAndBus();
            frameAndInterrupts();
            completeFrameCadence();
            lcdStartupAndCoincidence();
            statWritePulse();
            videoMemoryAccess();
            boardVideoInterrupts();
            backgroundAndScroll();
            windowAndRaster();
            windowDisableAndRestart();
            fetchRegisterSampling();
            scrollCarryAndDisabledWindow();
            liveFineScrollStartup();
            sharedFetcherAlignment();
            fetchAddressAndState();
            spritesAndPriority();
            objectFetchSampling();
            objectSelectionAndMasking();
            overlappingObjectsAtZero();
            unlimitedSprites();
            renderLayerVisibility();
            colorPalettesAndAttributes();
            dmaAndBoard();
            std::cout << "PPU FIFO, modes, interrupts, pixel composition, CGB and integration tests passed.\n";
            return 0;
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
}

int main() { return R2BOY::Test::runPPUTests(); }
