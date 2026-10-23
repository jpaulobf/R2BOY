#include "CPU.h"
#include <cassert>

namespace R2BOY::Core::CPU
{
    void CPU::reset(Registers registers)
    {
        r_ = registers;
        r_.f &= 0xF0;
        ime_ = halted_ = stopped_ = haltBug_ = locked_ = false;
        imeDelay_ = 0;
        cycles_ = 0;
        stepCycles_ = 0;
    }

    void CPU::machineCycle()
    {
        cycles_ += 4;
        stepCycles_ += 4;
        if (clock_)
            clock_();
    }

    void CPU::idle()
    {
        machineCycle();
        if (observer_)
            observer_({CycleType::Internal, 0, 0, cycles_});
    }

    std::uint8_t CPU::read8(std::uint16_t address)
    {
        machineCycle();
        const auto value = bus_.read(address);
        if (observer_)
            observer_({CycleType::Read, address, value, cycles_});
        return value;
    }

    void CPU::write8(std::uint16_t address, std::uint8_t value)
    {
        machineCycle();
        bus_.write(address, value);
        if (observer_)
            observer_({CycleType::Write, address, value, cycles_});
    }

    std::uint8_t CPU::fetch8()
    {
        const auto value = read8(r_.pc);
        r_.pc = static_cast<std::uint16_t>(r_.pc + 1);
        return value;
    }

    std::uint16_t CPU::fetch16()
    {
        const auto low = fetch8();
        return static_cast<std::uint16_t>(low | (fetch8() << 8));
    }

    std::uint8_t CPU::readR(unsigned index)
    {
        switch (index)
        {
        case 0:
            return r_.b;
        case 1:
            return r_.c;
        case 2:
            return r_.d;
        case 3:
            return r_.e;
        case 4:
            return r_.h;
        case 5:
            return r_.l;
        case 6:
            return read8(r_.hl());
        default:
            return r_.a;
        }
    }

    void CPU::writeR(unsigned index, std::uint8_t value)
    {
        switch (index)
        {
        case 0:
            r_.b = value;
            break;
        case 1:
            r_.c = value;
            break;
        case 2:
            r_.d = value;
            break;
        case 3:
            r_.e = value;
            break;
        case 4:
            r_.h = value;
            break;
        case 5:
            r_.l = value;
            break;
        case 6:
            write8(r_.hl(), value);
            break;
        default:
            r_.a = value;
            break;
        }
    }

    std::uint16_t CPU::readPair(unsigned index, bool stack) const
    {
        switch (index)
        {
        case 0:
            return r_.bc();
        case 1:
            return r_.de();
        case 2:
            return r_.hl();
        default:
            return stack ? r_.af() : r_.sp;
        }
    }

    void CPU::writePair(unsigned index, std::uint16_t value, bool stack)
    {
        switch (index)
        {
        case 0:
            r_.setBC(value);
            break;
        case 1:
            r_.setDE(value);
            break;
        case 2:
            r_.setHL(value);
            break;
        default:
            if (stack)
                r_.setAF(value);
            else
                r_.sp = value;
            break;
        }
    }

    void CPU::push(std::uint16_t value)
    {
        // A pilha cresce para endereços menores, gravando primeiro o byte alto.
        write8(--r_.sp, static_cast<std::uint8_t>(value >> 8));
        write8(--r_.sp, static_cast<std::uint8_t>(value));
    }

    std::uint16_t CPU::pop()
    {
        const auto low = read8(r_.sp++);
        return static_cast<std::uint16_t>(low | (read8(r_.sp++) << 8));
    }

    bool CPU::condition(unsigned index) const
    {
        switch (index)
        {
        case 0:
            return (r_.f & Zero) == 0;
        case 1:
            return (r_.f & Zero) != 0;
        case 2:
            return (r_.f & Carry) == 0;
        default:
            return (r_.f & Carry) != 0;
        }
    }

