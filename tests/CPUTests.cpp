#include "Core/Board.h"
#include <algorithm>
#include <array>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

namespace R2BOY::Test
{
    namespace
    {
        using Processor = Core::CPU::CPU;
        using Registers = Processor::Registers;
        constexpr auto Z = Processor::Zero, N = Processor::Subtract,
                       H = Processor::HalfCarry, C = Processor::Carry;

        void check(bool condition, const std::string &message)
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
            Processor cpu{bus};

            static Registers initial()
            {
                Registers r;
                r.pc = 0xC000;
                r.sp = 0xD000;
                r.setBC(0xC200);
                r.setDE(0xC300);
                r.setHL(0xC100);
                return r;
            }

            void prepare(std::initializer_list<std::uint8_t> program, Registers r = initial())
            {
                interrupts.reset();
                serial.reset();
                timer.reset();
                auto address = r.pc;
                for (const auto byte : program)
                    bus.write(address++, byte);
                cpu.reset(r);
            }
        };

        bool invalid(unsigned op)
        {
            switch (op)
            {
            case 0xD3:
            case 0xDB:
            case 0xDD:
            case 0xE3:
            case 0xE4:
            case 0xEB:
            case 0xEC:
            case 0xED:
            case 0xF4:
            case 0xFC:
            case 0xFD:
                return true;
            default:
                return false;
            }
        }

        void instructionCoverage()
        {
            // Tabela independente de tempos para F=0 (NZ/NC tomados; Z/C não tomados).
            constexpr std::array<unsigned, 64> lowCycles = {
                4, 12, 8, 8, 4, 4, 8, 4, 20, 8, 8, 8, 4, 4, 8, 4,
                4, 12, 8, 8, 4, 4, 8, 4, 12, 8, 8, 8, 4, 4, 8, 4,
                12, 12, 8, 8, 4, 4, 8, 4, 8, 8, 8, 8, 4, 4, 8, 4,
                12, 12, 8, 8, 12, 12, 12, 4, 8, 8, 8, 8, 4, 4, 8, 4};
            constexpr std::array<unsigned, 64> highCycles = {
                20, 12, 16, 16, 24, 16, 8, 16, 8, 16, 12, 8, 12, 24, 8, 16,
                20, 12, 16, 4, 24, 16, 8, 16, 8, 16, 12, 4, 12, 4, 8, 16,
                12, 12, 8, 4, 4, 16, 8, 16, 16, 4, 16, 4, 4, 4, 8, 16,
                12, 12, 8, 4, 4, 16, 8, 16, 12, 8, 16, 4, 4, 4, 8, 16};
            Fixture f;
            for (unsigned opcode = 0; opcode < 256; ++opcode)
            {
                f.prepare({static_cast<std::uint8_t>(opcode), 0xC1, 0xC1});
                unsigned expected = 4;
                if (opcode < 0x40)
                    expected = lowCycles[opcode];
                else if (opcode < 0x80)
                    expected = opcode == 0x76 ? 4 : (((opcode >> 3) & 7) == 6 || (opcode & 7) == 6 ? 8 : 4);
                else if (opcode < 0xC0)
                    expected = (opcode & 7) == 6 ? 8 : 4;
                else
                    expected = highCycles[opcode - 0xC0];
                check(f.cpu.step() == expected, "base opcode timing " + std::to_string(opcode));
                check(f.cpu.locked() == invalid(opcode), "base opcode coverage " + std::to_string(opcode));
                check((f.cpu.registers().f & 15) == 0, "F low nibble invariant");
                if (invalid(opcode))
                {
                    const auto pc = f.cpu.registers().pc;
                    check(f.cpu.step() == 4 && f.cpu.registers().pc == pc, "invalid opcode locks until reset");
                }
            }
        }

        std::uint8_t registerValue(const Registers &r, unsigned index)
        {
            switch (index)
            {
            case 0:
                return r.b;
            case 1:
                return r.c;
            case 2:
                return r.d;
            case 3:
                return r.e;
            case 4:
                return r.h;
            case 5:
                return r.l;
            default:
                return r.a;
            }
        }

