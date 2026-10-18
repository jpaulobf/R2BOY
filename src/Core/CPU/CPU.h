#pragma once

#include "Core/BUS/BUS.h"
#include <cstdint>
#include <functional>
#include <utility>

namespace R2BOY::Core::CPU
{
    /**
     * Executa as instruções SM83, incluindo o prefixo CB, usando somente o BUS.
     * step() continua por instrução, mas cada acesso e ciclo interno avança um
     * M-cycle (4 T-cycles) pelo callback de clock da Board. O acesso ao BUS ocorre
     * no final desse M-cycle. Não modela as fases elétricas dentro de cada T-cycle.
     * STOP tem comportamento básico, sem troca de velocidade CGB.
     */
    class CPU
    {
    public:
        struct Registers
        {
            std::uint8_t a = 0, f = 0, b = 0, c = 0, d = 0, e = 0, h = 0, l = 0;
            std::uint16_t sp = 0, pc = 0;
            std::uint16_t af() const { return static_cast<std::uint16_t>((a << 8) | f); }
            std::uint16_t bc() const { return static_cast<std::uint16_t>((b << 8) | c); }
            std::uint16_t de() const { return static_cast<std::uint16_t>((d << 8) | e); }
            std::uint16_t hl() const { return static_cast<std::uint16_t>((h << 8) | l); }
            void setAF(std::uint16_t value)
            {
                a = value >> 8;
                f = value & 0xF0;
            }
            void setBC(std::uint16_t value)
            {
                b = value >> 8;
                c = value & 0xFF;
            }
            void setDE(std::uint16_t value)
            {
                d = value >> 8;
                e = value & 0xFF;
            }
            void setHL(std::uint16_t value)
            {
                h = value >> 8;
                l = value & 0xFF;
            }
        };

        enum Flag : std::uint8_t
        {
            Carry = 0x10,
            HalfCarry = 0x20,
            Subtract = 0x40,
            Zero = 0x80
        };

        // O BUS e os alvos do callback devem viver mais que a CPU. Clock é chamado
        // uma vez por M-cycle, antes do acesso ao BUS; pode ser omitido em testes isolados.
        using Clock = std::function<void()>;
        explicit CPU(BUS::BUS &bus, Clock clock = {}) : bus_(bus), clock_(std::move(clock)) {}
        CPU(const CPU &) = delete;
        CPU &operator=(const CPU &) = delete;

        // Recebe um perfil inicial explícito; limpa IME, HALT/STOP, lock e ciclos.
        void reset(Registers registers);

        // Executa uma instrução, atende uma interrupção ou avança um período ocioso.
        // Opcodes inválidos travam a CPU até reset(), como no hardware.
        // Em STOP, os 4 ciclos retornados/contabilizados são uma unidade de espera da API;
        // não representam o avanço do oscilador parado nem devem acionar timers.
        unsigned step();
        const Registers &registers() const { return r_; }
        bool ime() const { return ime_; }
        bool halted() const { return halted_; }
        bool stopped() const { return stopped_; }
        bool locked() const { return locked_; }
        std::uint64_t cycles() const { return cycles_; }

        // Rastreamento opcional de acessos e ciclos internos, após cada M-cycle.
        // O observador não deve executar step()/reset() de forma reentrante.
        enum class CycleType
        {
            Read,
            Write,
            Internal
        };
        struct Cycle
        {
            CycleType type;
            std::uint16_t address;
            std::uint8_t value;
            std::uint64_t tCycles;
        };
        using CycleObserver = std::function<void(const Cycle &)>;
        void setCycleObserver(CycleObserver observer) { observer_ = std::move(observer); }

        // Ponto de integração futuro com a transição de entrada do joypad.
        void wakeFromStop() { stopped_ = false; }

    private:
        // Um acesso ou idle consome exatamente um M-cycle, sem completar ciclos no fim da instrução.
        void machineCycle();
        void idle();
        std::uint8_t read8(std::uint16_t address);
        void write8(std::uint16_t address, std::uint8_t value);
        std::uint8_t fetch8();
        std::uint16_t fetch16();
        std::uint8_t readR(unsigned index);
        void writeR(unsigned index, std::uint8_t value);
        std::uint16_t readPair(unsigned index, bool stack = false) const;
        void writePair(unsigned index, std::uint16_t value, bool stack = false);
        void push(std::uint16_t value);
        std::uint16_t pop();
        bool condition(unsigned index) const;
        void alu(unsigned operation, std::uint8_t value);
        void daa();
        std::uint16_t addSignedSP(std::uint8_t value);
        unsigned execute(std::uint8_t opcode);
        unsigned executeCB(std::uint8_t opcode);
        unsigned finish(unsigned cycles);

        BUS::BUS &bus_;
        Clock clock_;
        CycleObserver observer_;
        Registers r_;
        bool ime_ = false;
        unsigned imeDelay_ = 0;
        bool halted_ = false;
        bool stopped_ = false;
        bool haltBug_ = false;
        bool locked_ = false;
        std::uint64_t cycles_ = 0;
        unsigned stepCycles_ = 0;
    };
}