#include "Core/Board.h"
#include "System/RomFile.h"
#include <iostream>
#include <stdexcept>
#include <string>

namespace R2BOY::Test
{
    // Executor de ROMs Blargg sem SDL, sem saves e com limite determinístico de T-cycles.
    // Códigos de saída: 0=Passed, 1=Failed/lock/erro, 2=limite sem resultado.
    int runROM(int argc, char **argv)
    {
        if (argc < 2 || argc > 4)
        {
            std::cerr << "Usage: rom_runner <rom.gb> [max-T-cycles=1000000000] [dmg|cgb]\n";
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
            if (argc == 4)
            {
                const std::string selected = argv[3];
                if (selected == "cgb")
                    model = Core::Model::CGB;
                else if (selected != "dmg")
                    throw std::runtime_error("Model must be dmg or cgb.");
            }
            const auto path = std::filesystem::u8path(argv[1]);
            Core::Board board(model);
            board.loadCartridge(System::RomFile::read(path), path.u8string());
            std::string output;
            unsigned poll = 0;
            while (board.cpu().cycles() < limit)
            {
                board.step();
                if (board.cpu().locked())
                    throw std::runtime_error("CPU locked on an invalid opcode.");
                if (++poll < 1024)
                    continue;
                poll = 0;
                const auto text = board.takeSerialOutput();
                std::cout << text << std::flush;
                output += text;
                if (output.find("Failed") != std::string::npos || output.find("FAILED") != std::string::npos)
                {
                    std::cout << "\nResult: FAIL (" << board.cpu().cycles() << " T-cycles)\n";
                    return 1;
                }
                if (output.find("Passed\n") != std::string::npos || output.find("Passed all tests\n") != std::string::npos)
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
