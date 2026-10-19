#include "Core/Board.h"
#include <functional>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace R2BOY::Test
{
    namespace
    {
        using Processor = Core::CPU::CPU;
        using Type = Processor::CycleType;
        using Cycle = Processor::Cycle;

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
            Core::BUS::BUS bus{cartridge, memory, interrupts, serial, timer, dma, ppu, joypad};
            Processor cpu{bus, [this]()
                          {
                              for (unsigned t = 0; t < 4; ++t)
                              {
                                  ++elapsed;
                                  timer.tick(interrupts);
                                  serial.tick(1, interrupts);
                              }
                              if (onClock)
                                  onClock(elapsed);
                          }};
            std::vector<Cycle> trace;
            unsigned elapsed = 0;
            std::function<void(unsigned)> onClock;

            Fixture()
            {
                cpu.setCycleObserver([this](const Cycle &cycle)
                                     { trace.push_back(cycle); });
            }

            void prepare(std::initializer_list<std::uint8_t> program, std::uint8_t flags = 0)
            {
                memory.reset();
                interrupts.reset();
                serial.reset();
                timer.reset();
                trace.clear();
                elapsed = 0;
                onClock = {};
                Processor::Registers r;
                r.pc = 0xC000;
                r.sp = 0xD000;
                r.setHL(0xC100);
                r.f = flags;
                cpu.reset(r);
                std::uint16_t address = r.pc;
                for (const auto byte : program)
                    bus.write(address++, byte);
            }
        };

        void expect(const Fixture &f, std::initializer_list<Cycle> expected)
        {
            check(f.trace.size() == expected.size(), "unexpected number of M-cycles");
            unsigned index = 0;
            for (const auto &cycle : expected)
            {
                const auto &actual = f.trace[index++];
                check(actual.type == cycle.type && actual.address == cycle.address &&
                          actual.value == cycle.value && actual.tCycles == cycle.tCycles,
                      "wrong bus access/internal cycle position");
            }
        }

        void memoryAccesses()
        {
            Fixture f;
            f.prepare({0xFA, 0x00, 0xC1}); // LD A,[C100]
            f.bus.write(0xC100, 0x11);
            f.onClock = [&](unsigned time)
            { if (time == 16) f.bus.write(0xC100,0x42); };
            check(f.cpu.step() == 16 && f.cpu.registers().a == 0x42, "peripheral change must be visible inside instruction");
            expect(f, {{Type::Read, 0xC000, 0xFA, 4}, {Type::Read, 0xC001, 0x00, 8}, {Type::Read, 0xC002, 0xC1, 12}, {Type::Read, 0xC100, 0x42, 16}});

            f.prepare({0x34}); // INC [HL]: read-modify-write, sem ciclo interno artificial.
            f.bus.write(0xC100, 0x0F);
            check(f.cpu.step() == 12, "INC [HL] duration");
            expect(f, {{Type::Read, 0xC000, 0x34, 4}, {Type::Read, 0xC100, 0x0F, 8}, {Type::Write, 0xC100, 0x10, 12}});
            f.prepare({0xCB, 0x46});
            f.bus.write(0xC100, 1); // BIT 0,[HL] não escreve.
            check(f.cpu.step() == 12, "BIT [HL] duration");
            expect(f, {{Type::Read, 0xC000, 0xCB, 4}, {Type::Read, 0xC001, 0x46, 8}, {Type::Read, 0xC100, 1, 12}});
            f.prepare({0xCB, 0x06});
            f.bus.write(0xC100, 0x81); // RLC [HL]
            check(f.cpu.step() == 16, "RLC [HL] duration");
            expect(f, {{Type::Read, 0xC000, 0xCB, 4}, {Type::Read, 0xC001, 0x06, 8}, {Type::Read, 0xC100, 0x81, 12}, {Type::Write, 0xC100, 3, 16}});
            f.prepare({0x08, 0xFF, 0xC1}); // LD [C1FF],SP atravessa a página.
            check(f.cpu.step() == 20, "LD [a16],SP duration");
            expect(f, {{Type::Read, 0xC000, 8, 4}, {Type::Read, 0xC001, 0xFF, 8}, {Type::Read, 0xC002, 0xC1, 12}, {Type::Write, 0xC1FF, 0, 16}, {Type::Write, 0xC200, 0xD0, 20}});
        }

        void stackAndBranches()
        {
            Fixture f;
            f.prepare({0xC5}); // PUSH BC=0000: idle ANTES das duas escritas.
            check(f.cpu.step() == 16, "PUSH duration");
            expect(f, {{Type::Read, 0xC000, 0xC5, 4}, {Type::Internal, 0, 0, 8}, {Type::Write, 0xCFFF, 0, 12}, {Type::Write, 0xCFFE, 0, 16}});
            f.prepare({0xCD, 0x34, 0xC1});
            check(f.cpu.step() == 24 && f.cpu.registers().pc == 0xC134, "CALL duration/target");
            expect(f, {{Type::Read, 0xC000, 0xCD, 4}, {Type::Read, 0xC001, 0x34, 8}, {Type::Read, 0xC002, 0xC1, 12}, {Type::Internal, 0, 0, 16}, {Type::Write, 0xCFFF, 0xC0, 20}, {Type::Write, 0xCFFE, 3, 24}});
            f.prepare({0xCF});
            check(f.cpu.step() == 16 && f.cpu.registers().pc == 8, "RST duration/target");
            expect(f, {{Type::Read, 0xC000, 0xCF, 4}, {Type::Internal, 0, 0, 8}, {Type::Write, 0xCFFF, 0xC0, 12}, {Type::Write, 0xCFFE, 1, 16}});
            f.prepare({0xC0}); // RET NZ tomado: idle antes de ler a pilha e após o POP.
            f.bus.write(0xD000, 0x34);
            f.bus.write(0xD001, 0xC1);
            check(f.cpu.step() == 20 && f.cpu.registers().pc == 0xC134, "RET cc duration");
            expect(f, {{Type::Read, 0xC000, 0xC0, 4}, {Type::Internal, 0, 0, 8}, {Type::Read, 0xD000, 0x34, 12}, {Type::Read, 0xD001, 0xC1, 16}, {Type::Internal, 0, 0, 20}});
            f.prepare({0xC0}, Processor::Zero);
            check(f.cpu.step() == 8, "untaken RET cc duration");
            expect(f, {{Type::Read, 0xC000, 0xC0, 4}, {Type::Internal, 0, 0, 8}});
            f.prepare({0xC9});
            f.bus.write(0xD000, 0x34);
            f.bus.write(0xD001, 0xC1);
            check(f.cpu.step() == 16, "RET duration");
            expect(f, {{Type::Read, 0xC000, 0xC9, 4}, {Type::Read, 0xD000, 0x34, 8}, {Type::Read, 0xD001, 0xC1, 12}, {Type::Internal, 0, 0, 16}});
            f.prepare({0x20, 0xFE});
            check(f.cpu.step() == 12 && f.cpu.registers().pc == 0xC000, "taken JR duration");
            expect(f, {{Type::Read, 0xC000, 0x20, 4}, {Type::Read, 0xC001, 0xFE, 8}, {Type::Internal, 0, 0, 12}});
            f.prepare({0x20, 0xFE}, Processor::Zero);
            check(f.cpu.step() == 8, "untaken JR duration");
            expect(f, {{Type::Read, 0xC000, 0x20, 4}, {Type::Read, 0xC001, 0xFE, 8}});
            f.prepare({0xC3, 0x34, 0xC1});
            check(f.cpu.step() == 16, "JP duration");
            expect(f, {{Type::Read, 0xC000, 0xC3, 4}, {Type::Read, 0xC001, 0x34, 8}, {Type::Read, 0xC002, 0xC1, 12}, {Type::Internal, 0, 0, 16}});
            f.prepare({0xE8, 0x01}); // ADD SP,e8 tem dois ciclos internos.
            check(f.cpu.step() == 16, "ADD SP,e8 duration");
            expect(f, {{Type::Read, 0xC000, 0xE8, 4}, {Type::Read, 0xC001, 1, 8}, {Type::Internal, 0, 0, 12}, {Type::Internal, 0, 0, 16}});
        }

        void interruptsAndIdle()
        {
            Fixture f;
            f.prepare({0xFB, 0x00});
            f.cpu.step();
            f.cpu.step();
            f.trace.clear();
            f.interrupts.writeIE(1);
            f.interrupts.request(0);
            check(f.cpu.step() == 20 && f.cpu.registers().pc == 0x40, "ISR duration/vector");
            expect(f, {{Type::Internal, 0, 0, 12}, {Type::Internal, 0, 0, 16}, {Type::Write, 0xCFFF, 0xC0, 20}, {Type::Write, 0xCFFE, 2, 24}, {Type::Internal, 0, 0, 28}});
            f.prepare({0xFB, 0x00, 0x00});
            f.cpu.step();
            f.cpu.step();
            f.interrupts.writeIE(4);
            f.onClock = [&](unsigned time)
            { if (time == 12) f.interrupts.request(2); };
            check(f.cpu.step() == 4 && f.cpu.registers().pc == 0xC003, "IRQ raised during instruction waits for its boundary");
            check(f.cpu.step() == 20 && f.cpu.registers().pc == 0x50, "IRQ generated by clock is serviced next");
            f.prepare({0x76, 0});
            f.cpu.step();
            f.onClock = [&](unsigned time)
            { if (time == 8) { f.interrupts.writeIE(1); f.interrupts.request(0); } };
            check(f.cpu.step() == 4 && f.elapsed == 8, "HALT advances the hardware clock");
            check(f.cpu.step() == 4 && !f.cpu.halted() && f.cpu.registers().pc == 0xC002, "clock IRQ wakes HALT");
            f.prepare({0x10, 0});
            check(f.cpu.step() == 4 && f.cpu.stopped() && f.cpu.registers().pc == 0xC002, "STOP entry");
            expect(f, {{Type::Read, 0xC000, 0x10, 4}});
            check(f.cpu.step() == 4 && f.elapsed == 4 && f.trace.size() == 1, "STOP API wait must not clock hardware");
            f.prepare({0xD3});
            f.cpu.step();
            f.cpu.step();
            expect(f, {{Type::Read, 0xC000, 0xD3, 4}, {Type::Internal, 0, 0, 8}});
            check(f.cpu.locked() && f.elapsed == 8, "invalid CPU opcode does not stop peripheral clocks");
        }

        void serialInterleaving()
        {
            Fixture f;
            f.prepare({0xF0, 0x02}); // LDH A,[SC], com transferência terminando entre os fetches.
            f.serial.writeSB('A');
            f.serial.writeSC(0x81);
            f.serial.tick(4088, f.interrupts);
            check(f.cpu.step() == 12 && f.cpu.registers().a == 0x7F && f.serial.takeOutput() == "A",
                  "CPU must observe serial completion before instruction ends");

            Core::Board board;
            std::vector<std::uint8_t> image(32768);
            image[0x100] = 0x3E;
            image[0x101] = 0x81; // LD A,81
            image[0x102] = 0xE0;
            image[0x103] = 0x02; // LDH [SC],A
            board.loadCartridge(image, "serial cycle test");
            board.write(0xFF01, 'B');
            board.step();
            board.step();
            check(board.clockCycles() == 20, "Board clock counts each CPU M-cycle exactly once");
            while (board.clockCycles() < 20 + 4092)
                board.step();
            check((board.read(0xFF02) & 0x80) != 0 && board.takeSerialOutput().empty(),
                  "SC write must not retroactively receive instruction cycles");
            board.step();
            check(board.clockCycles() == 20 + 4096 && board.takeSerialOutput() == "B" &&
                      board.clockCycles() == board.cpu().cycles(),
                  "serial starts at the write boundary, without double clocking");
            board.reset();
            check(board.clockCycles() == 0 && board.cpu().cycles() == 0, "reset clears both clock counters");
        }
    }

    int runTimingTests()
    {
        try
        {
            memoryAccesses();
            stackAndBranches();
            interruptsAndIdle();
            serialInterleaving();
            std::cout << "M-cycle ordering and peripheral-interleaving tests passed.\n";
            return 0;
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
}

int main() { return R2BOY::Test::runTimingTests(); }
