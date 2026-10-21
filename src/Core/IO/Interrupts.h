#pragma once

#include "Core/StateArchive.h"
#include <cstdint>

namespace R2BOY::Core::IO
{
    /**
     * Mantém IF/IE e os pedidos de interrupção; IME pertence à CPU.
     * Os cinco bits representam VBlank, STAT, timer, serial e joypad, nessa prioridade.
     * request() permite conectar os periféricos sem expor os bytes internos.
     */
    class Interrupts
    {
    public:
        // Limpa IE; o perfil pós-boot mantém o pedido inicial de VBlank em IF.
        void reset(bool afterBoot = false)
        {
            enabled_ = 0;
            requested_ = afterBoot ? 1 : 0;
        }
        // Lê IF com os três bits não utilizados fixos em 1.
        std::uint8_t readIF() const { return requested_ | 0xE0; }
        
        // Lê IE preservando todos os bits escritos, inclusive os não utilizados.
        std::uint8_t readIE() const { return enabled_; }
        
        // Substitui os cinco pedidos de IF, descartando os bits não utilizados.
        void writeIF(std::uint8_t value) { requested_ = value & 0x1F; }
        
        // Substitui o registrador IE; apenas os cinco bits inferiores habilitam IRQs.
        void writeIE(std::uint8_t value) { enabled_ = value; }
        
        // Pedidos simultaneamente ativos em IF e IE, sem considerar IME.
        std::uint8_t pending() const { return enabled_ & requested_ & 0x1F; }
        
        // Marca o pedido de índice 0–4, mesmo que IE ou IME estejam desabilitados.
        void request(unsigned bit)
        {
            if (bit < 5)
                requested_ |= static_cast<std::uint8_t>(1 << bit);
        }
        
        // Limpa somente o pedido atendido; índices fora de 0–4 são ignorados.
        void acknowledge(unsigned bit)
        {
            if (bit < 5)
                requested_ &= static_cast<std::uint8_t>(~(1 << bit));
        }

    private:
        friend class R2BOY::Core::StateArchive;
        void state(StateArchive &archive)
        {
            archive(enabled_, requested_);
        }
        std::uint8_t enabled_ = 0;
        std::uint8_t requested_ = 0;
    };
}