#pragma once

#include "Core/StateArchive.h"
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
     * O clock deriva das bordas do contador de sistema, sem reiniciar sua fase por SC.
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
        
        // Reinicia a contagem dos bits e força SCK baixo, preservando a fase do divisor.
        void writeSC(std::uint8_t value, Interrupts &interrupts);
        
        // Observa o contador de sistema, avançado ou zerado por DIV/STOP.
        // Duas bordas do divisor geram um deslocamento; oito bits pedem IRQ.
        void clock(std::uint16_t previous, std::uint16_t current, Interrupts &interrupts);
        
        // Entrega os bytes transmitidos e esvazia o buffer de diagnóstico.
        std::string takeOutput();

    private:
        friend class R2BOY::Core::StateArchive;
        void state(StateArchive &archive)
        {
            archive(data_, control_, transmitted_, bits_, masterClock_, cgbMode_, output_);
        }
        void edge(Interrupts &interrupts);
        std::uint8_t data_ = 0;
        std::uint8_t control_ = 0;
        std::uint8_t transmitted_ = 0;
        unsigned bits_ = 0;
        bool masterClock_ = false;
        bool cgbMode_ = false;
        std::string output_;
    };
}