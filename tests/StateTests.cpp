#include "Core/Board.h"
#include "System/StateFile.h"
#include <chrono>
#include <fstream>
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

    std::vector<std::uint8_t> rom(bool color = false, std::uint8_t type = 0x03)
    {
        std::vector<std::uint8_t> bytes(0x10000, 0);
        bytes[0x143] = color ? 0x80 : 0;
        bytes[0x147] = type;
        bytes[0x148] = 1;
        bytes[0x149] = type == 0x06 ? 0 : 3;
        // JR -2, mantendo a execu��o fora do header.
        bytes[0x100] = 0x18;
        bytes[0x101] = 0xFE;
        return bytes;
    }

    template <class F>
    void rejects(F action)
    {
        try
        {
            action();
        }
        catch (const std::exception &)
        {
            return;
        }
        throw std::runtime_error("Invalid state must be rejected");
    }

    void roundTrip(Core::Model model, std::uint8_t type)
    {
        Core::Board board(model);
        board.loadCartridge(rom(model == Core::Model::CGB, type), "state test");
        board.write(0x0000, 0x0A);
        board.write(0xA000, 0x05);
        board.write(0xC000, 0x27);
        board.write(0xFF07, 0x05);
        board.write(0xFF01, 'A');
        board.write(0xFF02, 0x81);
        board.setButtons(0x11);
        for (int i = 0; i < 40; ++i)
            board.step();
        auto snapshot = board.saveState();
        for (int i = 0; i < 7000; ++i)
            board.step();
        const auto future = board.saveState();
        const auto frame = board.framebuffer();
        board.loadState(snapshot);
        check(board.saveState() == snapshot, "Snapshot must restore every serialized field");
        for (int i = 0; i < 7000; ++i)
            board.step();
        check(board.saveState() == future, "Replaying must reproduce CPU/peripheral state exactly");
        check(board.framebuffer() == frame, "Replaying must reproduce published frame");
        check(board.cartridge().ramDirty(), "Loading state must mark restored cartridge RAM for persistence");
        const auto unchanged = board.saveState();
        auto corrupt = snapshot;
        corrupt.back() ^= 0x80;
        rejects([&]
                { board.loadState(corrupt); });
        corrupt = snapshot;
        corrupt.resize(corrupt.size() / 2);
        rejects([&]
                { board.loadState(corrupt); });
        corrupt = snapshot;
        corrupt.push_back(0);
        rejects([&]
                { board.loadState(corrupt); });
        corrupt = snapshot;
        corrupt[18] ^= 0x01;
        rejects([&]
                { board.loadState(corrupt); });
        check(board.saveState() == unchanged, "Rejected loads must preserve running hardware");
        Core::Board other(model);
        auto different = rom(model == Core::Model::CGB, type);
        different[0x150] = 1;
        other.loadCartridge(different, "other");
        rejects([&]
                { other.loadState(snapshot); });
        // Captura no meio do DMA e executa em HRAM durante o bloqueio da ROM.
        Core::CPU::CPU::Registers registers;
        registers.pc = 0xFF80;
        registers.sp = 0xFFFE;
        board.write(0xFF80, 0x18);
        board.write(0xFF81, 0xFE);
        board.cpu().reset(registers);
        board.write(0xFF46, 0xC0);
        board.step();
        auto dma = board.saveState();
        for (int i = 0; i < 100; ++i)
            board.step();
        auto afterDMA = board.saveState();
        board.loadState(dma);
        for (int i = 0; i < 100; ++i)
            board.step();
        check(board.saveState() == afterDMA, "DMA must resume at the saved byte/phase");
    }

    void dmaBusStates()
    {
        // Partida pendente, cópia ativa e troca de barramento pendente.
        for (unsigned stage = 0; stage < 3; ++stage)
        {
            Core::Board board;
            board.loadCartridge(rom(), "DMA bus state");
            board.write(0xFF40, 0);
            for (unsigned i = 0; i < 160; ++i)
            {
                board.write(static_cast<std::uint16_t>(0xC000 + i), static_cast<std::uint8_t>(0x21 + i));
                board.write(static_cast<std::uint16_t>(0x8000 + i), static_cast<std::uint8_t>(0x61 + i));
            }
            auto registers = board.cpu().registers();
            registers.pc = 0xFF80; // NOPs: um M-cycle por step().
            board.cpu().reset(registers);
            board.write(0xFF46, 0xC0);
            board.step();
            if (stage != 0)
            {
                board.step();
                board.step();
            }
            if (stage == 2)
            {
                board.write(0xFF46, 0x80);
                board.step();
            }
            const auto snapshot = board.saveState();
            const auto data = board.read(0xC100);
            const auto page = board.read(0xFF46);
            check(data == (stage == 0 ? 0 : (stage == 1 ? 0x22 : 0x23)),
                  "DMA state fixture must expose the active source byte before saving");
            board.step();
            const auto future = board.saveState();
            board.loadState(snapshot);
            check(board.read(0xC100) == data && board.read(0xFF46) == page && board.saveState() == snapshot,
                  "Loading DMA must restore bus data and pending restart before any further tick");
            board.step();
            check(board.saveState() == future, "DMA start and bus-changing restart must replay identically");

            auto previousFormat = snapshot;
            constexpr auto versionOffset = sizeof(std::uint32_t) + sizeof("R2BOY-STATE") - 1;
            previousFormat[versionOffset] = 10;
            rejects([&]
                    { board.loadState(previousFormat); });
            check(board.saveState() == future, "Rejecting format 9 must preserve the running DMA");
        }
    }

    void ppuTransitionStates()
    {
        Core::Board board;
        board.loadCartridge(rom(), "PPU transition state");
        board.write(0xFF45, 1);
        board.write(0xFF40, 0); // Congela coincidência falsa, apesar de LY voltar a zero.
        board.write(0xFF45, 0);
        board.write(0xFF41, 0x40);
        board.write(0xFF0F, 0);
        check(!(board.read(0xFF41) & 4), "LCD-off state fixture must retain a frozen false comparison");
        const auto disabled = board.saveState();
        board.write(0xFF40, 0x91);
        check((board.read(0xFF41) & 7) == 4 && (board.read(0xFF0F) & 2),
              "restored LCD startup must compare again and raise a new LYC edge");
        const auto startup = board.saveState();
        board.loadState(disabled);
        check(board.saveState() == disabled && !(board.read(0xFF41) & 4),
              "save state must preserve frozen coincidence independently of current LY and LYC");
        board.write(0xFF40, 0x91);
        check(board.saveState() == startup, "LCD off/on must replay the same STAT edge and startup phase");
        for (unsigned i = 0; i < 100; ++i)
            board.step();
        const auto future = board.saveState();
        board.loadState(startup);
        for (unsigned i = 0; i < 100; ++i)
            board.step();
        check(board.saveState() == future, "loading during startup must preserve the first line's mode sequence");
    }

    void windowFetchStates()
    {
        Core::Board board;
        auto image = rom();
        image[0x100] = 0x76; // HALT fornece um M-cycle por step, com IE=0.
        board.loadCartridge(image, "window fetch state");
        board.write(0xFF40, 0);
        board.write(0xFF47, 0xE4);
        for (unsigned row = 0; row < 8; ++row)
        {
            board.write(static_cast<std::uint16_t>(0x8000 + row * 2), 0xFF);
            board.write(static_cast<std::uint16_t>(0x8011 + row * 2), 0xFF);
        }
        board.write(0x8012, 0xFF);
        for (unsigned x = 0; x < 32; ++x)
            board.write(static_cast<std::uint16_t>(0x9C00 + x), 1);
        board.write(0xFF4B, 7);
        board.write(0xFF40, 0xF1);
        while (board.baseClockCycles() < 70224 + 104)
            board.step();
        board.write(0xFF40, 0xD1); // Desliga no meio do tile: faltam dois pixels da janela.
        const auto snapshot = board.saveState();
        const auto finish = [&]()
        {
            board.step();
            board.write(0xFF4B, 31);
            board.write(0xFF40, 0xF1);
            while (board.frameNumber() < 2)
                board.step();
            check(board.framebuffer()[7] == 0xFF555555 && board.framebuffer()[8] == 0xFFAAAAAA &&
                      board.framebuffer()[24] == 0xFF000000,
                  "resumed window tile must finish before BG and the next window row are emitted");
            return board.saveState();
        };
        const auto future = finish();
        board.loadState(snapshot);
        check(board.saveState() == snapshot, "window tile position and queued pixels must round-trip");
        check(finish() == future, "window disable/restart must replay identically after a mid-tile load");
    }

    void disabledWindowInsertionState()
    {
        Core::Board board;
        auto image = rom();
        image[0x100] = 0x76;
        board.loadCartridge(image, "disabled window insertion state");
        board.setUnlimitedSprites(false);
        board.write(0xFF40, 0);
        board.write(0xFF47, 0xE4);
        board.write(0xFF4B, 7);
        for (unsigned row = 0; row < 8; ++row)
            board.write(static_cast<std::uint16_t>(0x8000 + row * 2), 0xFF);
        board.write(0xFF40, 0x91);
        while (board.baseClockCycles() < 70224 + 92)
            board.step();
        check(board.timing().ppu.fifoSize == 1,
              "save fixture must retain the inserted pixel ahead of the pending tile push");
        const auto snapshot = board.saveState();
        const auto finish = [&]()
        {
            while (board.frameNumber() < 2)
                board.step();
            check(board.framebuffer()[0] == 0xFFFFFFFF && board.framebuffer()[1] == 0xFFAAAAAA,
                  "restored disabled-window insertion must precede the original background tile");
            return board.saveState();
        };
        const auto future = finish();
        board.loadState(snapshot);
        check(finish() == future, "queued insertion and pending tile push must replay identically");

        board.write(0xFF40, 0);
        board.write(0xFF4B, 7);
        board.write(0xFF40, 0xB1);
        const auto origin = board.baseClockCycles();
        while (board.baseClockCycles() < origin + 70224 + 96)
            board.step();
        board.write(0xFF40, 0x91); // Desliga durante os seis dots do primeiro fetch de window.
        board.write(0xFF4B, 15);   // Match no próximo push de BG, em x=8.
        const auto blocked = board.saveState();
        const auto finishBlocked = [&]()
        {
            while (board.frameNumber() < 4)
                board.step();
            check(board.framebuffer()[8] == 0xFFAAAAAA,
                  "disabling window during its initial fetch must suppress later insertion on the line");
            return board.saveState();
        };
        const auto blockedFuture = finishBlocked();
        board.loadState(blocked);
        check(finishBlocked() == blockedFuture, "window insertion suppression must survive a mid-line load");
    }

    void statWritePulseStates()
    {
        Core::Board board;
        auto image = rom();
        image[0x100] = 0x76;
        board.loadCartridge(image, "STAT pulse state");
        board.setUnlimitedSprites(false);
        board.write(0xFF40, 0);
        board.write(0xFF43, 2);
        board.write(0xFF45, 1);
        board.write(0xFF40, 0x91);
        while (board.baseClockCycles() < 70224 + 252)
            board.step();
        check((board.read(0xFF41) & 3) == 3, "STAT state fixture must be two dots before HBlank");
        board.write(0xFF0F, 0);
        board.write(0xFF41, 0);
        const auto snapshot = board.saveState();
        board.step(); // O pulso cruza HBlank no meio deste M-cycle e então expira.
        check((board.read(0xFF0F) & 2) && (board.read(0xFF41) & 3) == 0,
              "pending STAT write must raise the HBlank edge before expiring");
        const auto future = board.saveState();
        board.loadState(snapshot);
        check(board.saveState() == snapshot, "pending STAT pulse must round-trip");
        board.step();
        check(board.saveState() == future, "loading a pending STAT pulse must replay its IRQ and expiration");
    }

    void sharedFetchStates()
    {
        for (const bool zero : {false, true})
            for (unsigned dot : {88u, 92u, 96u, 100u, 108u})
            {
                Core::Board board;
                auto image = rom();
                image[0x100] = 0x76;
                board.loadCartridge(image, "shared fetch state");
                board.setUnlimitedSprites(false);
                board.write(0xFF40, 0);
                board.write(0xFE00, 16);
                board.write(0xFE01, zero ? 0 : 8);
                board.write(0xFE04, 16);
                board.write(0xFE05, zero ? 0 : 9);
                board.write(0xFF40, 0x93);
                while (board.baseClockCycles() < 70224 + dot)
                    board.step();
                const auto snapshot = board.saveState();
                const auto finish = [&]()
                {
                    board.write(0xFF42, 1);
                    while (board.frameNumber() < 2)
                        board.step();
                    return board.saveState();
                };
                const auto future = finish();
                board.loadState(snapshot);
                check(board.saveState() == snapshot, "shared BG/OBJ fetch state must round-trip");
                check(finish() == future, "alignment wait, metadata and object data phases must replay identically");
            }
    }

    void objectFetchStates()
    {
        Core::Board board;
        auto image = rom();
        image[0x100] = 0x76;
        board.loadCartridge(image, "object fetch state");
        board.setUnlimitedSprites(false);
        board.write(0xFF40, 0);
        board.write(0xFF48, 0xE4);
        for (unsigned row = 0; row < 8; ++row)
        {
            board.write(static_cast<std::uint16_t>(0x8021 + row * 2), 0xFF);
            board.write(static_cast<std::uint16_t>(0x8030 + row * 2), 0xFF);
        }
        board.write(0xFE00, 16);
        board.write(0xFE01, 8);
        board.write(0xFE02, 3);
        board.write(0xFF4B, 7);
        board.write(0xFF40, 0xB3);
        while (board.baseClockCycles() < 70224 + 108)
            board.step();
        // Plano baixo 8x8 já buscado; plano alto ainda pendente.
        const auto snapshot = board.saveState();
        const auto finish = [&]()
        {
            board.write(0xFF40, 0xB7);
            while (board.frameNumber() < 2)
                board.step();
            check(board.framebuffer()[0] == 0xFF000000,
                  "mid-OBJ state must preserve the first plane while the next samples LCDC size");
            return board.saveState();
        };
        const auto future = finish();
        board.loadState(snapshot);
        check(board.saveState() == snapshot, "partially fetched OBJ must round-trip exactly");
        check(finish() == future, "OBJ plane fetch must replay identically after loading");
    }

    void unlimitedSpriteStates()
    {
        Core::Board board;
        board.loadCartridge(rom(), "unlimited sprite state");
        board.write(0xFF40, 0);
        for (unsigned row = 0; row < 8; ++row)
        {
            board.write(static_cast<std::uint16_t>(0x8010 + row * 2), 0xFF);
            board.write(static_cast<std::uint16_t>(0x8011 + row * 2), 0xFF);
        }
        for (unsigned i = 0; i < 40; ++i)
        {
            const auto base = static_cast<std::uint16_t>(0xFE00 + i * 4);
            board.write(base, 16);
            board.write(base + 1, i < 10 ? 0 : 8 + ((i - 10) % 20) * 8);
            board.write(base + 2, 1);
            board.write(base + 3, 0);
        }
        board.write(0xFF48, 0xE4);
        board.write(0xFF40, 0x93);
        while (board.baseClockCycles() < 70224 + 120)
            board.step();
        const auto snapshot = board.saveState(); // Captura em modo 3 com trinta objetos extras.
        const auto run = [&]()
        {
            for (unsigned i = 0; i < 12000; ++i)
                board.step();
        };
        run();
        check(board.framebuffer()[0] == 0xFF000000, "saved scene must contain extra sprite pixels");
        const auto future = board.saveState();
        board.loadState(snapshot);
        check(board.saveState() == snapshot, "extra sprite selection must round-trip in mid-scanline");
        run();
        check(board.saveState() == future, "extra sprite pipeline must replay deterministically");
        board.setUnlimitedSprites(false);
        board.loadState(snapshot);
        check(!board.isUnlimitedSpritesEnabled(), "load state must preserve the current menu preference");
        board.reset();
        check(!board.isUnlimitedSpritesEnabled(), "reset must preserve the current menu preference");
    }

    void files()
    {
        const auto directory = std::filesystem::temp_directory_path() /
                               ("r2boy-state-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        struct Cleanup
        {
            std::filesystem::path path;
            ~Cleanup()
            {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
            }
        } cleanup{directory};
        System::StateFile files(directory);
        Core::Board board;
        rejects([&]
                { files.save(board, 1); });
        board.loadCartridge(rom(), "slots");
        rejects([&]
                { files.load(board, 1); });
        for (int slot = 1; slot <= 3; ++slot)
        {
            board.write(0xC000, static_cast<std::uint8_t>(slot));
            files.save(board, slot);
        }
        for (int slot = 1; slot <= 3; ++slot)
        {
            files.load(board, slot);
            check(board.read(0xC000) == slot, "Each slot must retain independent state");
        }
        board.write(0xC000, 42);
        files.save(board, 1);
        board.reset();
        files.load(board, 1);
        check(board.read(0xC000) == 42, "Existing slot must be replaced");
        const auto before = board.saveState();
        {
            std::ofstream bad(files.pathFor(board, 1), std::ios::binary | std::ios::trunc);
            bad << "broken";
        }
        rejects([&]
                { files.load(board, 1); });
        check(board.saveState() == before, "Corrupt file must preserve hardware");
        rejects([&]
                { files.save(board, 0); });
        rejects([&]
                { files.load(board, 4); });
    }
}
int main()
{
    try
    {
        for (auto model : {Core::Model::DMG, Core::Model::CGB})
            for (std::uint8_t type : {0x03, 0x06, 0x13, 0x1B})
                roundTrip(model, type);
        files();
        dmaBusStates();
        ppuTransitionStates();
        windowFetchStates();
        disabledWindowInsertionState();
        objectFetchStates();
        sharedFetchStates();
        statWritePulseStates();
        unlimitedSpriteStates();
        std::cout << "Save state tests passed.\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}