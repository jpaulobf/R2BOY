#pragma once

#include "Core/StateArchive.h"
#include "Interrupts.h"
#include <cstdint>

namespace R2BOY::Core::IO
{
    // Matriz JOYP: estado lógico ativo em 1, leitura de hardware ativa em 0.
    class Joypad
    {
    public:
        // Libera todos os botões e deixa as duas linhas de seleção desabilitadas.
        void reset()
        {
            selected_ = 0x30;
            buttons_ = 0;
        }
        
        // Lê FF00; combina os grupos selecionados e converte pressionado para bit 0.
        std::uint8_t read() const;
        
        // Altera apenas a seleção dos grupos; uma nova borda de descida pede IRQ.
        void write(std::uint8_t value, Interrupts &interrupts);
        
        // Recebe a máscara GameBoyButtons ativa em 1 e verifica as linhas selecionadas.
        void setButtons(std::uint8_t buttons, Interrupts &interrupts);

    private:
        friend class R2BOY::Core::StateArchive;
        void state(StateArchive &archive)
        {
            archive(selected_, buttons_);
        }
        // Compara a leitura anterior com a atual nos quatro bits de entrada de JOYP.
        void detectEdge(std::uint8_t previous, Interrupts &interrupts) const;
        std::uint8_t selected_ = 0x30;
        std::uint8_t buttons_ = 0;
    };
}