    void CPU::alu(unsigned operation, std::uint8_t value)
    {
        const unsigned a = r_.a;
        const unsigned carry = (r_.f & Carry) != 0 ? 1 : 0;
        unsigned result = 0;
        switch (operation)
        {
        case 0:
        case 1: // ADD / ADC
        {
            const unsigned c = operation == 1 ? carry : 0;
            result = a + value + c;
            r_.f = ((a & 15) + (value & 15) + c > 15 ? HalfCarry : 0) |
                   (result > 255 ? Carry : 0);
            break;
        }
        case 2:
        case 3:
        case 7: // SUB / SBC / CP
        {
            const unsigned c = operation == 3 ? carry : 0;
            result = a - value - c;
            r_.f = Subtract | ((a & 15) < (value & 15) + c ? HalfCarry : 0) |
                   (a < value + c ? Carry : 0);
            break;
        }
        case 4:
            result = a & value;
            r_.f = HalfCarry;
            break;
        case 5:
            result = a ^ value;
            r_.f = 0;
            break;
        case 6:
            result = a | value;
            r_.f = 0;
            break;
        }
        if (static_cast<std::uint8_t>(result) == 0)
            r_.f |= Zero;
        if (operation != 7)
            r_.a = static_cast<std::uint8_t>(result);
    }

    void CPU::daa()
    {
        unsigned adjustment = 0;
        bool carry = (r_.f & Carry) != 0;
        if ((r_.f & Subtract) == 0)
        {
            if (carry || r_.a > 0x99)
            {
                adjustment |= 0x60;
                carry = true;
            }
            if ((r_.f & HalfCarry) != 0 || (r_.a & 15) > 9)
                adjustment |= 6;
            r_.a = static_cast<std::uint8_t>(r_.a + adjustment);
        }
        else
        {
            if (carry)
                adjustment |= 0x60;
            if ((r_.f & HalfCarry) != 0)
                adjustment |= 6;
            r_.a = static_cast<std::uint8_t>(r_.a - adjustment);
        }
        r_.f = (r_.f & Subtract) | (r_.a == 0 ? Zero : 0) | (carry ? Carry : 0);
    }

    std::uint16_t CPU::addSignedSP(std::uint8_t value)
    {
        // H/C consideram a soma dos bytes baixos, mesmo para deslocamentos negativos.
        r_.f = ((r_.sp & 15) + (value & 15) > 15 ? HalfCarry : 0) |
               ((r_.sp & 255) + value > 255 ? Carry : 0);
        const int offset = value < 0x80 ? value : static_cast<int>(value) - 256;
        return static_cast<std::uint16_t>(r_.sp + offset);
    }

    unsigned CPU::executeCB(std::uint8_t opcode)
    {
        const unsigned group = opcode >> 6, bit = (opcode >> 3) & 7, reg = opcode & 7;
        auto value = readR(reg);
        if (group == 1) // BIT preserva C e não escreve de volta na memória.
        {
            r_.f = (r_.f & Carry) | HalfCarry | ((value & (1 << bit)) == 0 ? Zero : 0);
            return reg == 6 ? 12 : 8;
        }
        if (group == 2)
            value &= static_cast<std::uint8_t>(~(1 << bit));
        else if (group == 3)
            value |= static_cast<std::uint8_t>(1 << bit);
        else
        {
            const auto old = value;
            const auto carry = (r_.f & Carry) != 0 ? 1 : 0;
            bool out = false;
            switch (bit)
            {
            case 0:
                out = (old & 0x80) != 0;
                value = static_cast<std::uint8_t>((old << 1) | (old >> 7));
                break;
            case 1:
                out = (old & 1) != 0;
                value = static_cast<std::uint8_t>((old >> 1) | (old << 7));
                break;
            case 2:
                out = (old & 0x80) != 0;
                value = static_cast<std::uint8_t>((old << 1) | carry);
                break;
            case 3:
                out = (old & 1) != 0;
                value = static_cast<std::uint8_t>((old >> 1) | (carry << 7));
                break;
            case 4:
                out = (old & 0x80) != 0;
                value = static_cast<std::uint8_t>(old << 1);
                break;
            case 5:
                out = (old & 1) != 0;
                value = (old >> 1) | (old & 0x80);
                break;
            case 6:
                value = static_cast<std::uint8_t>((old << 4) | (old >> 4));
                break;
            case 7:
                out = (old & 1) != 0;
                value = old >> 1;
                break;
            }
            r_.f = (value == 0 ? Zero : 0) | (out ? Carry : 0);
        }
        writeR(reg, value);
        return reg == 6 ? 16 : 8;
    }

