#pragma once

#include "Interrupts.h"
#include <cstdint>

namespace R2BOY::Core::IO
{
    /**
     * Divisor de 16 bits e timer TIMA/TMA/TAC, avançados por T-cycle.
     * TIMA conta bordas de descida do bit selecionado do divisor (9, 3, 5 ou 7).
     * Overflow mantém TIMA em zero por 4 T-cycles antes de recarregar TMA e pedir IRQ.
     * Regras: Pan Docs, Timer and Divider Registers / Timer Obscure Behaviour.
     */
    class Timer
    {
    public:
        // O modelo físico determina o comportamento de TAC, mesmo em compatibilidade DMG.
        // O contador inicial permite perfis pós-boot sem acoplar o timer ao cartucho.
        void reset(bool cgbHardware = false, std::uint16_t divider = 0);

        // Um único T-cycle. A Board suspende estas chamadas durante STOP.
        void tick(Interrupts &interrupts);

        // Registradores FF04–FF07. Endereços externos retornam 0xFF/ignoram escritas.
        std::uint8_t read(std::uint16_t address) const;

        // DIV/TAC podem gerar bordas; TIMA durante atraso cancela o reload.
        // Durante o M-cycle de reload, TIMA ignora escritas e TMA atualiza os dois.
        void write(std::uint16_t address, std::uint8_t value);

    private:
        bool signal() const;
        void increment();
        std::uint16_t divider_ = 0;
        std::uint8_t tima_ = 0;
        std::uint8_t tma_ = 0;
        std::uint8_t tac_ = 0;
        unsigned reloadDelay_ = 0;
        unsigned reloadWindow_ = 0;
        bool cgbHardware_ = false;
    };
}