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
            files.save(board, slot, slot == 2);
        }
        for (int slot = 1; slot <= 3; ++slot)
        {
            check(files.load(board, slot) == (slot == 2), "KEY1 workaround must restore");
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
        std::cout << "Save state tests passed.\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}