    unsigned CPU::execute(std::uint8_t opcode)
    {
        // Os campos x/y/z agrupam instruções com a mesma codificação de operandos;
        // p/q separam os pares de registradores e as variantes de cada operação.
        const unsigned x = opcode >> 6, y = (opcode >> 3) & 7, z = opcode & 7;
        const unsigned p = y >> 1, q = y & 1;
        if (x == 1)
        {
            if (opcode == 0x76)
            {
                if (!ime_ && bus_.pendingInterrupts() != 0)
                    haltBug_ = true;
                else
                    halted_ = true;
                return 4;
            }
            writeR(y, readR(z));
            return y == 6 || z == 6 ? 8 : 4;
        }
        if (x == 2)
        {
            alu(y, readR(z));
            return z == 6 ? 8 : 4;
        }
        if (x == 0)
        {
            switch (z)
            {
            case 0:
                if (y == 0)
                    return 4; // NOP
                if (y == 1)
                {
                    const auto address = fetch16();
                    write8(address, static_cast<std::uint8_t>(r_.sp));
                    write8(static_cast<std::uint16_t>(address + 1), static_cast<std::uint8_t>(r_.sp >> 8));
                    return 20;
                }
                if (y == 2)
                {
                    // STOP básico ocupa dois bytes, mas apenas um M-cycle. Não
                    // inventa uma segunda leitura temporizada para o byte de padding.
                    ++r_.pc;
                    stopped_ = !bus_.stop();
                    return 4;
                }
                {
                    const auto value = fetch8();
                    const int offset = value < 128 ? value : static_cast<int>(value) - 256;
                    if (y == 3 || condition(y - 4))
                    {
                        idle();
                        r_.pc = static_cast<std::uint16_t>(r_.pc + offset);
                        return 12;
                    }
                    return 8;
                }
            case 1:
                if (q == 0)
                {
                    writePair(p, fetch16());
                    return 12;
                }
                {
                    const auto hl = r_.hl();
                    const auto value = readPair(p);
                    idle();
                    const unsigned result = hl + value;
                    r_.f = (r_.f & Zero) | ((hl & 0xFFF) + (value & 0xFFF) > 0xFFF ? HalfCarry : 0) |
                           (result > 0xFFFF ? Carry : 0);
                    r_.setHL(static_cast<std::uint16_t>(result));
                    return 8;
                }
            case 2:
            {
                const auto address = p < 2 ? readPair(p) : r_.hl();
                if (q == 0)
                    write8(address, r_.a);
                else
                    r_.a = read8(address);
                if (p >= 2)
                    r_.setHL(static_cast<std::uint16_t>(address + (p == 2 ? 1 : -1)));
                return 8;
            }
            case 3:
                idle();
                writePair(p, static_cast<std::uint16_t>(readPair(p) + (q == 0 ? 1 : -1)));
                return 8;
            case 4:
            case 5:
            {
                const auto old = readR(y);
                const auto value = static_cast<std::uint8_t>(old + (z == 4 ? 1 : -1));
                r_.f = (r_.f & Carry) | (value == 0 ? Zero : 0) | (z == 5 ? Subtract : 0) |
                       ((z == 4 ? (old & 15) == 15 : (old & 15) == 0) ? HalfCarry : 0);
                writeR(y, value);
                return y == 6 ? 12 : 4;
            }
            case 6:
                writeR(y, fetch8());
                return y == 6 ? 12 : 8;
            case 7:
                if (y < 4)
                {
                    // As rotações de A sem CB sempre limpam Z, mesmo quando A resulta em zero.
                    const auto old = r_.a;
                    const unsigned carry = (r_.f & Carry) != 0 ? 1 : 0;
                    const bool out = (y & 1) == 0 ? (old & 0x80) != 0 : (old & 1) != 0;
                    if (y == 0)
                        r_.a = static_cast<std::uint8_t>((old << 1) | (old >> 7));
                    if (y == 1)
                        r_.a = static_cast<std::uint8_t>((old >> 1) | (old << 7));
                    if (y == 2)
                        r_.a = static_cast<std::uint8_t>((old << 1) | carry);
                    if (y == 3)
                        r_.a = static_cast<std::uint8_t>((old >> 1) | (carry << 7));
                    r_.f = out ? Carry : 0;
                }
                else if (y == 4)
                    daa();
                else if (y == 5)
                {
                    r_.a ^= 0xFF;
                    r_.f |= Subtract | HalfCarry;
                }
                else if (y == 6)
                    r_.f = (r_.f & Zero) | Carry;
                else
                    r_.f = (r_.f & Zero) | ((r_.f & Carry) == 0 ? Carry : 0);
                return 4;
            }
        }
        // Bloco 3: controle de fluxo, pilha, endereços altos e operações imediatas.
        switch (z)
        {
        case 0:
            if (y < 4)
            {
                idle(); // RET cc testa a condição durante o segundo M-cycle.
                if (condition(y))
                {
                    const auto address = pop();
                    idle();
                    r_.pc = address;
                    return 20;
                }
                return 8;
            }
            if (y == 4)
            {
                const auto address = static_cast<std::uint16_t>(0xFF00 | fetch8());
                write8(address, r_.a);
                return 12;
            }
            if (y == 5)
            {
                const auto value = fetch8();
                idle();
                idle();
                r_.sp = addSignedSP(value);
                return 16;
            }
            if (y == 6)
            {
                r_.a = read8(static_cast<std::uint16_t>(0xFF00 | fetch8()));
                return 12;
            }
            {
                const auto value = fetch8();
                idle();
                r_.setHL(addSignedSP(value));
            }
            return 12;
        case 1:
            if (q == 0)
            {
                writePair(p, pop(), true);
                return 12;
            }
            if (p == 0)
            {
                const auto address = pop();
                idle();
                r_.pc = address;
                return 16;
            }
            if (p == 1)
            {
                const auto address = pop();
                idle();
                r_.pc = address;
                ime_ = true;
                imeDelay_ = 0;
                return 16;
            }
            if (p == 2)
            {
                r_.pc = r_.hl();
                return 4;
            }
            idle();
            r_.sp = r_.hl();
            return 8;
        case 2:
            if (y < 4)
            {
                const auto address = fetch16();
                if (condition(y))
                {
                    idle();
                    r_.pc = address;
                    return 16;
                }
                return 12;
            }
            if (y == 4)
            {
                write8(static_cast<std::uint16_t>(0xFF00 | r_.c), r_.a);
                return 8;
            }
            if (y == 5)
            {
                const auto address = fetch16();
                write8(address, r_.a);
                return 16;
            }
            if (y == 6)
            {
                r_.a = read8(static_cast<std::uint16_t>(0xFF00 | r_.c));
                return 8;
            }
            r_.a = read8(fetch16());
            return 16;
        case 3:
            if (y == 0)
            {
                const auto address = fetch16();
                idle();
                r_.pc = address;
                return 16;
            }
            if (y == 1)
                return executeCB(fetch8());
            if (y == 6)
            {
                ime_ = false;
                imeDelay_ = 0;
                return 4;
            }
            if (y == 7)
            {
                if (imeDelay_ == 0)
                    imeDelay_ = 2;
                return 4;
            }
            break;
        case 4:
            if (y < 4)
            {
                const auto address = fetch16();
                if (condition(y))
                {
                    idle();
                    push(r_.pc);
                    r_.pc = address;
                    return 24;
                }
                return 12;
            }
            break;
        case 5:
            if (q == 0)
            {
                idle();
                push(readPair(p, true));
                return 16;
            }
            if (p == 0)
            {
                const auto address = fetch16();
                idle();
                push(r_.pc);
                r_.pc = address;
                return 24;
            }
            break;
        case 6:
            alu(y, fetch8());
            return 8;
        case 7:
            idle();
            push(r_.pc);
            r_.pc = static_cast<std::uint16_t>(y * 8);
            return 16;
        }
        locked_ = true;
        return 4;
    }