        void prefixedInstructions()
        {
            Fixture f;
            constexpr std::array<std::uint8_t, 8> rotated = {0x03, 0xC0, 0x03, 0xC0, 0x02, 0xC0, 0x18, 0x40};
            for (unsigned op = 0; op < 256; ++op)
            {
                auto r = Fixture::initial();
                r.a = r.b = r.c = r.d = r.e = r.h = r.l = 0x81;
                r.f = C;
                f.prepare({0xCB, static_cast<std::uint8_t>(op)}, r);
                f.bus.write(0x8181, 0x81);
                const unsigned group = op >> 6, bit = (op >> 3) & 7, reg = op & 7;
                const unsigned expectedCycles = reg == 6 ? (group == 1 ? 12 : 16) : 8;
                check(f.cpu.step() == expectedCycles, "CB opcode cycles");
                std::uint8_t expected = 0x81, flags = C;
                if (group == 0)
                {
                    expected = rotated[bit];
                    flags = bit == 6 ? 0 : C;
                }
                if (group == 1)
                    flags = C | H | ((0x81 & (1 << bit)) == 0 ? Z : 0);
                if (group == 2)
                    expected = static_cast<std::uint8_t>(0x81 & ~(1 << bit));
                if (group == 3)
                    expected = static_cast<std::uint8_t>(0x81 | (1 << bit));
                const auto actual = reg == 6 ? f.bus.read(0x8181) : registerValue(f.cpu.registers(), reg);
                check(actual == expected && f.cpu.registers().f == flags, "CB result/flags " + std::to_string(op));
                check(f.cpu.registers().pc == 0xC002 && !f.cpu.locked(), "CB fetch length");
            }
            f.prepare({0xCB, 0x37, 0x07}); // SWAP A, RLCA
            check(f.cpu.step() == 8 && f.cpu.registers().f == Z, "SWAP zero clears carry");
            check(f.cpu.step() == 4 && f.cpu.registers().f == 0, "RLCA zero clears Z");
        }

        void exhaustiveArithmetic()
        {
            Fixture f;
            // Resultado e flags comparados com aritmética inteira, cobrindo todas as entradas de 8 bits.
            for (unsigned operation = 0; operation < 8; ++operation)
                for (unsigned a = 0; a < 256; ++a)
                    for (unsigned b = 0; b < 256; ++b)
                        for (unsigned carry = 0; carry <= (operation == 1 || operation == 3 ? 1u : 0u); ++carry)
                        {
                            auto r = Fixture::initial();
                            r.a = static_cast<std::uint8_t>(a);
                            r.f = carry ? C : 0;
                            f.prepare({static_cast<std::uint8_t>(0xC6 + operation * 8), static_cast<std::uint8_t>(b)}, r);
                            const bool subtract = operation == 2 || operation == 3 || operation == 7;
                            int result = 0;
                            std::uint8_t flags = 0;
                            if (operation <= 3 || operation == 7)
                            {
                                result = subtract ? static_cast<int>(a) - static_cast<int>(b) - static_cast<int>(carry)
                                                  : static_cast<int>(a + b + carry);
                                const int nibble = subtract ? static_cast<int>(a % 16) - static_cast<int>(b % 16) - static_cast<int>(carry)
                                                            : static_cast<int>(a % 16 + b % 16 + carry);
                                flags = (subtract ? N : 0) | ((nibble < 0 || nibble >= 16) ? H : 0) |
                                        ((result < 0 || result >= 256) ? C : 0);
                            }
                            else if (operation == 4)
                            {
                                result = a & b;
                                flags = H;
                            }
                            else if (operation == 5)
                                result = a ^ b;
                            else
                                result = a | b;
                            const auto byte = static_cast<std::uint8_t>(result);
                            if (byte == 0)
                                flags |= Z;
                            check(f.cpu.step() == 8, "ALU immediate cycles");
                            check(f.cpu.registers().a == (operation == 7 ? a : byte) && f.cpu.registers().f == flags,
                                  "exhaustive ALU result/flags");
                        }

            for (unsigned subtract = 0; subtract < 2; ++subtract)
                for (unsigned a = 0; a < 100; ++a)
                    for (unsigned b = 0; b < 100; ++b)
                        for (unsigned carry = 0; carry < 2; ++carry)
                        {
                            const auto bcd = [](unsigned value)
                            { return static_cast<std::uint8_t>((value / 10) * 16 + value % 10); };
                            auto r = Fixture::initial();
                            r.a = bcd(a);
                            r.f = carry ? C : 0;
                            f.prepare({static_cast<std::uint8_t>(subtract ? 0xDE : 0xCE), bcd(b), 0x27}, r);
                            f.cpu.step();
                            f.cpu.step();
                            const int result = subtract ? static_cast<int>(a) - static_cast<int>(b) - static_cast<int>(carry)
                                                        : static_cast<int>(a + b + carry);
                            const bool overflow = result < 0 || result >= 100;
                            const auto decimal = static_cast<unsigned>((result + 100) % 100);
                            const auto flags = (decimal == 0 ? Z : 0) | (subtract ? N : 0) | (overflow ? C : 0);
                            check(f.cpu.registers().a == bcd(decimal) && f.cpu.registers().f == flags, "DAA decimal arithmetic");
                        }

            for (unsigned sp = 0; sp < 256; ++sp)
                for (unsigned byte = 0; byte < 256; ++byte)
                {
                    auto r = Fixture::initial();
                    r.sp = static_cast<std::uint16_t>(0xFF00 | sp);
                    f.prepare({0xF8, static_cast<std::uint8_t>(byte)}, r);
                    const int offset = byte < 128 ? byte : static_cast<int>(byte) - 256;
                    const auto expected = static_cast<std::uint16_t>(r.sp + offset);
                    const auto flags = ((sp % 16 + byte % 16) >= 16 ? H : 0) | (sp + byte >= 256 ? C : 0);
                    check(f.cpu.step() == 12 && f.cpu.registers().hl() == expected &&
                              f.cpu.registers().sp == r.sp && f.cpu.registers().f == flags,
                          "signed SP displacement");
                }
        }

