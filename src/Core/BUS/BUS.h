#pragma once

namespace R2BOY::Core::BUS
{
    /**
     * Reserva o barramento que interpreta o mapa de endereços do console.
     * Encaminhará leituras e escritas à memória, ao cartucho ou aos registradores
     * dos dispositivos. O armazenamento e o comportamento de cada região ficam
     * com o componente responsável. O roteamento ainda não está implementado.
     */
    class BUS
    {
    };
}
