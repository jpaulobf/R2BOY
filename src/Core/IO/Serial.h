#pragma once

#include "Interrupts.h"
#include <cstdint>
#include <string>

namespace R2BOY::Core::IO
{
    /**
     * Porta serial sem cabo conectado: recebe bits 1 e captura os bytes enviados.
     * Implementa SB/SC, clock interno normal/rápido CGB e pedido de interrupção.
     * O clock externo permanece aguardando um parceiro (link cable não implementado).
     * A Board avança o clock em T-cycles entre acessos da CPU a cada M-cycle.
     * A fase exata do divisor ainda não é emulada.
     */
    class Serial
    {
    public:
        // Limpa a transferência e a saída capturada; cgbMode habilita o clock rápido.
        void reset(bool cgbMode = false);
        
        // Conteúdo atual de SB, incluindo o deslocamento parcial durante a transferência.
        std::uint8_t readSB() const { return data_; }
        
        // Consulta SC com os bits não utilizados fixos conforme o modo DMG/CGB.
        std::uint8_t readSC() const { return control_ | (cgbMode_ ? 0x7C : 0x7E); }
        
        // Substitui o registrador de dados, inclusive durante uma transferência.
        void writeSB(std::uint8_t value) { data_ = value; }
        
        // Aplica os bits válidos de controle e reinicia a contagem da transferência.
        void writeSC(std::uint8_t value);
        
        // Avança T-cycles do clock interno; ao completar oito bits, pede IRQ de serial.
        void tick(unsigned tCycles, Interrupts &interrupts);
        
        // Entrega os bytes transmitidos e esvazia o buffer de diagnóstico.
        std::string takeOutput();

    private:
        std::uint8_t data_ = 0;
        std::uint8_t control_ = 0;
        std::uint8_t transmitted_ = 0;
        unsigned bits_ = 0;
        unsigned elapsed_ = 0;
        bool cgbMode_ = false;
        std::string output_;
    };
}