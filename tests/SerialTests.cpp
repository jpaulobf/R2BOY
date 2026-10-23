#include "Core/Board.h"
#include <iostream>
#include <stdexcept>

namespace R2BOY::Test
{
    namespace
    {
        void check(bool condition, const char *message)
        {
            if (!condition)
                throw std::runtime_error(message);
        }

        void sharedClockPhase()
        {
            Core::IO::Serial serial;
            Core::IO::Timer timer;
            Core::IO::Interrupts interrupts;
            const auto advance = [&](unsigned cycles)
            {
                while (cycles-- != 0)
                {
                    const auto previous = timer.systemCounter();
                    timer.tick(interrupts);
                    serial.clock(previous, timer.systemCounter(), interrupts);
                }
            };
            for (const bool fast : {false, true})
            {
                timer.reset(fast);
                serial.reset(fast);
                interrupts.reset();
                const unsigned period = fast ? 16 : 512;
                advance(period / 4);
                serial.writeSB(0x80);
                serial.writeSC(fast ? 0x83 : 0x81, interrupts);
                advance(period * 3 / 4 - 1);
                check(serial.readSB() == 0x80, "SC must wait for the shared clock's falling edge");
                advance(1);
                check(serial.readSB() == 1, "serial first bit must align to the free-running divider");
                advance(7 * period - 1);
                check((serial.readSC() & 0x80) != 0 && !(interrupts.readIF() & 8),
                      "serial completion must wait for exactly eight shift edges");
                advance(1);
                check(serial.readSB() == 0xFF && (interrupts.readIF() & 8) && serial.takeOutput() == std::string(1, char(0x80)),
                      "serial must finish independently of the SC write phase at either rate");
                interrupts.writeIF(0);
                advance(period / 4);
                serial.writeSB('R');
                serial.writeSC(fast ? 0x83 : 0x81, interrupts);
                advance(8 * period - period / 4);
                check(serial.takeOutput() == "R", "successive transfers must preserve the common divider phase");
                interrupts.writeIF(0);
                advance(2 * period);
                check(!(interrupts.readIF() & 8) && serial.takeOutput().empty(), "serial completion must request IRQ only once");
            }
        }

        void dmaAndInterrupts()
        {
            for (const auto model : {Core::Model::DMG, Core::Model::CGB})
            {
                Core::Board board(model);
                std::vector<std::uint8_t> image(32768);
                image[0x143] = model == Core::Model::CGB ? 0x80 : 0;
                image[0x50] = 0xD9; // RETI da IRQ do timer.
                board.loadCartridge(image, "DMA peripheral IRQ integration");
                board.write(0xFF40, 0);
                board.write(0xFF0F, 0);
                board.write(0xFFFF, 0);
                for (unsigned i = 0; i < 160; ++i)
                    board.write(static_cast<std::uint16_t>(0xC000 + i), static_cast<std::uint8_t>(0x44 + i));
                board.write(0xFF80, 0xFB); // EI; NOP; HALT, com pilha em HRAM.
                board.write(0xFF81, 0);
                board.write(0xFF82, 0x76);
                auto registers = board.cpu().registers();
                registers.pc = 0xFF80;
                board.cpu().reset(registers);
                board.step();
                board.step();
                board.step();
                board.write(0xFF04, 0);
                board.write(0xFF01, 'D');
                board.write(0xFF02, 0x81);
                for (unsigned t = 0; t < 3456; t += 4)
                    board.step();
                board.write(0xFF05, 0xFF);
                board.write(0xFF06, 0x42);
                board.write(0xFF07, 5);
                board.write(0xFF46, 0xC0);
                for (unsigned t = 0; t < 640; t += 4)
                    board.step();
                check(board.cpu().halted() && board.read(0xFE00) == 0xFF && (board.read(0xFF0F) & 0x0C) == 0x0C,
                      "timer and serial requests must accumulate with IE disabled during active DMA");
                check(board.takeSerialOutput() == "D" && board.read(0xFF01) == 0xFF,
                      "serial must continue shifting while OAM DMA owns its source bus");
                board.write(0xFFFF, 0x0C);
                check(board.step() == 20 && board.cpu().registers().pc == 0x50 && (board.read(0xFF0F) & 0x0C) == 8,
                      "IE write during DMA must wake HALT and acknowledge timer before serial");
                for (unsigned i = 0; i < 160; ++i)
                    check(board.read(static_cast<std::uint16_t>(0xFE00 + i)) == static_cast<std::uint8_t>(0x44 + i),
                          "peripheral IRQs must not disturb DMA data or completion");
                check(board.step() == 16 && board.cpu().ime(), "timer handler must return with RETI");
                check(board.step() == 20 && board.cpu().registers().pc == 0x58 && !(board.read(0xFF0F) & 0x0C),
                      "serial request must survive timer acknowledgement and select vector 58");
            }
        }

