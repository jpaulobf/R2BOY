#include "Core/Board.h"
#include <iostream>
#include <stdexcept>

namespace R2BOY::Test
{
    int runSerialTests()
    {
        const auto check = [](bool condition, const char *message)
        {
            if (!condition)
                throw std::runtime_error(message);
        };
        try
        {
            Core::IO::Serial serial;
            Core::IO::Interrupts interrupts;
            serial.reset();
            check(serial.readSC() == 0x7E && serial.readSB() == 0, "DMG serial reset");
            serial.writeSB('A');
            serial.writeSC(0x81);
            serial.tick(512, interrupts);
            check(serial.readSB() == 0x83 && (serial.readSC() & 0x80) != 0 &&
                      serial.takeOutput().empty(),
                  "serial shifts bits over time");
            serial.tick(3583, interrupts);
            check((serial.readSC() & 0x80) != 0 && (interrupts.readIF() & 8) == 0, "serial must wait 4096 T-cycles");
            serial.tick(1, interrupts);
            check(serial.readSB() == 0xFF && serial.readSC() == 0x7F &&
                      (interrupts.readIF() & 8) != 0 && serial.takeOutput() == "A",
                  "serial completion and interrupt");
            check(serial.takeOutput().empty(), "serial output drains once");
            serial.reset(true);
            interrupts.reset();
            check(serial.readSC() == 0x7F, "CGB serial reset mask");
            serial.writeSB('B');
            serial.writeSC(0x83);
            serial.tick(127, interrupts);
            check(serial.takeOutput().empty(), "CGB fast serial boundary");
            serial.tick(1, interrupts);
            check(serial.takeOutput() == "B" && (interrupts.readIF() & 8) != 0, "CGB fast serial completion");
            serial.reset();
            interrupts.reset();
            serial.writeSB('C');
            serial.writeSC(0x80);
            serial.tick(100000, interrupts);
            check(serial.readSB() == 'C' && (serial.readSC() & 0x80) != 0 &&
                      serial.takeOutput().empty() && (interrupts.readIF() & 8) == 0,
                  "external clock waits for partner");
            serial.writeSC(0x81);
            serial.tick(1024, interrupts);
            serial.writeSC(0);
            serial.tick(4096, interrupts);
            check(serial.takeOutput().empty() && (interrupts.readIF() & 8) == 0, "aborted serial transfer");

            Core::Board board;
            std::vector<std::uint8_t> image(32768);
            image[0x100] = 0x18;
            image[0x101] = 0xFE; // JR -2 mantém o clock ativo.
            board.loadCartridge(image, "serial integration");
            board.write(0xFF01, 'D');
            board.write(0xFF02, 0x81);
            while (board.cpu().cycles() < 4096)
                board.step();
            check(board.takeSerialOutput() == "D" && board.read(0xFF01) == 0xFF &&
                      (board.read(0xFF02) & 0x80) == 0 && (board.read(0xFF0F) & 8) != 0,
                  "Board clocks serial through BUS");
            std::cout << "Serial transfer tests passed.\n";
            return 0;
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
}

int main() { return R2BOY::Test::runSerialTests(); }