        void loadsAndStack()
        {
            Fixture f;
            for (unsigned dest = 0; dest < 8; ++dest)
                for (unsigned src = 0; src < 8; ++src)
                {
                    if (dest == 6 && src == 6)
                        continue; // 0x76 é HALT, não LD [HL],[HL].
                    auto state = Fixture::initial();
                    state.a = 0xA7;
                    state.b = 0xB0;
                    state.c = 0xC1;
                    state.d = 0xD2;
                    state.e = 0xE3;
                    state.f = Z | C;
                    f.prepare({static_cast<std::uint8_t>(0x40 + dest * 8 + src)}, state);
                    f.bus.write(state.hl(), 0x69);
                    const auto expected = src == 6 ? 0x69 : registerValue(state, src);
                    f.cpu.step();
                    const auto actual = dest == 6 ? f.bus.read(state.hl()) : registerValue(f.cpu.registers(), dest);
                    check(actual == expected && f.cpu.registers().f == state.f, "LD register matrix");
                }
            for (unsigned reg = 0; reg < 8; ++reg)
            {
                auto state = Fixture::initial();
                state.f = Z | C;
                f.prepare({static_cast<std::uint8_t>(0x06 + reg * 8), 0x5A}, state);
                f.cpu.step();
                check((reg == 6 ? f.bus.read(state.hl()) : registerValue(f.cpu.registers(), reg)) == 0x5A &&
                          f.cpu.registers().f == state.f,
                      "LD immediate register matrix");
            }
            auto r = Fixture::initial();
            r.setBC(0x1234);
            r.setDE(0x5678);
            r.setAF(0xABF0);
            f.prepare({0xC5, 0xD5, 0xF5, 0xC1, 0xD1, 0xF1}, r);
            for (int i = 0; i < 3; ++i)
                check(f.cpu.step() == 16, "PUSH cycles");
            check(f.cpu.registers().sp == 0xCFFA && f.bus.read(0xCFFA) == 0xF0 && f.bus.read(0xCFFB) == 0xAB, "stack byte order");
            for (int i = 0; i < 3; ++i)
                check(f.cpu.step() == 12, "POP cycles");
            check(f.cpu.registers().bc() == 0xABF0 && f.cpu.registers().de() == 0x5678 &&
                      f.cpu.registers().af() == 0x1230 && f.cpu.registers().sp == r.sp,
                  "POP AF masks low nibble");
            r = Fixture::initial();
            r.setBC(0x1234);
            r.sp = 0;
            f.prepare({0xC5, 0xC1}, r);
            f.cpu.step();
            check(f.cpu.registers().sp == 0xFFFE && f.bus.read(0xFFFF) == 0x12 && f.bus.read(0xFFFE) == 0x34, "stack wraps through IE/HRAM");
            f.cpu.step();
            check(f.cpu.registers().bc() == 0x1234 && f.cpu.registers().sp == 0, "stack wrap POP");

            f.prepare({0x31, 0x34, 0x12, 0x08, 0xFF, 0xC1, 0x21, 0x00, 0xC1, 0x3E, 0x56, 0x22, 0x32, 0x2A});
            f.cpu.step();
            f.cpu.step();
            check(f.bus.read(0xC1FF) == 0x34 && f.bus.read(0xC200) == 0x12, "LD [a16],SP little endian");
            f.cpu.step();
            f.cpu.step();
            f.cpu.step();
            f.cpu.step();
            check(f.cpu.registers().hl() == 0xC100 && f.bus.read(0xC100) == 0x56 && f.bus.read(0xC101) == 0x56, "HL postincrement/decrement");
            f.cpu.step();
            check(f.cpu.registers().a == 0x56 && f.cpu.registers().hl() == 0xC101, "LD A,[HL+]");
            f.prepare({0x3E, 0x42, 0xE0, 0x80, 0x3E, 0, 0xF0, 0x80, 0x0E, 0x81, 0xE2, 0x3E, 0, 0xF2});
            for (int i = 0; i < 8; ++i)
                f.cpu.step();
            check(f.cpu.registers().a == 0x42 && f.bus.read(0xFF81) == 0x42, "LDH immediate and C forms");
            r = Fixture::initial();
            r.pc = 0xFFFE;
            f.prepare({0x06}, r);
            f.bus.write(0xFFFF, 0x42);
            f.cpu.step();
            check(f.cpu.registers().b == 0x42 && f.cpu.registers().pc == 0, "PC wraps at FFFF");
            r = Fixture::initial();
            r.setHL(0xFFFF);
            r.sp = 1;
            r.f = Z;
            f.prepare({0x39}, r);
            f.cpu.step();
            check(f.cpu.registers().hl() == 0 && f.cpu.registers().f == (Z | H | C), "ADD HL preserves Z");
            r = Fixture::initial();
            r.b = 0x0F;
            r.f = C;
            f.prepare({0x04, 0x05}, r);
            f.cpu.step();
            check(f.cpu.registers().b == 0x10 && f.cpu.registers().f == (H | C), "INC preserves C");
            f.cpu.step();
            check(f.cpu.registers().b == 0x0F && f.cpu.registers().f == (N | H | C), "DEC preserves C");
        }

