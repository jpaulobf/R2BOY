#include "Core/Board.h"
#include "Common/RomFile.h"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace R2BOY::Test
{
    // Blargg/Mooneye sem SDL ou hacks; Mealybug captura o frame no breakpoint.
    // Saídas: 0=Passed/capturado (não implica imagem correta), 1=falha, 2=timeout.
    int runROM(int argc, char **argv)
    {
        if (argc < 2 || argc > 6)
        {
            std::cerr << "Usage: rom_runner <rom.gb> [max-T-cycles=1000000000] [dmg|cgb] [blargg|mooneye|mealybug] [frame.ppm for mealybug]\n";
            return 1;
        }
        try
        {
            std::uint64_t limit = 1000000000;
            if (argc >= 3)
            {
                const std::string input = argv[2];
                std::size_t parsed = 0;
                if (input.empty() || input.front() == '-')
                    throw std::runtime_error("Invalid cycle limit.");
                limit = std::stoull(input, &parsed);
                if (parsed != input.size() || limit == 0)
                    throw std::runtime_error("Invalid cycle limit.");
            }
            Core::Model model = Core::Model::DMG;
            if (argc >= 4)
            {
                const std::string selected = argv[3];
                if (selected == "cgb")
                    model = Core::Model::CGB;
                else if (selected != "dmg")
                    throw std::runtime_error("Model must be dmg or cgb.");
            }
            bool mooneye = false;
            bool mealybug = false;
            if (argc >= 5)
            {
                const std::string protocol = argv[4];
                if (protocol == "mooneye")
                    mooneye = true;
                else if (protocol == "mealybug")
                    mealybug = true;
                else if (protocol != "blargg")
                    throw std::runtime_error("Protocol must be blargg, mooneye or mealybug.");
            }
            if (mealybug != (argc == 6))
                throw std::runtime_error("Mealybug requires an output PPM path; other protocols do not accept one.");
            const auto path = std::filesystem::u8path(argv[1]);
            Core::Board board(model);
            board.setCPUOverclock(false);
            board.setUnlimitedSprites(false);
            board.loadCartridge(Common::RomFile::read(path), path.u8string());
            std::string output;
            unsigned poll = 0;
            while (board.cpu().cycles() < limit)
            {
                const auto pc = board.cpu().registers().pc;
                const bool breakpoint = (mooneye || mealybug) && board.read(pc) == 0x40;
                const auto elapsed = board.step();
                if (board.cpu().locked())
                    throw std::runtime_error("CPU locked on an invalid opcode.");
                if (breakpoint && elapsed == 4 && board.cpu().registers().pc == static_cast<std::uint16_t>(pc + 1))
                {
                    if (mealybug)
                    {
                        std::ofstream frame(std::filesystem::u8path(argv[5]), std::ios::binary);
                        frame << "P6\n160 144\n255\n";
                        for (const auto pixel : board.framebuffer())
                            for (const unsigned shift : {16u, 8u, 0u})
                                frame.put(static_cast<char>(pixel >> shift));
                        frame.close();
                        if (!frame)
                            throw std::runtime_error("Failed to write captured frame.");
                        std::cout << "Result: FRAME CAPTURED (Mealybug, " << board.cpu().cycles()
                                  << " T-cycles; compare image separately)\n";
                        return 0;
                    }
                    const auto &r = board.cpu().registers();
                    const std::array<std::uint8_t, 6> actual{r.b, r.c, r.d, r.e, r.h, r.l};
                    constexpr std::array<std::uint8_t, 6> passed{3, 5, 8, 13, 21, 34};
                    constexpr std::array<std::uint8_t, 6> failed{0x42, 0x42, 0x42, 0x42, 0x42, 0x42};
                    if (actual == passed || actual == failed)
                    {
                        std::cout << "Result: " << (actual == passed ? "PASS" : "FAIL")
                                  << " (Mooneye, " << board.cpu().cycles() << " T-cycles)\n";
                        return actual == passed ? 0 : 1;
                    }
                }
                if (++poll < 1024)
                    continue;
                poll = 0;
                const auto text = board.takeSerialOutput();
                std::cout << text << std::flush;
                output += text;
                if (!mooneye && !mealybug && (output.find("Failed") != std::string::npos || output.find("FAILED") != std::string::npos))
                {
                    std::cout << "\nResult: FAIL (" << board.cpu().cycles() << " T-cycles)\n";
                    return 1;
                }
                if (!mooneye && !mealybug && (output.find("Passed\n") != std::string::npos || output.find("Passed all tests\n") != std::string::npos))
                {
                    std::cout << "\nResult: PASS (" << board.cpu().cycles() << " T-cycles)\n";
                    return 0;
                }
            }
            std::cout << board.takeSerialOutput() << std::flush;
            std::cerr << "\nResult: TIMEOUT at PC=" << std::hex << board.cpu().registers().pc
                      << std::dec << " after " << board.cpu().cycles() << " T-cycles\n";
            return 2;
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
}

int main(int argc, char **argv) { return R2BOY::Test::runROM(argc, argv); }
