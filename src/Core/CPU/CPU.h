#pragma once

#include "Core/StateArchive.h"
#include "Core/BUS/BUS.h"
#include <cstdint>
#include <functional>
#include <utility>

namespace R2BOY::Core { class Board; }

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
        // Estado dos registradores; nos pares, o primeiro byte é o mais significativo.
        struct Registers
        {
            std::uint8_t a = 0, f = 0, b = 0, c = 0, d = 0, e = 0, h = 0, l = 0;
            std::uint16_t sp = 0, pc = 0;
            // Combina o acumulador e o registrador de flags no par AF.
            std::uint16_t af() const { return static_cast<std::uint16_t>((a << 8) | f); }
            // Consulta o par BC sem alterar os registradores individuais.
            std::uint16_t bc() const { return static_cast<std::uint16_t>((b << 8) | c); }
            // Consulta o par DE sem alterar os registradores individuais.
            std::uint16_t de() const { return static_cast<std::uint16_t>((d << 8) | e); }
            // Consulta o par HL, também usado no endereçamento indireto.
            std::uint16_t hl() const { return static_cast<std::uint16_t>((h << 8) | l); }
            // Atualiza AF e descarta os quatro bits inferiores não utilizados de F.
            void setAF(std::uint16_t value)
            {
                a = value >> 8;
                f = value & 0xF0;
            }
            // Distribui os bytes alto e baixo entre B e C.
            void setBC(std::uint16_t value)
            {
                b = value >> 8;
                c = value & 0xFF;
            }
            // Distribui os bytes alto e baixo entre D e E.
            void setDE(std::uint16_t value)
            {
                d = value >> 8;
                e = value & 0xFF;
            }
            // Distribui os bytes alto e baixo entre H e L.
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
        // Conecta barramento e clock; o perfil dos registradores é aplicado por reset().
        explicit CPU(BUS::BUS &bus, Clock clock = {}) : bus_(bus), clock_(std::move(clock)) {}
        // Impede cópias que compartilhem o barramento e os callbacks da instância original.
        CPU(const CPU &) = delete;
        // Mantém os vínculos de hardware definidos na construção.
        CPU &operator=(const CPU &) = delete;

        // Recebe um perfil inicial explícito; limpa IME, HALT/STOP, lock e ciclos.
        void reset(Registers registers);

        // Executa uma instrução, atende uma interrupção ou avança um período ocioso.
        // Opcodes inválidos travam a CPU até reset(), como no hardware.
        // Em STOP, os 4 ciclos retornados/contabilizados são uma unidade de espera da API;
        // não representam o avanço do oscilador parado nem devem acionar timers.
        unsigned step();
        // Expõe o estado atual para diagnóstico, sem permitir alterações externas.
        const Registers &registers() const { return r_; }
        // Habilitação global de interrupções; não equivale aos bits de IE.
        bool ime() const { return ime_; }
        // HALT suspende instruções até um pedido habilitado, mantendo o clock ativo.
        bool halted() const { return halted_; }
        // STOP suspende o clock até uma chamada a wakeFromStop().
        bool stopped() const { return stopped_; }
        // Indica travamento por opcode inválido, recuperável apenas por reset().
        bool locked() const { return locked_; }
        // T-cycles contabilizados desde reset(), incluindo as unidades de espera de STOP.
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
        // Substitui o observador de ciclos; um callback vazio desativa o rastreamento.
        void setCycleObserver(CycleObserver observer) { observer_ = std::move(observer); }

        // Ponto de integração futuro com a transição de entrada do joypad.
        // A Board já o utiliza ao detectar um novo botão pressionado; não altera IME.
        void wakeFromStop() { stopped_ = false; }

    private:
        friend class R2BOY::Core::Board;
        friend class R2BOY::Core::StateArchive;
        void state(StateArchive &archive)
        {
            archive(r_.a, r_.f, r_.b, r_.c, r_.d, r_.e, r_.h);
            archive(r_.l, r_.sp, r_.pc, ime_, imeDelay_, halted_, stopped_);
            archive(haltBug_, locked_, cycles_, stepCycles_);
        }
        // Um acesso ou idle consome exatamente um M-cycle, sem completar ciclos no fim da instrução.
        void machineCycle();
        // Avança um M-cycle interno e o informa ao observador, sem acessar memória.
        void idle();
        // Lê um byte no final de um M-cycle e registra o acesso no observador.
        std::uint8_t read8(std::uint16_t address);
        // Grava um byte no final de um M-cycle e registra o acesso no observador.
        void write8(std::uint16_t address, std::uint8_t value);
        // Busca um byte em PC e incrementa PC, consumindo um M-cycle.
        std::uint8_t fetch8();
        // Busca dois bytes consecutivos em ordem little-endian, avançando PC.
        std::uint16_t fetch16();
        // Decodifica B/C/D/E/H/L/(HL)/A; o índice 6 realiza leitura temporizada no BUS.
        std::uint8_t readR(unsigned index);
        // Usa a mesma codificação de readR(); o índice 6 grava pela porta do BUS.
        void writeR(unsigned index, std::uint8_t value);
        // Decodifica BC/DE/HL/SP; stack troca o último par por AF.
        std::uint16_t readPair(unsigned index, bool stack = false) const;
        // Atualiza o par decodificado, preservando a máscara de F ao escrever AF.
        void writePair(unsigned index, std::uint16_t value, bool stack = false);
        // Empilha dois bytes, decrementando SP antes de cada escrita temporizada.
        void push(std::uint16_t value);
        // Desempilha dois bytes em little-endian, incrementando SP após cada leitura.
        std::uint16_t pop();
        // Avalia as condições NZ/Z/NC/C codificadas nos saltos, chamadas e retornos.
        bool condition(unsigned index) const;
        // Executa ADD/ADC/SUB/SBC/AND/XOR/OR/CP e atualiza as flags correspondentes.
        void alu(unsigned operation, std::uint8_t value);
        // Ajusta A para aritmética BCD conforme as flags da operação anterior.
        void daa();
        // Calcula SP mais deslocamento com sinal e atualiza H/C, sem modificar SP.
        std::uint16_t addSignedSP(std::uint8_t value);
        // Decodifica o opcode principal e retorna o total esperado de T-cycles.
        unsigned execute(std::uint8_t opcode);
        // Executa rotações, deslocamentos e operações de bits do prefixo CB.
        unsigned executeCB(std::uint8_t opcode);
        // Confere em Debug os ciclos já consumidos; não avança o clock.
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