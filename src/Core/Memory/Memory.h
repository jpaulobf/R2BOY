#pragma once

namespace R2BOY::Core::Memory
{
    /**
     * Reserva o armazenamento de WRAM, HRAM, VRAM, OAM e boot ROM.
     * A decodificação de endereços caberá ao BUS; ROM e RAM do cartucho pertencem
     * ao subsistema Cartridge. A classe ainda não aloca nem gerencia essas regiões.
     */
    class Memory
    {
    };
}