    unsigned CPU::finish(unsigned cycles)
    {
        // Verifica a tabela em Debug; nunca adiciona ciclos faltantes ao final.
        assert(stepCycles_ == cycles);
        (void)cycles;
        return stepCycles_;
    }

    unsigned CPU::step()
    {
        stepCycles_ = 0;
        if (stopped_)
        {
            // Unidade de espera da API, sem avançar o clock do hardware.
            cycles_ += 4;
            return 4;
        }
        if (locked_)
        {
            idle(); // O lock impede instruções, mas não desliga os demais circuitos.
            return finish(4);
        }
        if (bus_.switchingSpeed())
        {
            idle(); // Sem instruções ou atendimento de IRQ durante a estabilização.
            return finish(4);
        }
        const auto pending = bus_.pendingInterrupts();
        if (pending != 0)
        {
            halted_ = false;
            if (ime_)
            {
                ime_ = false;
                imeDelay_ = 0;
                // EI; HALT com pedido pendente retorna ao HALT após o atendimento.
                if (haltBug_)
                {
                    --r_.pc;
                    haltBug_ = false;
                }
                idle();
                idle();
                write8(--r_.sp, static_cast<std::uint8_t>(r_.pc >> 8));
                // A escrita do byte alto pode atingir IE e cancelar/redirecionar
                // a IRQ. IE fica fixo daqui em diante; IF ainda recebe pedidos
                // durante o próximo M-cycle, antes da escrita do byte baixo.
                const auto enabled = bus_.enabledInterrupts();
                const auto lowAddress = --r_.sp;
                machineCycle();
                const auto selected = enabled & bus_.requestedInterrupts();
                const auto low = static_cast<std::uint8_t>(r_.pc);
                bus_.write(lowAddress, low);
                if (observer_)
                    observer_({CycleType::Write, lowAddress, low, cycles_});
                std::uint16_t vector = 0; // Dispatch cancelado ainda consome os cinco M-cycles.
                if (selected != 0)
                {
                    unsigned bit = 0;
                    while ((selected & (1 << bit)) == 0)
                        ++bit;
                    bus_.acknowledgeInterrupt(bit);
                    vector = static_cast<std::uint16_t>(0x40 + bit * 8);
                }
                idle();
                r_.pc = vector;
                return finish(20);
            }
        }
        if (halted_)
        {
            idle();
            return finish(4);
        }
        const auto opcode = read8(r_.pc);

        // O HALT bug suprime apenas este incremento: a próxima busca reutiliza o endereço.
        if (haltBug_)
            haltBug_ = false;
        else
            ++r_.pc;
        const auto cycles = execute(opcode);

        // EI só passa a valer após a instrução seguinte; DI e RETI tratam IME diretamente.
        if (imeDelay_ != 0 && --imeDelay_ == 0)
            ime_ = true;
        return finish(cycles);
    }
}
