#pragma once

namespace R2BOY::Core::IO
{
    /**
     * Reserva os pedidos e as máscaras de habilitação de interrupções.
     * Reunirá os estados dos registradores IF e IE para que a CPU possa avaliar
     * as interrupções pendentes. A classe ainda não possui comportamento implementado.
     */
    class Interrupts
    {
    };
}