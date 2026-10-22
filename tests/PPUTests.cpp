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
            Core::BUS::BUS bus{cartridge, memory, interrupts, serial, timer, dma, ppu, joypad, speedSwitch};

            explicit Fixture(bool color = false)
            {
                memory.reset(color);
                interrupts.reset();
                dma.reset(color);
                ppu.reset(color, color, false);
                bus.write(0xFF47, 0xE4);
                bus.write(0xFF48, 0xE4);
                bus.write(0xFF49, 0xE4);
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

            std::uint32_t pixel(unsigned x, unsigned y = 0) const { return ppu.framebuffer()[y * 160 + x]; }
        };

        void modesAndBus()
        {
            Fixture f;
            check(f.bus.read(0xFF44) == 0 && (f.bus.read(0xFF41) & 0x87) == 0x84, "LCD off LY, STAT mask and coincidence");
            f.bus.write(0x8000, 0x12);
            f.bus.write(0xFE00, 0x34);
            f.bus.write(0xFF40, 0x91);
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
            f.bus.write(0xFF40, 0x91);
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
            check(f.bus.read(0xFF44) == 144 && f.ppu.mode() == 1 && f.ppu.frameNumber() == 1,
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
            limit.tile(1, 0xFF, 0xFF);
            for (unsigned i = 0; i < 10; ++i)
                limit.sprite(i, 0, 16, 1);
            limit.sprite(10, 8, 16, 1);
            limit.render(0x93);
            check(limit.pixel(0) == white, "ten-object scanline limit counts hidden X-zero objects");

            Fixture timing;
            timing.sprite(0, 8, 16, 0);
            timing.bus.write(0xFF40, 0x93);
            timing.advance(262);
            check(timing.ppu.mode() == 3, "object fetch stalls pixel output even for transparent pixels");
            timing.advance(1);
            check(timing.ppu.mode() == 0, "aligned object incurs eleven-dot penalty");
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
            backgroundAndScroll();
            windowAndRaster();
            spritesAndPriority();
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