        void flowControl()
        {
            Fixture f;
            for (unsigned cond = 0; cond < 4; ++cond)
                for (unsigned taken = 0; taken < 2; ++taken)
                {
                    auto r = Fixture::initial();
                    const unsigned flag = cond < 2 ? Z : C;
                    r.f = ((cond & 1) != 0) == (taken != 0) ? flag : 0;
                    f.prepare({static_cast<std::uint8_t>(0x20 + cond * 8), 0xFE}, r);
                    check(f.cpu.step() == (taken ? 12u : 8u) && f.cpu.registers().pc == (taken ? 0xC000 : 0xC002), "JR conditional paths");
                    f.prepare({static_cast<std::uint8_t>(0xC2 + cond * 8), 0x23, 0xC1}, r);
                    check(f.cpu.step() == (taken ? 16u : 12u) && f.cpu.registers().pc == (taken ? 0xC123 : 0xC003), "JP conditional paths");
                    f.prepare({static_cast<std::uint8_t>(0xC4 + cond * 8), 0x23, 0xC1}, r);
                    check(f.cpu.step() == (taken ? 24u : 12u) && f.cpu.registers().pc == (taken ? 0xC123 : 0xC003), "CALL conditional paths");
                    if (taken)
                        check(f.bus.read(0xCFFE) == 3 && f.bus.read(0xCFFF) == 0xC0, "CALL return address");
                    f.prepare({static_cast<std::uint8_t>(0xC0 + cond * 8)}, r);
                    f.bus.write(0xD000, 0x23);
                    f.bus.write(0xD001, 0xC1);
                    check(f.cpu.step() == (taken ? 20u : 8u) && f.cpu.registers().pc == (taken ? 0xC123 : 0xC001), "RET conditional paths");
                }
            for (unsigned vector = 0; vector < 8; ++vector)
            {
                f.prepare({static_cast<std::uint8_t>(0xC7 + vector * 8)});
                check(f.cpu.step() == 16 && f.cpu.registers().pc == vector * 8 && f.bus.read(0xCFFE) == 1, "RST vectors");
            }
            f.prepare({0xCD, 0x10, 0xC0});
            f.bus.write(0xC010, 0xC9);
            check(f.cpu.step() == 24 && f.cpu.step() == 16 && f.cpu.registers().pc == 0xC003 && f.cpu.registers().sp == 0xD000, "CALL/RET roundtrip");
        }

