#include "Core/Board.h"
#include <algorithm>
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

    std::vector<std::uint8_t> rom()
    {
        std::vector<std::uint8_t> data(0x8000);
        // Fade NR50 from FF to 88, then mark completion in WRAM. Mirrors the
        // read/subtract/write wait used by Animaniacs without proprietary data.
        const std::uint8_t program[]{
            0x3E, 0xFF, 0xE0, 0x24, 0xF0, 0x24, 0xFE, 0x88,
            0x28, 0x06, 0xD6, 0x11, 0xE0, 0x24, 0x18, 0xF4,
            0x3E, 0x01, 0xEA, 0x00, 0xC0, 0x18, 0xFE};
        std::copy(std::begin(program), std::end(program), data.begin() + 0x100);
        return data;
    }

    void controls(Core::Model model)
    {
        Core::Board board(model);
        board.loadCartridge(rom(), "audio controls test");
        check(board.read(0xFF24) == 0x77 && board.read(0xFF25) == 0xF3, "post-boot mixer profile");
        for (unsigned i = 0; i < 256; ++i)
        {
            board.write(0xFF24, i);
            board.write(0xFF25, 255 - i);
            check(board.read(0xFF24) == i && board.read(0xFF25) == 255 - i, "mixer readback including VIN bits");
        }
        board.write(0xFF26, 0x7F);
        check(board.read(0xFF26) == 0x70, "NR52 writable power bit and unused bits");
        board.write(0xFF24, 0xFF);
        board.write(0xFF25, 0xFF);
        check(board.read(0xFF24) == 0 && board.read(0xFF25) == 0, "power-off clears and locks mixer controls");
        const auto off = board.saveState();
        board.write(0xFF26, 0x80);
        check(board.read(0xFF24) == 0 && board.read(0xFF25) == 0, "power-on preserves cleared controls");
        board.write(0xFF24, 0x23);
        board.write(0xFF25, 0xA5);
        const auto on = board.saveState();
        board.reset();
        board.loadState(on);
        check(board.read(0xFF24) == 0x23 && board.read(0xFF25) == 0xA5, "restore active mixer controls");
        board.loadState(off);
        board.write(0xFF24, 0x77);
        check(board.read(0xFF26) == 0x70 && board.read(0xFF24) == 0, "restore power-off write lock");
        board.reset();
        check(board.read(0xFF24) == 0x77 && board.read(0xFF25) == 0xF3, "reset mixer controls");
        for (unsigned i = 0; i < 100; ++i)
            board.step();
        check(board.read(0xC000) == 1 && board.read(0xFF24) == 0x88, "CPU volume fade must finish");
        board.write(0xFF46, 0xC0);
        for (unsigned i = 0; i < 3; ++i)
            board.step();
        board.write(0xFF24, 0x11);
        board.write(0xFF25, 0x22);
        check(board.read(0xFF24) == 0x11 && board.read(0xFF25) == 0x22,
              "mixer I/O remains accessible during DMA");
        board.write(0xFF26, 0);
        check(board.read(0xFF24) == 0 && board.read(0xFF25) == 0 && board.read(0xFF26) == 0x70,
              "power control remains accessible during DMA");
    }
}

int main()
{
    try
    {
        controls(Core::Model::DMG);
        controls(Core::Model::CGB);
        std::cout << "APU control tests passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
