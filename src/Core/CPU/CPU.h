#pragma once

namespace R2BOY::Core::CPU
{
    /**
     * Reserva a CPU SM83: registradores, decodificação e execução de instruções.
     * Os acessos à memória deverão passar pelo BUS; a contagem de ciclos permitirá
     * sincronizar os demais componentes da Board.
     * A classe ainda é um esqueleto, sem execução implementada.
     */
    class CPU
    {
    };
}