        void interruptAndSleep()
        {
            Fixture f;
            for (unsigned bit = 0; bit < 5; ++bit)
            {
                f.prepare({0xFB, 0x00, 0x76});
                f.cpu.step();
                f.cpu.step();
                f.cpu.step();
                check(f.cpu.halted(), "HALT with IME enabled");
                f.interrupts.writeIE(0x1F);
                f.interrupts.request(bit);
                check(f.cpu.step() == 20 && f.cpu.registers().pc == 0x40 + bit * 8 &&
                          !f.cpu.halted() && f.interrupts.pending() == 0,
                      "peripheral request wakes HALT and selects vector");
            }
            auto state = Fixture::initial();
            state.f = 0xFF;
            f.prepare({0}, state);
            check(f.cpu.registers().f == 0xF0, "reset sanitizes F low nibble");
            f.prepare({0xFB, 0x00, 0x00});
            f.bus.write(0xFFFF, 0x1F);
            f.bus.write(0xFF0F, 0x1F);
            check(f.cpu.step() == 4 && !f.cpu.ime(), "EI is delayed");
            check(f.cpu.step() == 4 && f.cpu.ime(), "EI takes effect after next instruction");
            check(f.cpu.step() == 20 && !f.cpu.ime() && f.cpu.registers().pc == 0x40, "interrupt vector priority/cycles");
            check(f.bus.read(0xFF0F) == 0xFE && f.bus.read(0xCFFE) == 2 && f.bus.read(0xCFFF) == 0xC0, "interrupt acknowledge/stack");
            check(f.cpu.cycles() == 28, "total T-cycles");
            f.prepare({0xFB, 0xF3, 0x00});
            f.bus.write(0xFFFF, 1);
            f.bus.write(0xFF0F, 1);
            f.cpu.step();
            f.cpu.step();
            f.cpu.step();
            check(!f.cpu.ime() && f.cpu.registers().pc == 0xC003, "DI cancels delayed EI");
            f.prepare({0xFB, 0xFB, 0x00});
            f.bus.write(0xFFFF, 1);
            f.bus.write(0xFF0F, 1);
            f.cpu.step();
            f.cpu.step();
            check(f.cpu.ime() && f.cpu.step() == 20, "EI EI does not postpone first EI");
            f.prepare({0xD9});
            f.bus.write(0xD000, 0x12);
            f.bus.write(0xD001, 0xC1);
            check(f.cpu.step() == 16 && f.cpu.ime() && f.cpu.registers().pc == 0xC112, "RETI enables IME immediately");
            f.prepare({0x76, 0x06, 0x42});
            check(f.cpu.step() == 4 && f.cpu.halted(), "HALT enters idle state");
            check(f.cpu.step() == 4 && f.cpu.registers().pc == 0xC001, "HALT idle PC");
            f.bus.write(0xFFFF, 4);
            f.bus.write(0xFF0F, 4);
            check(f.cpu.step() == 8 && !f.cpu.halted() && f.cpu.registers().b == 0x42 && f.bus.read(0xFF0F) == 0xE4, "HALT wake with IME clear does not service interrupt");
            f.prepare({0x76, 0x06, 0x42});
            f.bus.write(0xFFFF, 1);
            f.bus.write(0xFF0F, 1);
            f.cpu.step();
            f.cpu.step();
            check(f.cpu.registers().b == 0x06 && f.cpu.registers().pc == 0xC002, "HALT bug duplicates opcode as immediate");
            f.prepare({0x76, 0xCB, 0x11});
            f.bus.write(0xFFFF, 1);
            f.bus.write(0xFF0F, 1);
            f.cpu.step();
            check(f.cpu.step() == 8 && f.cpu.registers().e == 2 && f.cpu.registers().pc == 0xC002,
                  "HALT bug must duplicate the CB prefix into SET 1,E, suppressing only one PC increment");
            f.prepare({0x76, 0xFF, 0});
            f.bus.write(0xFFFF, 1);
            f.bus.write(0xFF0F, 1);
            f.cpu.step();
            check(f.cpu.step() == 16 && f.cpu.registers().pc == 0x38 &&
                      f.bus.read(0xCFFE) == 1 && f.bus.read(0xCFFF) == 0xC0,
                  "HALT bug followed by RST must push the RST address, not the following byte");
            f.prepare({0xFB, 0x76});
            f.bus.write(0xFFFF, 1);
            f.bus.write(0xFF0F, 1);
            f.cpu.step();
            f.cpu.step();
            check(f.cpu.step() == 20 && f.bus.read(0xCFFE) == 1, "EI HALT interrupt return address");
            f.prepare({0x10, 0x00, 0x00});
            check(f.cpu.step() == 4 && f.cpu.stopped() && f.cpu.registers().pc == 0xC002, "STOP consumes padding");
            check(f.cpu.step() == 4 && f.cpu.registers().pc == 0xC002, "STOP idle");
            f.cpu.wakeFromStop();
            f.cpu.step();
            check(!f.cpu.stopped() && f.cpu.registers().pc == 0xC003, "STOP explicit wake");
        }

