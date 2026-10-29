#include "Core/Board.h"
#include "Common/RomFile.h"
#include "System/StateFile.h"
#include <algorithm>
#include <array>
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

    void maliciousStates(Core::Model model, bool color)
    {
        Core::Board board(model);
        board.loadCartridge(rom(color), "malicious state test");
        board.write(0xFF40, 0);
        const std::array<std::uint8_t, 16> marker{0xB9, 0x37, 0xF2, 0x51, 0xA6, 0xE4, 0x0D, 0x7C,
                                                  0x31, 0x9F, 0x62, 0xD8, 0x43, 0xAE, 0x16, 0x75};
        for (unsigned i = 0; i < marker.size(); ++i)
            board.write(static_cast<std::uint16_t>(0x8000 + i), marker[i]);
        board.write(0xFF40, 0x91);
        while (board.timing().ppu.line < 1 || board.timing().ppu.mode != 3 || board.timing().ppu.x < 10)
            board.step();
        const auto original = board.saveState();
        constexpr std::size_t headerSize = 11 + 64 + 8 + 6 * 4;
        const auto found = std::search(original.begin() + headerSize, original.end(), marker.begin(), marker.end());
        check(found != original.end(), "state test must locate its VRAM marker");
        const std::size_t memory = static_cast<std::size_t>(found - original.begin());
        check(std::search(found + 1, original.end(), marker.begin(), marker.end()) == original.end(),
              "VRAM marker must be unique in this test state");
        const std::size_t modeFlag = memory + 0x4000 + 0x8000 + 0xA0 + 0x7F;
        const std::size_t vramBank = modeFlag + 1, wramBank = modeFlag + 2, ppu = modeFlag + 3;
        constexpr std::size_t frameBytes = 160 * 144 * 4;
        const std::size_t bgFifo = ppu + 2 * frameBytes + 128;
        const std::size_t objFifo = bgFifo + 16 * 3;
        const std::size_t lcdc = objFifo + 8 * 5 + 10 * 5 + 5;
        const std::size_t line = lcdc + 6 + 7 + 7 + 6;
        const std::size_t dot = line + 4, ppuMode = dot + 4, x = ppuMode + 4;
        const std::size_t colorHardware = line + 20 + 33 + 24;
        const std::size_t colorMode = colorHardware + 1;
        const std::size_t dma = headerSize + 8 + 1 + 13 + board.cartridge().ram().size();
        check(original.at(modeFlag) == static_cast<std::uint8_t>(color) && original.at(ppuMode) == 3 &&
                  original.at(line) == board.timing().ppu.line && original.at(x) == board.timing().ppu.x &&
                  original.at(colorHardware) == static_cast<std::uint8_t>(model == Core::Model::CGB) &&
                  original.at(colorMode) == static_cast<std::uint8_t>(color),
              "state test offsets must identify actual mode and coordinates");
        const auto checksum = [&](std::vector<std::uint8_t> &state)
        {
            const std::vector<std::uint8_t> payload(state.begin() + headerSize, state.end());
            const auto hash = Common::RomFile::hash(payload);
            check(hash.size() == 8, "CRC32 hash must occupy eight characters");
            std::copy(hash.begin(), hash.end(), state.begin() + headerSize - 8);
        };
        const auto invalid = [&](std::size_t offset, std::uint8_t value, const char *expected)
        {
            auto altered = original;
            altered.at(offset) = value;
            checksum(altered);
            bool rejected = false;
            try
            {
                board.loadState(altered);
            }
            catch (const std::runtime_error &error)
            {
                rejected = true;
                check(std::string(error.what()).find(expected) != std::string::npos,
                      "corrupt state must reach the relevant semantic validation");
            }
            check(rejected, "semantically invalid state with correct CRC must be rejected");
            check(board.saveState() == original, "rejected state must leave the entire session unchanged");
        };
        invalid(vramBank, 255, "Invalid memory");
        const std::size_t mbc = headerSize + 8 + 1;
        invalid(mbc, 255, "Invalid cartridge controller");
        invalid(mbc + 4, 0, "Invalid cartridge controller");
        invalid(mbc + 5, 0, "Invalid cartridge controller");
        invalid(mbc + 6, 1, "Invalid cartridge controller");
        invalid(mbc + 7, 1, "Invalid cartridge controller");
        check(board.cartridge().hasBattery(), "rejected states must preserve cartridge battery persistence");
        invalid(wramBank, 255, "Invalid memory");
        invalid(modeFlag, color ? 0 : 1, "Invalid memory");
        invalid(dma + 2, 255, "Invalid OAM DMA");
        invalid(dma + 6, 9, "Invalid OAM DMA");
        invalid(dma + 10, 5, "Invalid OAM DMA");
        invalid(dma + 14, 1, "Invalid OAM DMA");
        invalid(dma + 15, model == Core::Model::CGB ? 0 : 1, "Invalid OAM DMA");
        invalid(colorHardware, model == Core::Model::CGB ? 0 : 1, "Invalid PPU save state");
        invalid(colorMode, color ? 0 : 1, "Invalid PPU save state");
        if (!color)
        {
            invalid(vramBank, 1, "Invalid memory");
            invalid(wramBank, 0, "Invalid memory");
        }
        invalid(line, 144, "Invalid PPU save state");
        invalid(x, 160, "Invalid PPU save state");
        invalid(dot, 79, "Invalid PPU save state");
        invalid(lcdc, static_cast<std::uint8_t>(original[lcdc] & 0x7F), "Invalid PPU save state");
        invalid(bgFifo, 255, "background FIFO");
        invalid(bgFifo + 1, color ? 8 : 1, "background FIFO");
        invalid(bgFifo + 2, 2, "background FIFO");
        invalid(objFifo, 255, "object FIFO");
        invalid(objFifo + 1, color ? 8 : 2, "object FIFO");
        invalid(bgFifo + 15 * 3, 4, "background FIFO");
        invalid(objFifo + 7 * 5, 4, "object FIFO");
        const auto hdma = original.size() - 34;
        check(original.at(hdma) == static_cast<std::uint8_t>(color), "test must locate VRAM DMA enable flag");
        invalid(hdma, color ? 0 : 1, "Invalid CGB VRAM DMA");
        if (color)
        {
            auto stalled = original;
            stalled[hdma + 1] = 1;  // active
            stalled[hdma + 6] = 16; // remaining
            stalled[hdma + 23] = 1; // busy
            stalled[hdma + 24] = 1; // HBlank, com bloco vazio e tail zero.
            checksum(stalled);
            rejects([&]
                    { board.loadState(stalled); });
            check(board.saveState() == original, "empty HDMA tail must be rejected without changing the session");
            stalled[hdma + 14] = 1; // phase=1 não torna a combinação válida.
            checksum(stalled);
            rejects([&]
                    { board.loadState(stalled); });
            check(board.saveState() == original, "HDMA validation must cover both transfer phases");
        }

        // Limites válidos também precisam retomar a execução após o carregamento.
        auto boundary = original;
        boundary[line] = 143;
        boundary[x] = 159;
        boundary[bgFifo] = boundary[objFifo] = 3;
        boundary[bgFifo + 1] = color ? 7 : 0;
        boundary[objFifo + 1] = color ? 7 : 1;
        checksum(boundary);
        board.loadState(boundary);
        for (unsigned i = 0; i < 32; ++i)
            board.step();
        board.loadState(original);

        // Fuzz determinístico dos campos de maior risco, com CRC válido e retomada.
        const std::array<std::size_t, 7> fields{vramBank, wramBank, line, x, bgFifo, bgFifo + 1, objFifo + 1};
        std::uint32_t random = 0x6C8E9CF5;
        for (unsigned i = 0; i < 200; ++i)
        {
            auto altered = original;
            random = random * 1664525u + 1013904223u;
            const auto field = fields[(random >> 16) % fields.size()];
            random = random * 1664525u + 1013904223u;
            altered[field] = static_cast<std::uint8_t>(random >> 24);
            checksum(altered);
            try
            {
                board.loadState(altered);
                for (unsigned step = 0; step < 8; ++step)
                    board.step();
                board.loadState(original);
            }
            catch (const std::runtime_error &)
            {
                check(board.saveState() == original, "rejected fuzz case must preserve the session");
            }
        }
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
        // Toda família mantém tipo, RAM, bateria e rumble definidos pelo header.
        constexpr std::size_t currentHeader = 11 + 64 + 8 + 6 * 4;
        constexpr std::size_t controller = currentHeader + 8 + 1;
        for (const unsigned field : {0u, 4u, 5u, 6u})
        {
            auto invalidController = unchanged;
            invalidController[controller + field] ^= 1;
            const std::vector<std::uint8_t> invalidPayload(invalidController.begin() + currentHeader, invalidController.end());
            const auto crc = Common::RomFile::hash(invalidPayload);
            std::copy(crc.begin(), crc.end(), invalidController.begin() + currentHeader - 8);
            rejects([&]
                    { board.loadState(invalidController); });
            check(board.saveState() == unchanged, "controller metadata rejection must preserve the whole session");
        }
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
            previousFormat[versionOffset] = 15;
            rejects([&]
                    { board.loadState(previousFormat); });
            check(board.saveState() == future, "Rejecting format 15 must preserve the running DMA");
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

    void liveFineScrollState()
    {
        Core::Board board;
        auto image = rom();
        image[0x100] = 0x76;
        board.loadCartridge(image, "live fine-scroll startup state");
        board.setUnlimitedSprites(false);
        board.write(0xFF40, 0);
        board.write(0xFF47, 0xE4);
        board.write(0xFF4B, 7);
        board.write(0xFF43, 7);
        for (unsigned row = 0; row < 8; ++row)
            board.write(static_cast<std::uint16_t>(0x8000 + row * 2), 0xFF);
        board.write(0xFF40, 0x91);
        while (board.baseClockCycles() < 70224 + 88)
            board.step();
        const auto before = board.saveState();
        board.write(0xFF43, 0); // O match de zero já passou; deve dar outra volta.
        while (board.baseClockCycles() < 70224 + 92)
            board.step();
        const auto wrapped = board.saveState();
        const auto finish = [&]()
        {
            while (board.frameNumber() < 2)
                board.step();
            check(board.framebuffer()[0] == 0xFFFFFFFF && board.framebuffer()[1] == 0xFFAAAAAA,
                  "live fine-scroll replay must preserve the inserted pixel and the fetched tile");
            return board.saveState();
        };
        const auto future = finish();
        board.loadState(wrapped);
        check(finish() == future, "fine-scroll comparator wrap must survive a mid-startup load");
        board.loadState(before);
        board.write(0xFF43, 0);
        check(finish() == future, "fine-scroll writes during startup must replay from before the wrap");
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
            for (std::uint8_t type : {0x02, 0x03, 0x06, 0x13, 0x1B, 0x1E})
                roundTrip(model, type);
        files();
        maliciousStates(Core::Model::DMG, false);
        maliciousStates(Core::Model::CGB, false);
        maliciousStates(Core::Model::CGB, true);
        dmaBusStates();
        ppuTransitionStates();
        windowFetchStates();
        disabledWindowInsertionState();
        liveFineScrollState();
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