        void divWriteAndState()
        {
            Core::Board board;
            board.loadCartridge(std::vector<std::uint8_t>(32768), "serial divider state");
            board.write(0xFF40, 0);
            board.write(0xFF80, 0x76);
            auto registers = board.cpu().registers();
            registers.pc = 0xFF80;
            board.cpu().reset(registers);
            board.step();
            board.write(0xFF04, 0);
            board.write(0xFF01, 0x80);
            board.write(0xFF02, 0x81);
            for (unsigned t = 0; t < 256; t += 4)
                board.step();
            const auto snapshot = board.saveState(); // SCK alto, ainda sem deslocamento.
            const auto finish = [&]()
            {
                for (unsigned t = 0; t < 128; t += 4)
                    board.step();
                check(board.read(0xFF01) == 0x80, "half bit must not shift SB");
                board.write(0xFF04, 0); // Queda do bit 7 força a borda de SCK.
                check(board.read(0xFF01) == 1, "DIV write must clock the serial unit through BUS");
                for (unsigned t = 0; t < 7 * 512; t += 4)
                    board.step();
                check((board.read(0xFF0F) & 8) && board.takeSerialOutput() == std::string(1, char(0x80)),
                      "DIV-induced bit must count toward serial completion");
                return board.saveState();
            };
            const auto future = finish();
            board.loadState(snapshot);
            check(finish() == future, "save states must restore divider and SCK phase, not only the bit count");
        }
    }

    int runSerialTests()
    {
        try
        {
            Core::IO::Serial serial;
            Core::IO::Interrupts interrupts;
            Core::IO::Timer timer;
            const auto advance = [&](unsigned cycles)
            {
                while (cycles-- != 0)
                {
                    const auto previous = timer.systemCounter();
                    timer.tick(interrupts);
                    serial.clock(previous, timer.systemCounter(), interrupts);
                }
            };
            serial.reset();
            check(serial.readSC() == 0x7E && serial.readSB() == 0, "DMG serial reset");
            serial.writeSB('A');
            serial.writeSC(0x81, interrupts);
            advance(512);
            check(serial.readSB() == 0x83 && (serial.readSC() & 0x80) != 0 &&
                      serial.takeOutput().empty(),
                  "serial shifts bits over time");
            advance(3583);
            check((serial.readSC() & 0x80) != 0 && (interrupts.readIF() & 8) == 0, "serial must wait 4096 T-cycles");
            advance(1);
            check(serial.readSB() == 0xFF && serial.readSC() == 0x7F &&
                      (interrupts.readIF() & 8) != 0 && serial.takeOutput() == "A",
                  "serial completion and interrupt");
            check(serial.takeOutput().empty(), "serial output drains once");
            serial.reset(true);
            timer.reset(true);
            interrupts.reset();
            check(serial.readSC() == 0x7F, "CGB serial reset mask");
            serial.writeSB('B');
            serial.writeSC(0x83, interrupts);
            advance(127);
            check(serial.takeOutput().empty(), "CGB fast serial boundary");
            advance(1);
            check(serial.takeOutput() == "B" && (interrupts.readIF() & 8) != 0, "CGB fast serial completion");
            serial.reset();
            timer.reset();
            interrupts.reset();
            serial.writeSB('C');
            serial.writeSC(0x80, interrupts);
            advance(100000);
            check(serial.readSB() == 'C' && (serial.readSC() & 0x80) != 0 &&
                      serial.takeOutput().empty() && (interrupts.readIF() & 8) == 0,
                  "external clock waits for partner");
            serial.writeSC(0x81, interrupts);
            advance(1024);
            serial.writeSC(0, interrupts);
            advance(4096);
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
            sharedClockPhase();
            dmaAndInterrupts();
            divWriteAndState();
            std::cout << "Serial transfer, shared clock, DMA, interrupt and state tests passed.\n";
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