        void boardIntegration()
        {
            std::vector<std::uint8_t> image(32768);
            image[0x14D] = 1;
            const std::uint8_t program[] = {0x3E, 0x42, 0xEA, 0x00, 0xC0, 0x76};
            std::copy(std::begin(program), std::end(program), image.begin() + 0x100);
            Core::Board board;
            check(board.step() == 0, "empty board must not execute");
            board.loadCartridge(image, "CPU test");
            check(board.cpu().registers().af() == 0x01B0 && board.cpu().registers().bc() == 0x0013 &&
                      board.cpu().registers().de() == 0x00D8 && board.cpu().registers().hl() == 0x014D &&
                      board.cpu().registers().sp == 0xFFFE && board.cpu().registers().pc == 0x100 && !board.cpu().ime(),
                  "DMG post-boot registers");
            check(board.step() == 8 && board.step() == 16 && board.step() == 4 && board.read(0xC000) == 0x42, "ROM program executes through BUS");
            board.reset();
            check(!board.cpu().halted() && board.cpu().cycles() == 0 && board.cpu().registers().pc == 0x100, "board reset clears CPU state");
            image[0x14D] = 0;
            board.loadCartridge(image, "zero checksum");
            check(board.cpu().registers().f == Z, "DMG flags depend on header checksum");
            Core::Board color(Core::Model::CGB);
            image[0x143] = 0x80;
            color.loadCartridge(image, "CGB");
            check(color.cpu().registers().af() == 0x1180 && color.cpu().registers().de() == 0xFF56 &&
                      color.cpu().registers().hl() == 0x000D,
                  "CGB native CPU profile");
            image[0x143] = 0;
            image[0x14B] = 1;
            image[0x134] = 0x43;
            color.loadCartridge(image, "CGB compatibility");
            check(color.cpu().registers().b == 0x43 && color.cpu().registers().hl() == 0x991A &&
                      color.cpu().registers().de() == 8,
                  "CGB compatibility title-dependent CPU profile");
        }
    }

    int runCPUTests()
    {
        try
        {
            instructionCoverage();
            prefixedInstructions();
            exhaustiveArithmetic();
            loadsAndStack();
            flowControl();
            interruptAndSleep();
            boardIntegration();
            std::cout << "SM83 opcode, arithmetic, interrupt and integration tests passed.\n";
            return 0;
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
}

int main() { return R2BOY::Test::runCPUTests(); }
